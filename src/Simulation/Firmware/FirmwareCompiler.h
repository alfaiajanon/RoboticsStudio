#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;


/*
 * Compiles the robot's sketch to WebAssembly with the bundled wasi-sdk clang,
 * in the background (QProcess), against the firmware core in
 * resources/firmware. Results go to the Output panel (Log) and finished().
 *
 *   core: resources/firmware/core/*.cpp -> objects, built once per core
 *         version and cached (~/.cache/.../firmware/core-<hash>)
 *   sketch: written with robot_pins.h into sketch-<hash>/, compiled and
 *         linked with the core objects -> firmware.wasm. The hash covers the
 *         sketch, the pins header, the flags and the core, so pressing Play
 *         again without changes reuses the result.
 *
 * A .ino sketch gets what the Arduino IDE adds, `#include <Arduino.h>`, and a
 * #line so diagnostics point at the sketch's own lines. Everything else
 * (Servo.h, robot_pins.h) the sketch includes itself, so the same file builds
 * for a real board. No Arduino-style automatic prototypes: declare a
 * function before using it.
 */
class FirmwareCompiler : public QObject {
    Q_OBJECT

    public:
        struct Diagnostic {
            QString file;        // the sketch's file name, a core header, or empty (linker)
            int line = 0;
            int column = 0;
            QString severity;    // error / warning / note
            QString message;
            bool inSketch = false;
        };

        explicit FirmwareCompiler(QObject* parent = nullptr);
        ~FirmwareCompiler() override;

        // Starts a compile; finished() always follows (at once from the cache).
        // A compile still running is cancelled.
        void compile(const QString& sketchPath, const QString& sketchText, const QString& pinsHeader);
        void cancel();
        bool isBusy() const { return process != nullptr; }

        const QByteArray& wasm() const { return result; }
        const QList<Diagnostic>& diagnostics() const { return diags; }

        // Where the toolchain and the core live; empty if missing.
        static QString toolchainDir();
        static QString firmwareDir();

    signals:
        void finished(bool ok);

    private:
        struct Job {
            QStringList args;
            QString output;        // file it produces (renamed from .tmp on success)
        };

        QProcess* process = nullptr;
        QList<Job> jobs;
        QString clang;
        QString sketchName;
        QString wasmPath;
        QByteArray result;
        QList<Diagnostic> diags;
        QElapsedTimer timer;
        bool coreBuilt = false;
        int generation = 0;   // a newer compile() drops an older one's finished()

        void runNext();
        void onProcessFinished(int exitCode);
        void parseOutput(const QString& text);
        void finish(bool ok, const QString& message);
};
