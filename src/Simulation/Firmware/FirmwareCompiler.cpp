#include "FirmwareCompiler.h"

#include <algorithm>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include "Utils/Log.h"


// A compile that takes longer than this is stopped.
static constexpr int kTimeoutMs = 60000;

static const QStringList& compileFlags() {
    static const QStringList f{
        "--target=wasm32-wasip1", "-O2", "-std=gnu++17", "-fno-exceptions", "-fno-rtti",
        "-Wall", "-fno-color-diagnostics",
    };
    return f;
}

// Reactor module (setup/loop are called by the simulator, no main), 16 KB
// stack, wasi-libc's debug info dropped (names stay, for crash stacks).
static const QStringList& linkFlags() {
    static const QStringList f{"-mexec-model=reactor", "-Wl,-z,stack-size=16384", "-Wl,--strip-debug"};
    return f;
}

// Old builds are dropped once there are more than this many.
static constexpr int kKeepSketchBuilds = 20;
static constexpr int kKeepCoreBuilds = 2;

static QString cacheRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/firmware";
}

// Marks a build folder as just used: its modification time (what prune()
// sorts by) changes only when an entry is added or removed.
static void touch(const QString& dir) {
    QFile::remove(dir + "/used");
    QFile used(dir + "/used");
    if (used.open(QIODevice::WriteOnly)) used.close();
}

// Keeps the most recently used `keep` folders named prefix*.
static void prune(const QString& prefix, int keep) {
    const QFileInfoList dirs = QDir(cacheRoot()).entryInfoList({prefix + "*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (int i = keep; i < dirs.size(); ++i) QDir(dirs[i].absoluteFilePath()).removeRecursively();
}

static QString firstExisting(const QStringList& dirs, const QString& marker) {
    for (const QString& d : dirs) {
        if (!d.isEmpty() && QFileInfo::exists(d + "/" + marker)) return QDir(d).absolutePath();
    }
    return QString();
}



QString FirmwareCompiler::toolchainDir() {
    const QString app = QCoreApplication::applicationDirPath();
    return firstExisting({qEnvironmentVariable("RS_WASI_SDK"),
                          app + "/../lib/roboticsstudio/wasi-sdk",   // AppImage
#ifdef RS_SOURCE_DIR
                          QString(RS_SOURCE_DIR) + "/extern/wasi-sdk",   // dev build
#endif
                         }, "bin/clang++");
}

QString FirmwareCompiler::firmwareDir() {
    const QString app = QCoreApplication::applicationDirPath();
    return firstExisting({app + "/../share/roboticsstudio/firmware",
#ifdef RS_SOURCE_DIR
                          QString(RS_SOURCE_DIR) + "/resources/firmware",
#endif
                         }, "include/Arduino.h");
}



FirmwareCompiler::FirmwareCompiler(QObject* parent) : QObject(parent) {}

FirmwareCompiler::~FirmwareCompiler() {
    cancel();
}



void FirmwareCompiler::cancel() {
    jobs.clear();
    if (process) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
        process->deleteLater();
        process = nullptr;
    }
}



void FirmwareCompiler::compile(const QString& sketchPath, const QString& sketchText, const QString& pinsHeader,
                               const QString& variantHeader, const QStringList& defines) {
    cancel();
    ++generation;
    result.clear();
    diags.clear();
    timer.start();
    coreBuilt = false;

    const QString sdk = toolchainDir();
    const QString fw = firmwareDir();
    if (sdk.isEmpty()) {
        finish(false, "Firmware toolchain (wasi-sdk) not found. In a dev build, run tools/fetch_wasi_sdk.sh");
        return;
    }
    if (fw.isEmpty()) {
        finish(false, "Firmware core (resources/firmware) not found");
        return;
    }
    clang = sdk + "/bin/clang++";
    const QStringList flags = compileFlags() + QStringList{"--sysroot=" + sdk + "/share/wasi-sysroot", "-I" + fw + "/include"};

    // The core's version: every core file, the flags and the toolchain.
    QCryptographicHash coreHash(QCryptographicHash::Sha1);
    QStringList coreFiles;
    for (const QString& sub : {QString("include"), QString("core")}) {
        QDirIterator it(fw + "/" + sub, QDir::Files);
        while (it.hasNext()) coreFiles << it.next();
    }
    coreFiles.sort();
    for (const QString& f : coreFiles) {
        QFile file(f);
        if (file.open(QIODevice::ReadOnly)) { coreHash.addData(f.toUtf8()); coreHash.addData(file.readAll()); }
    }
    coreHash.addData(flags.join(' ').toUtf8());
    QFile pinned(sdk + "/VERSION.pinned");
    if (pinned.open(QIODevice::ReadOnly)) coreHash.addData(pinned.readAll());
    const QString coreKey = coreHash.result().toHex().left(12);

    const QString coreDir = cacheRoot() + "/core-" + coreKey;
    QDir().mkpath(coreDir);
    QStringList coreObjects;
    const bool coreReady = QFileInfo::exists(coreDir + "/complete");
    for (const QString& f : coreFiles) {
        if (!f.endsWith(".cpp")) continue;
        const QString obj = coreDir + "/" + QFileInfo(f).completeBaseName() + ".o";
        coreObjects << obj;
        if (!coreReady) jobs.append({flags + QStringList{"-c", f, "-o", obj + ".tmp"}, obj});
    }
    if (!coreReady) jobs.append({{}, coreDir + "/complete"});   // stamp: written, not compiled
    else touch(coreDir);

    // The sketch, wrapped as the Arduino IDE does for .ino.
    QFileInfo info(sketchPath);
    sketchName = info.fileName();
    QString wrapped;
    if (info.suffix().compare("ino", Qt::CaseInsensitive) == 0) wrapped += "#include <Arduino.h>\n";
    wrapped += QString("#line 1 \"%1\"\n").arg(sketchName) + sketchText;
    if (!wrapped.endsWith('\n')) wrapped += '\n';

    // The user's own headers next to the sketch (a pins.h, helpers): part of the build.
    const QString userDir = info.absolutePath();
    QStringList userHeaders = QDir(userDir).entryList({"*.h", "*.hpp"}, QDir::Files, QDir::Name);
    QStringList defineFlags;
    for (const QString& d : defines) if (!d.isEmpty()) defineFlags << "-D" + d;

    QCryptographicHash sketchHash(QCryptographicHash::Sha1);
    sketchHash.addData(wrapped.toUtf8());
    sketchHash.addData(pinsHeader.toUtf8());
    sketchHash.addData(variantHeader.toUtf8());
    sketchHash.addData(defineFlags.join(' ').toUtf8());
    for (const QString& h : userHeaders) {
        QFile f(userDir + "/" + h);
        if (f.open(QIODevice::ReadOnly)) { sketchHash.addData(h.toUtf8()); sketchHash.addData(f.readAll()); }
    }
    sketchHash.addData(coreKey.toUtf8());
    sketchHash.addData(linkFlags().join(' ').toUtf8());
    const QString sketchDir = cacheRoot() + "/sketch-" + sketchHash.result().toHex().left(12);
    wasmPath = sketchDir + "/firmware.wasm";

    if (QFileInfo::exists(wasmPath)) {
        jobs.clear();
        QFile cached(wasmPath);
        if (cached.open(QIODevice::ReadOnly)) result = cached.readAll();
        if (!result.isEmpty()) {
            touch(sketchDir);
            touch(coreDir);
            finish(true, "Firmware unchanged since the last build");
            return;
        }
    }

    QDir().mkpath(sketchDir);
    auto writeFile = [&](const QString& name, const QString& text) {
        QFile f(sketchDir + "/" + name);
        if (text.isEmpty()) { f.remove(); return true; }
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(text.toUtf8());
        return true;
    };
    if (!writeFile("robot_pins.h", pinsHeader) || !writeFile("pins_arduino.h", variantHeader) || !writeFile("sketch.cpp", wrapped)) {
        jobs.clear();
        finish(false, "Can't write the firmware build files to " + sketchDir);
        return;
    }

    jobs.append({flags + defineFlags + QStringList{"-I" + sketchDir, "-I" + userDir} + linkFlags()
                     + QStringList{sketchDir + "/sketch.cpp"} + coreObjects + QStringList{"-o", wasmPath + ".tmp"},
                 wasmPath});
    if (!coreReady) Log::info("Building the firmware core (first time only)...");
    runNext();
}



void FirmwareCompiler::runNext() {
    while (!jobs.isEmpty() && jobs.first().args.isEmpty()) {   // stamps
        QFile stamp(jobs.takeFirst().output);
        if (stamp.open(QIODevice::WriteOnly)) stamp.write("ok\n");
        coreBuilt = true;
    }
    if (jobs.isEmpty()) {
        QFile out(wasmPath);
        if (out.open(QIODevice::ReadOnly)) result = out.readAll();
        if (result.isEmpty()) {
            finish(false, "The compiler produced no firmware");
            return;
        }
        const int warnings = int(std::count_if(diags.begin(), diags.end(), [](const Diagnostic& d) { return d.severity == "warning"; }));
        finish(true, QString("Firmware compiled in %1 s%2%3")
                         .arg(timer.elapsed() / 1000.0, 0, 'f', 2)
                         .arg(coreBuilt ? " (core built)" : "")
                         .arg(warnings ? QString(", %1 warning(s)").arg(warnings) : QString()));
        return;
    }

    process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        onProcessFinished(status == QProcess::NormalExit ? exitCode : -1);
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false, "Can't start the firmware compiler: " + clang);
    });
    QTimer::singleShot(kTimeoutMs, process, [this] {
        finish(false, QString("Firmware compile took longer than %1 s; stopped").arg(kTimeoutMs / 1000));
    });
    process->start(clang, jobs.first().args);
}



void FirmwareCompiler::onProcessFinished(int exitCode) {
    parseOutput(QString::fromUtf8(process->readAll()));
    process->deleteLater();
    process = nullptr;
    const Job job = jobs.takeFirst();

    if (exitCode != 0) {
        const int errors = int(std::count_if(diags.begin(), diags.end(), [](const Diagnostic& d) { return d.severity == "error"; }));
        jobs.clear();
        finish(false, errors ? QString("Firmware compile failed: %1 error(s)").arg(errors)
                             : QString("Firmware compile failed (exit code %1)").arg(exitCode));
        return;
    }
    QFile::remove(job.output);
    QFile::rename(job.output + ".tmp", job.output);
    runNext();
}



// clang:   "Robot.ino:12:5: error: expected ';' after expression"
// wasm-ld: "wasm-ld: error: /tmp/x.o: undefined symbol: setup"
void FirmwareCompiler::parseOutput(const QString& text) {
    static const QRegularExpression clangLine(R"(^(.+?):(\d+):(\d+): (fatal error|error|warning|note): (.*)$)");
    static const QRegularExpression linkerLine(R"(^wasm-ld: (error|warning): (?:[^:]*\.o: )?(.*)$)");
    const QString fw = firmwareDir();

    for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
        Diagnostic d;
        QRegularExpressionMatch m = clangLine.match(line);
        if (m.hasMatch()) {
            d.file = m.captured(1);
            d.line = m.captured(2).toInt();
            d.column = m.captured(3).toInt();
            d.severity = m.captured(4) == "fatal error" ? "error" : m.captured(4);
            d.message = m.captured(5);
            d.inSketch = d.file == sketchName;
            if (!fw.isEmpty() && d.file.startsWith(fw)) d.file = "firmware" + d.file.mid(fw.size());
            if (d.message == "'robot_pins.h' file not found")
                d.message += " (the Real level generates no robot_pins.h: use the board's pin numbers, e.g. "
                             "servo.attach(9), or put your own robot_pins.h next to the sketch; see firmware.md, Pins)";
        } else if ((m = linkerLine.match(line)).hasMatch()) {
            d.severity = m.captured(1);
            d.message = m.captured(2);
            if (d.message == "undefined symbol: setup" || d.message == "undefined symbol: loop")
                d.message += " (the sketch must define setup() and loop())";
        } else {
            continue;   // source excerpts and carets
        }
        diags.append(d);

        const QString where = d.file.isEmpty() ? QString("link") : QString("%1:%2:%3").arg(d.file).arg(d.line).arg(d.column);
        const QString msg = where + ": " + d.message;
        if (d.severity == "error") Log::error(msg);
        else if (d.severity == "warning") Log::warning(msg);
        else Log::info(msg);
    }
}



void FirmwareCompiler::finish(bool ok, const QString& message) {
    if (process) {
        process->disconnect(this);
        process->kill();
        process->deleteLater();
        process = nullptr;
    }
    jobs.clear();
    if (!ok) result.clear();
    if (ok) {
        prune("sketch-", kKeepSketchBuilds);
        prune("core-", kKeepCoreBuilds);
    }
    if (ok) Log::info(message);
    else Log::error(message);
    // Always after compile() returns, so callers can set up their UI first.
    QTimer::singleShot(0, this, [this, ok, gen = generation] { if (gen == generation) emit finished(ok); });
}
