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
 *   sketch: written into sketch-<hash>/ with the generated headers (the
 *         ideal board's robot_pins.h at the Prototype level; a real board's
 *         variant, pins_arduino.h, at the Real level), compiled and linked
 *         with the core objects -> firmware.wasm. The sketch's own folder is
 *         on the include path after them, so the user's headers next to the
 *         sketch are found. The hash covers the sketch, the generated
 *         headers, the user's headers in the sketch folder, the defines, the
 *         flags and the core, so pressing Play again without changes reuses
 *         the result.
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
        // pinsHeader: robot_pins.h to generate (Prototype), or empty;
        // variantHeader: the board's pins_arduino.h (Real), or empty;
        // defines: the board's macros (NAME or NAME=VALUE).
        void compile(const QString& sketchPath, const QString& sketchText, const QString& pinsHeader,
                     const QString& variantHeader = QString(), const QStringList& defines = QStringList());
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
