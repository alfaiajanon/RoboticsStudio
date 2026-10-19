#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <vector>
#include "Simulation/Controller/RobotController.h"

class PinBus;
class DeviceBus;
class RealBoard;


// What the board under the firmware can do. The ideal board (Prototype) does
// everything on every pin and its buses take no time; a real board (Real
// level, from its .rsdef "board" block and pin caps) has its own limits.
struct BoardProfile {
    bool ideal = true;
    QString name = "the ideal board";
    QHash<int, QStringList> caps;     // pin number -> capabilities (real boards)
    QHash<int, double> pwmHz;         // pin number -> analogWrite frequency, where it differs
    double defaultPwmHz = 490.0;
    int adcBits = 10;
    double adcVref = 5.0;
    double i2cDefaultHz = 100000.0;
    double spiMaxHz = 8e6;

    bool can(int pin, const QString& cap) const { return ideal || caps.value(pin).contains(cap); }
    double pwmPeriodUs(int pin) const { return 1e6 / (ideal ? 490.0 : pwmHz.value(pin, defaultPwmHz)); }
    static BoardProfile fromBoard(const RealBoard& board);
};

// WAMR handles (wasm_export.h), kept out of this header.
typedef struct WASMModuleCommon* wasm_module_t;
typedef struct WASMModuleInstanceCommon* wasm_module_inst_t;
typedef struct WASMExecEnv* wasm_exec_env_t;


/*
 * Runs the robot's firmware: an Arduino sketch compiled to WebAssembly
 * (resources/firmware, the firmware compiler), in the embedded WAMR
 * interpreter, in lockstep with the physics through its LockstepGate.
 *
 * The firmware's imports (resources/firmware/include/rs_abi.h) are served
 * here: time from the gate, pins from the PinBus, Serial to the Output panel
 * (tag MCU, one entry per line). wasi-libc's few WASI calls are served too:
 * stdout/stderr to the Output panel (tagged [stdout]/[stderr]: on a real Uno
 * plain printf() prints nothing), the clock is sim time, no files. Nothing
 * reaches the host OS, so runs repeat.
 *
 * Threads: load()/stop() on the UI thread; runCycle() on the controller
 * thread; the watchdog's abort (wasm_runtime_terminate) on the physics
 * thread. The module is only replaced while the controller is out of it.
 */
class FirmwareRuntime : public RobotController {
    QString tag;
    PinBus* pins = nullptr;
    DeviceBus* devices = nullptr;

    // Kept alive while the module is loaded (WAMR reads from it).
    std::vector<uint8_t> binary;
    wasm_module_t module = nullptr;
    wasm_module_inst_t instance = nullptr;
    wasm_exec_env_t execEnv = nullptr;

    // Controller thread (and load(), while idle).
    bool setupDone = false;
    QByteArray serialLine, stdoutLine, stderrLine;

    // Pins the firmware configured or complained about (controller thread).
    QHash<int, int> pinModes;
    QHash<int, int> cameraFormats;        // SCCB pin -> pixel format set by camera_init
    QHash<int, quint64> cameraLastSeq;    // SCCB pin -> seq of the last frame handed out
    QSet<int> reportedPins;
    QSet<QString> reported;               // other one-time warnings
    BoardProfile board;
    double i2cHz = 100000.0, spiHz = 4000000.0;   // bus clocks set by the firmware

    void unload();
    bool call(const char* function);
    void flushText();

    public:
        explicit FirmwareRuntime(const QString& tag = "MCU");
        ~FirmwareRuntime() override;

        // UI thread, between runs: replace the firmware and start it at sim
        // time 0 (due on the first step). False (and logged) if it can't run.
        bool load(const QByteArray& wasm, PinBus* pinBus, DeviceBus* deviceBus = nullptr,
                  const BoardProfile& profile = BoardProfile());
        bool isLoaded() const { return instance != nullptr; }

        bool runCycle() override;
        void stop() override;

        // ---- for the native functions (controller thread) ----
        LockstepGate& clock() { return gate; }
        int pinHandle(int pin, const char* what);   // -1 (logged once) if no part has it
        int pinMode(int pin) const { return pinModes.value(pin, 0); }
        void setPinMode(int pin, int mode) { pinModes[pin] = mode; }
        QHash<int, int>& cameraFormat() { return cameraFormats; }
        QHash<int, quint64>& cameraSeq() { return cameraLastSeq; }
        PinBus* pinBus() { return pins; }
        const BoardProfile& profile() const { return board; }
        void warnOnce(const QString& key, const QString& message);
        // Real board: the bus transfer takes its bit time (simulated time).
        void setI2CClock(double hz) { i2cHz = hz; }
        void setSPIClock(double hz) { spiHz = hz; }
        void i2cBusTime(int bytes);
        void spiBusTime(int bytes);
        DeviceBus* deviceBus() { return devices; }
        // Text from the firmware: Serial (MCU) or stdout/stderr, logged per line.
        void appendText(int stream, const char* data, uint32_t len);
        enum Stream { SerialStream = 0, StdoutStream = 1, StderrStream = 2 };
};
