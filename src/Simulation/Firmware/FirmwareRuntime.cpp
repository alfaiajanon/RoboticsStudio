#include "FirmwareRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cxxabi.h>
#include <mutex>
#include <QStringList>
#include "wasm_export.h"
#include "Simulation/Controller/PinBus.h"
#include "Simulation/Controller/DeviceBus.h"
#include "Simulation/Controller/SerialHub.h"
#include "Document/RealBoard.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Utils/Log.h"


// Interpreter stack per call chain and native stack reserve (bytes).
static constexpr uint32_t kWasmStack = 64 * 1024;
static constexpr uint32_t kExecEnvStack = 64 * 1024;

// A long line without '\n' is logged anyway once it gets this long.
static constexpr int kMaxLine = 1024;

// WASI errno values (wasi_snapshot_preview1).
static constexpr int32_t kWasiSuccess = 0;
static constexpr int32_t kWasiEBADF = 8;
static constexpr int32_t kWasiEINVAL = 28;
static constexpr int32_t kWasiESPIPE = 70;


static FirmwareRuntime* runtimeOf(wasm_exec_env_t env) {
    return static_cast<FirmwareRuntime*>(wasm_runtime_get_user_data(env));
}

// millis()/micros() wrap at 2^32 like on a real board.
static uint32_t wrap32(double v) {
    return uint32_t(uint64_t(v));
}


#pragma region rs natives

static uint32_t n_millis(wasm_exec_env_t env) { return wrap32(runtimeOf(env)->clock().millis()); }
static uint32_t n_micros(wasm_exec_env_t env) { return wrap32(runtimeOf(env)->clock().micros()); }
static void n_delay_us(wasm_exec_env_t env, uint32_t us) { runtimeOf(env)->clock().wait(us / 1e6); }
static void n_yield(wasm_exec_env_t env) { runtimeOf(env)->clock().wait(0.0); }

static void n_pin_mode(wasm_exec_env_t env, int32_t pin, int32_t mode) {
    FirmwareRuntime* fw = runtimeOf(env);
    if (fw->pinHandle(pin, "pinMode") < 0) return;
    fw->setPinMode(pin, mode);
}

static void n_digital_write(wasm_exec_env_t env, int32_t pin, int32_t level) {
    FirmwareRuntime* fw = runtimeOf(env);
    fw->pinBus()->writeLevel(fw->outHandle(pin, "digitalWrite"), level);
}

// Nothing drives a board input yet (sensors come with their device models):
// an undriven input reads its pull-up, or LOW.
static int32_t n_digital_read(wasm_exec_env_t env, int32_t pin) {
    FirmwareRuntime* fw = runtimeOf(env);
    const int h = fw->pinHandle(pin, "digitalRead");
    const PinState* s = fw->pinBus()->state(h);
    if (!s) return 0;
    if (s->drive == PinState::Drive::Level) return s->level;
    return fw->pinMode(pin) == 2 /* INPUT_PULLUP */ ? 1 : 0;
}

// 0 and 255 are steady levels; anything between is a pulse train at the
// pin's PWM frequency, which a servo on that pin reads as a pulse width (as a
// real one would). On a real board's pin without PWM the Arduino core falls
// back to digitalWrite (value < 128 -> LOW).
static void n_analog_write(wasm_exec_env_t env, int32_t pin, int32_t value) {
    FirmwareRuntime* fw = runtimeOf(env);
    const int h = fw->outHandle(pin, "analogWrite");
    if (!fw->profile().can(pin, "pwm")) {
        fw->warnOnce(QString("pwm %1").arg(pin), QString("analogWrite on pin %1: %2 has no PWM on that pin, so it acts as "
                                                         "digitalWrite (HIGH from 128)").arg(pin).arg(fw->profile().name));
        fw->pinBus()->writeLevel(h, value >= 128 ? 1 : 0);
        return;
    }
    fw->pinBus()->writeAnalog(h, value, fw->profile().pwmPeriodUs(pin));
}

// The board's ADC: counts of its resolution against its reference voltage
// (10-bit on 5 V on the ideal board and an Uno).
static int32_t n_analog_read(wasm_exec_env_t env, int32_t pin) {
    FirmwareRuntime* fw = runtimeOf(env);
    const int h = fw->pinHandle(pin, "analogRead");
    const BoardProfile& b = fw->profile();
    if (!b.can(pin, "analog")) {
        fw->warnOnce(QString("adc %1").arg(pin), QString("analogRead on pin %1: %2 has no ADC on that pin; it reads 0").arg(pin).arg(b.name));
        return 0;
    }
    const long full = (1L << std::clamp(b.adcBits, 1, 24)) - 1;
    const double v = fw->pinBus()->voltage(h);
    return int32_t(std::clamp(std::lround(v / b.adcVref * double(full)), 0L, full));
}

static void n_pwm_write_us(wasm_exec_env_t env, int32_t pin, int32_t us) {
    FirmwareRuntime* fw = runtimeOf(env);
    const int h = us <= 0 ? fw->pinHandle(pin, "Servo") : fw->outHandle(pin, "Servo");
    if (us <= 0) fw->pinBus()->release(h);
    else fw->pinBus()->writePulseUs(h, us);
}

static void n_pwm_write_ns(wasm_exec_env_t env, int32_t pin, int32_t ns) {
    FirmwareRuntime* fw = runtimeOf(env);
    const int h = ns <= 0 ? fw->pinHandle(pin, "Servo") : fw->outHandle(pin, "Servo");
    if (ns <= 0) fw->pinBus()->release(h);
    else fw->pinBus()->writePulseUs(h, ns / 1000.0);
}

// I2C (Wire): one call per transaction. Status as Wire's endTransmission().
// A NACKed address still costs the address byte.
static int32_t n_i2c_write(wasm_exec_env_t env, int32_t address, const uint8_t* data, uint32_t len, int32_t stop) {
    FirmwareRuntime* fw = runtimeOf(env);
    DeviceBus* bus = fw->deviceBus();
    const int32_t status = bus ? bus->i2cWrite(fw->busMaster(), address, data, int(len), stop != 0) : 2;
    fw->i2cBusTime(status == 2 ? 1 : 1 + int(len));
    return status;
}

static int32_t n_i2c_read(wasm_exec_env_t env, int32_t address, uint8_t* data, uint32_t len) {
    FirmwareRuntime* fw = runtimeOf(env);
    DeviceBus* bus = fw->deviceBus();
    const int32_t n = bus ? bus->i2cRead(fw->busMaster(), address, data, int(len)) : 0;
    fw->i2cBusTime(n > 0 ? 1 + int(len) : 1);
    return n;
}

static void n_i2c_set_clock(wasm_exec_env_t env, uint32_t hz) {
    runtimeOf(env)->setI2CClock(std::clamp(double(hz), 1000.0, 3.4e6));
}

// SPI: the bytes go to the parts whose chip-select pin is LOW. Nothing comes
// back yet (no SPI sensor parts), so MISO reads as 0.
static void n_spi_transfer(wasm_exec_env_t env, const uint8_t* data, uint32_t len) {
    FirmwareRuntime* fw = runtimeOf(env);
    if (DeviceBus* bus = fw->deviceBus()) bus->spiTransfer(fw->busMaster(), data, int(len));
    fw->spiBusTime(int(len));
}

static void n_spi_set_clock(wasm_exec_env_t env, uint32_t hz) {
    runtimeOf(env)->setSPIClock(std::max(1000.0, double(hz)));
}

// Camera (esp32-camera subset). Formats as in esp_camera.h: RGB565 0,
// GRAYSCALE 3, DEPTH16 9 (RoboticsStudio only). Errors as esp32-camera's.
static constexpr int32_t kPixRGB565 = 0, kPixGray = 3, kPixDepth16 = 9;
static constexpr int32_t kErrNotDetected = 0x20001, kErrFrameSize = 0x20002, kErrNotSupported = 0x20004;

static int32_t n_camera_init(wasm_exec_env_t env, int32_t sdaPin, int32_t format, int32_t width, int32_t height) {
    FirmwareRuntime* fw = runtimeOf(env);
    CameraDevice* cam = fw->deviceBus() ? fw->deviceBus()->camera(fw->pinHandle(sdaPin, "esp_camera_init")) : nullptr;
    if (!cam) return kErrNotDetected;
    if (width != cam->frameWidth() || height != cam->frameHeight()) return kErrFrameSize;
    const bool ok = cam->isDepth() ? format == kPixDepth16 : (format == kPixRGB565 || format == kPixGray);
    if (!ok) return kErrNotSupported;
    fw->cameraFormat()[sdaPin] = format;
    fw->cameraSeq()[sdaPin] = 0;
    return 0;
}

// Blocks in sim time until a frame newer than the last one handed out (the
// real driver waits for the next frame too); gives up after 2 s like its
// timeout. meta (6 x u32): width, height, format, seq, timestamp us lo/hi.
static int32_t n_camera_frame(wasm_exec_env_t env, int32_t sdaPin, uint8_t* buf, uint32_t cap, uint32_t metaOffset) {
    FirmwareRuntime* fw = runtimeOf(env);
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (!fw->cameraFormat().contains(sdaPin) || !wasm_runtime_validate_app_addr(inst, metaOffset, 24)) return 0;
    CameraDevice* cam = fw->deviceBus() ? fw->deviceBus()->camera(fw->pinHandle(sdaPin, "esp_camera_fb_get")) : nullptr;
    if (!cam) return 0;
    const int32_t format = fw->cameraFormat()[sdaPin];

    CameraFrame f;
    for (double waited = 0.0; ; waited += 0.001) {
        f = cam->latestFrame();
        if (f.pixels && f.seq != fw->cameraSeq()[sdaPin]) break;
        if (waited >= 2.0 || fw->clock().stopRequested()) return 0;
        fw->clock().wait(0.001);
    }
    const int n = f.width * f.height;
    const uint32_t bytes = uint32_t(format == kPixGray ? n : 2 * n);
    if (cap < bytes) return 0;
    fw->cameraSeq()[sdaPin] = f.seq;

    if (format == kPixDepth16) {
        const float* src = reinterpret_cast<const float*>(f.pixels->constData());
        for (int i = 0; i < n; ++i) {
            const uint16_t mm = uint16_t(std::clamp(std::lround(src[i] * 1000.0), 0L, 65535L));
            std::memcpy(buf + 2 * i, &mm, 2);   // little-endian, as the wasm side reads it
        }
    } else {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(f.pixels->constData());
        for (int i = 0; i < n; ++i) {
            const uint8_t r = src[3 * i], g = src[3 * i + 1], b = src[3 * i + 2];
            if (format == kPixGray) {
                buf[i] = uint8_t((77 * r + 150 * g + 29 * b) >> 8);
            } else {   // RGB565, high byte first (as the ESP32 driver delivers it)
                const uint16_t c = uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                buf[2 * i] = uint8_t(c >> 8);
                buf[2 * i + 1] = uint8_t(c);
            }
        }
    }
    const uint64_t us = uint64_t(f.time * 1e6);
    const uint32_t meta[6] = {uint32_t(f.width), uint32_t(f.height), uint32_t(format), uint32_t(f.seq),
                              uint32_t(us & 0xffffffffu), uint32_t(us >> 32)};
    std::memcpy(wasm_runtime_addr_app_to_native(inst, metaOffset), meta, sizeof meta);
    return int32_t(bytes);
}

// Serial ports: 0..2 = Serial, Serial1, Serial2; 16 and up = SoftwareSerial.
static void n_serial_write(wasm_exec_env_t env, const char* data, uint32_t len) {   // older cores: Serial
    runtimeOf(env)->uartWrite(0, reinterpret_cast<const uint8_t*>(data), int(len));
}
static int32_t n_uart_begin(wasm_exec_env_t env, int32_t port, uint32_t baud, int32_t rx, int32_t tx) {
    return runtimeOf(env)->uartBegin(port, double(baud), rx, tx) ? 0 : -1;
}
static void n_uart_end(wasm_exec_env_t env, int32_t port) { runtimeOf(env)->uartEnd(port); }
static int32_t n_uart_write(wasm_exec_env_t env, int32_t port, const uint8_t* data, uint32_t len) {
    return runtimeOf(env)->uartWrite(port, data, int(len));
}
static int32_t n_uart_available(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->uartAvailable(port); }
static int32_t n_uart_read(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->uartRead(port, false); }
static int32_t n_uart_peek(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->uartRead(port, true); }
static int32_t n_uart_available_for_write(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->uartAvailableForWrite(port); }
static void n_uart_flush(wasm_exec_env_t env, int32_t port) { runtimeOf(env)->uartFlush(port); }
static int32_t n_uart_wait(wasm_exec_env_t env, int32_t port, uint32_t timeoutUs) {
    return runtimeOf(env)->uartWait(port, timeoutUs / 1e6) ? 1 : 0;
}
static int32_t n_soft_serial_open(wasm_exec_env_t env, int32_t rx, int32_t tx) { return runtimeOf(env)->softOpen(rx, tx); }
static int32_t n_soft_serial_listen(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->softListen(port) ? 1 : 0; }
static int32_t n_soft_serial_overflow(wasm_exec_env_t env, int32_t port) { return runtimeOf(env)->softOverflow(port) ? 1 : 0; }

static NativeSymbol rsNatives[] = {
    {"millis", (void*)n_millis, "()i", nullptr},
    {"micros", (void*)n_micros, "()i", nullptr},
    {"delay_us", (void*)n_delay_us, "(i)", nullptr},
    {"yield", (void*)n_yield, "()", nullptr},
    {"pin_mode", (void*)n_pin_mode, "(ii)", nullptr},
    {"digital_write", (void*)n_digital_write, "(ii)", nullptr},
    {"digital_read", (void*)n_digital_read, "(i)i", nullptr},
    {"analog_write", (void*)n_analog_write, "(ii)", nullptr},
    {"analog_read", (void*)n_analog_read, "(i)i", nullptr},
    {"pwm_write_us", (void*)n_pwm_write_us, "(ii)", nullptr},
    {"pwm_write_ns", (void*)n_pwm_write_ns, "(ii)", nullptr},
    {"i2c_set_clock", (void*)n_i2c_set_clock, "(i)", nullptr},
    {"spi_set_clock", (void*)n_spi_set_clock, "(i)", nullptr},
    {"serial_write", (void*)n_serial_write, "(*~)", nullptr},
    {"uart_begin", (void*)n_uart_begin, "(iiii)i", nullptr},
    {"uart_end", (void*)n_uart_end, "(i)", nullptr},
    {"uart_write", (void*)n_uart_write, "(i*~)i", nullptr},
    {"uart_available", (void*)n_uart_available, "(i)i", nullptr},
    {"uart_read", (void*)n_uart_read, "(i)i", nullptr},
    {"uart_peek", (void*)n_uart_peek, "(i)i", nullptr},
    {"uart_available_for_write", (void*)n_uart_available_for_write, "(i)i", nullptr},
    {"uart_flush", (void*)n_uart_flush, "(i)", nullptr},
    {"uart_wait", (void*)n_uart_wait, "(ii)i", nullptr},
    {"soft_serial_open", (void*)n_soft_serial_open, "(ii)i", nullptr},
    {"soft_serial_listen", (void*)n_soft_serial_listen, "(i)i", nullptr},
    {"soft_serial_overflow", (void*)n_soft_serial_overflow, "(i)i", nullptr},
    {"i2c_write", (void*)n_i2c_write, "(i*~i)i", nullptr},
    {"i2c_read", (void*)n_i2c_read, "(i*~)i", nullptr},
    {"spi_transfer", (void*)n_spi_transfer, "(*~)", nullptr},
    {"camera_init", (void*)n_camera_init, "(iiii)i", nullptr},
    {"camera_frame", (void*)n_camera_frame, "(i*~i)i", nullptr},
};


#pragma region WASI natives

// The few WASI calls wasi-libc makes. Pointers arrive as app offsets ('i')
// because iovecs hold further offsets that need checking one by one.

static int32_t n_fd_write(wasm_exec_env_t env, int32_t fd, uint32_t iovs, uint32_t iovsLen, uint32_t nwritten) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (fd != 1 && fd != 2) return kWasiEBADF;
    if (!wasm_runtime_validate_app_addr(inst, iovs, uint64_t(iovsLen) * 8)
        || !wasm_runtime_validate_app_addr(inst, nwritten, 4)) return kWasiEINVAL;

    const uint32_t* vec = static_cast<const uint32_t*>(wasm_runtime_addr_app_to_native(inst, iovs));
    uint32_t total = 0;
    for (uint32_t i = 0; i < iovsLen; ++i) {
        const uint32_t buf = vec[2 * i], len = vec[2 * i + 1];
        if (!len) continue;
        if (!wasm_runtime_validate_app_addr(inst, buf, len)) return kWasiEINVAL;
        runtimeOf(env)->appendText(fd == 1 ? FirmwareRuntime::StdoutStream : FirmwareRuntime::StderrStream,
                                   static_cast<const char*>(wasm_runtime_addr_app_to_native(inst, buf)), len);
        total += len;
    }
    *static_cast<uint32_t*>(wasm_runtime_addr_app_to_native(inst, nwritten)) = total;
    return kWasiSuccess;
}

// isatty() for stdout/stderr: a character device, so printf flushes per line.
static int32_t n_fd_fdstat_get(wasm_exec_env_t env, int32_t fd, uint32_t statOut) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (fd < 0 || fd > 2) return kWasiEBADF;
    if (!wasm_runtime_validate_app_addr(inst, statOut, 24)) return kWasiEINVAL;
    uint8_t* stat = static_cast<uint8_t*>(wasm_runtime_addr_app_to_native(inst, statOut));
    std::memset(stat, 0, 24);   // fdstat: filetype u8, flags u16, rights u64 x2
    stat[0] = 2;                // __WASI_FILETYPE_CHARACTER_DEVICE
    const uint64_t rights = ~uint64_t(0);
    std::memcpy(stat + 8, &rights, 8);
    std::memcpy(stat + 16, &rights, 8);
    return kWasiSuccess;
}

static int32_t n_fd_seek(wasm_exec_env_t, int32_t, int64_t, int32_t, uint32_t) { return kWasiESPIPE; }
static int32_t n_fd_close(wasm_exec_env_t, int32_t) { return kWasiSuccess; }

// Every clock is sim time, so time()/clock() repeat run to run.
static int32_t n_clock_time_get(wasm_exec_env_t env, int32_t, int64_t, uint32_t timeOut) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (!wasm_runtime_validate_app_addr(inst, timeOut, 8)) return kWasiEINVAL;
    const uint64_t ns = uint64_t(runtimeOf(env)->clock().now() * 1e9);
    std::memcpy(wasm_runtime_addr_app_to_native(inst, timeOut), &ns, sizeof ns);
    return kWasiSuccess;
}

static void n_proc_exit(wasm_exec_env_t env, int32_t code) {
    wasm_runtime_set_exception(wasm_runtime_get_module_inst(env),
                               QString("firmware called exit(%1)").arg(code).toUtf8().constData());
}

static NativeSymbol wasiNatives[] = {
    {"fd_write", (void*)n_fd_write, "(iiii)i", nullptr},
    {"fd_fdstat_get", (void*)n_fd_fdstat_get, "(ii)i", nullptr},
    {"fd_seek", (void*)n_fd_seek, "(iIii)i", nullptr},
    {"fd_close", (void*)n_fd_close, "(i)i", nullptr},
    {"clock_time_get", (void*)n_clock_time_get, "(iIi)i", nullptr},
    {"proc_exit", (void*)n_proc_exit, "(i)", nullptr},
};


#pragma region runtime

// One WAMR runtime per process, with the natives registered once.
static bool ensureWamr() {
    static bool ok = false;
    static std::once_flag once;
    std::call_once(once, [] {
        RuntimeInitArgs init;
        std::memset(&init, 0, sizeof init);
        init.mem_alloc_type = Alloc_With_System_Allocator;
        ok = wasm_runtime_full_init(&init)
             && wasm_runtime_register_natives("rs", rsNatives, sizeof rsNatives / sizeof rsNatives[0])
             && wasm_runtime_register_natives("wasi_snapshot_preview1", wasiNatives, sizeof wasiNatives / sizeof wasiNatives[0]);
        wasm_runtime_set_log_level(WASM_LOG_LEVEL_ERROR);
        if (!ok) Log::error("Firmware: could not start the WebAssembly runtime");
    });
    return ok;
}


FirmwareRuntime::FirmwareRuntime(const QString& tag)
    : RobotController(tag, [this] { if (instance) wasm_runtime_terminate(instance); }),
      tag(tag) {}


FirmwareRuntime::~FirmwareRuntime() {
    stop();
    gate.waitUntilOutOfCall(LockstepGate::kWatchdog);
    unload();
}


void FirmwareRuntime::unload() {
    if (execEnv) wasm_runtime_destroy_exec_env(execEnv);
    if (instance) wasm_runtime_deinstantiate(instance);
    if (module) wasm_runtime_unload(module);
    execEnv = nullptr;
    instance = nullptr;
    module = nullptr;
    binary.clear();
}


BoardProfile BoardProfile::fromBoard(const RealBoard& rb) {
    BoardProfile p;
    ComponentInstance* comp = rb.instance();
    if (!comp || !comp->getBlueprint()) return p;
    const BoardDef& def = rb.def();
    p.ideal = false;
    p.uid = comp->uid;
    p.name = comp->name;
    p.defaultPwmHz = def.analogWriteHz;
    p.adcBits = def.adcBits;
    p.adcVref = def.adcVref;
    p.i2cDefaultHz = def.i2cDefaultHz;
    p.spiMaxHz = def.spiMaxHz;
    for (const PinDef& pin : comp->getBlueprint()->pins) {
        if (pin.number < 0) continue;
        QStringList caps = pin.caps;
        if (pin.internal) caps = QStringList{"digital", pin.type};   // hard-wired to an on-board part
        p.caps.insert(pin.number, caps);
        if (def.analogWriteHzByPin.contains(pin.id)) p.pwmHz.insert(pin.number, def.analogWriteHzByPin.value(pin.id));
    }
    auto numberOf = [&](const QString& id) {
        for (const PinDef& pin : comp->getBlueprint()->pins) if (pin.id == id) return pin.number;
        return -1;
    };
    p.uarts.clear();
    for (const UartPortDef& u : def.uarts) p.uarts.append({u.name, numberOf(u.rx), numberOf(u.tx), u.usb, u.matrix});
    p.uartRxBuffer = def.uartRxBuffer;
    p.uartTxBuffer = def.uartTxBuffer;
    p.softSerialMaxRxBaud = def.softSerialMaxRxBaud;
    return p;
}



void FirmwareRuntime::warnOnce(const QString& key, const QString& message) {
    if (reported.contains(key)) return;
    reported.insert(key);
    Log::warning(tag + ": " + message);
}



void FirmwareRuntime::i2cBusTime(int bytes) {
    if (board.ideal || bytes <= 0) return;
    gate.wait((bytes * 9 + 2) / i2cHz);   // 8 data bits + ACK per byte, plus start and stop
}



void FirmwareRuntime::spiBusTime(int bytes) {
    if (board.ideal || bytes <= 0) return;
    gate.wait(bytes * 8 / std::min(spiHz, board.spiMaxHz));
}



bool FirmwareRuntime::load(const QByteArray& wasm, PinBus* pinBus, DeviceBus* deviceBus, const BoardProfile& profile,
                           SerialHub* serialHub) {
    stop();
    if (!gate.waitUntilOutOfCall(LockstepGate::kWatchdog)) {
        Log::error(tag + ": previous firmware run did not stop; not loading");
        return false;
    }
    flushText();
    unload();
    held = false;
    gate.reset();
    pins = pinBus;
    devices = deviceBus;
    hub = serialHub;
    master = deviceBus ? deviceBus->masterOf(profile.uid) : -1;
    tag = profile.ideal ? QString("MCU") : profile.name;
    gate.setTag(tag);
    pinModes.clear();
    cameraFormats.clear();
    cameraLastSeq.clear();
    reportedPins.clear();
    reported.clear();
    board = profile;
    resetPorts();
    i2cHz = profile.i2cDefaultHz;
    spiHz = 4000000.0;   // SPI's default before any beginTransaction (an Uno's clock / 4)
    setupDone = false;

    if (!ensureWamr()) return false;
    if (wasm.isEmpty()) {
        Log::error(tag + ": no compiled firmware to run");
        return false;
    }

    char err[256] = {};
    binary.assign(wasm.begin(), wasm.end());
    module = wasm_runtime_load(binary.data(), uint32_t(binary.size()), err, sizeof err);
    if (!module) {
        Log::error(tag + ": firmware can't be loaded: " + QString::fromUtf8(err));
        unload();
        return false;
    }

    // Anything the firmware imports must be served here; say what isn't.
    QStringList missing;
    for (int i = 0; i < wasm_runtime_get_import_count(module); ++i) {
        wasm_import_t imp;
        wasm_runtime_get_import_type(module, i, &imp);
        if (imp.kind == WASM_IMPORT_EXPORT_KIND_FUNC && !imp.linked)
            missing << QString("%1.%2").arg(imp.module_name, imp.name);
    }
    if (!missing.isEmpty()) {
        Log::error(tag + ": the firmware uses functions the simulator doesn't provide: " + missing.join(", "));
        unload();
        return false;
    }

    instance = wasm_runtime_instantiate(module, kWasmStack, 0, err, sizeof err);
    if (instance) execEnv = wasm_runtime_create_exec_env(instance, kExecEnvStack);
    if (!instance || !execEnv) {
        Log::error(tag + ": firmware can't be started: " + QString::fromUtf8(err));
        unload();
        return false;
    }
    wasm_runtime_set_user_data(execEnv, this);

    gate.start();
    return true;
}


void FirmwareRuntime::stop() {
    gate.stop();
    // After a stop every native returns at once, so the firmware normally
    // leaves its call within moments. Only code that calls none (a busy loop)
    // is terminated from here, as a last resort: terminating from another
    // thread makes WAMR walk the running thread's frames (call-stack dump),
    // which races with that thread and crashed a Stop during normal running.
    if (instance && !gate.waitUntilOutOfCall(std::chrono::milliseconds(100))) wasm_runtime_terminate(instance);
}


bool FirmwareRuntime::holdInReset() {
    if (!instance || held) return false;
    held = true;
    stop();
    return true;
}


bool FirmwareRuntime::restart(double simTime) {
    if (!held || !module) return false;
    if (!gate.waitUntilOutOfCall(LockstepGate::kWatchdog)) {
        Log::error(tag + ": the firmware did not stop for the reset; it stays off");
        return false;
    }
    // A fresh instance of the same module: the MCU's RAM starts over.
    if (execEnv) wasm_runtime_destroy_exec_env(execEnv);
    if (instance) wasm_runtime_deinstantiate(instance);
    execEnv = nullptr;
    char err[256] = {};
    instance = wasm_runtime_instantiate(module, kWasmStack, 0, err, sizeof err);
    if (instance) execEnv = wasm_runtime_create_exec_env(instance, kExecEnvStack);
    if (!instance || !execEnv) {
        Log::error(tag + ": firmware can't be restarted: " + QString::fromUtf8(err));
        return false;
    }
    wasm_runtime_set_user_data(execEnv, this);
    pinModes.clear();
    cameraFormats.clear();
    cameraLastSeq.clear();
    resetPorts();
    i2cHz = board.i2cDefaultHz;
    spiHz = 4000000.0;
    setupDone = false;
    held = false;
    gate.restartAt(simTime);
    return true;
}


// "#01: 0x0123 - _Z4loopv" -> "#01: 0x0123 - loop()"
static QString demangleFrame(const QString& line) {
    const int at = line.indexOf(" - ");
    if (at < 0) return line;
    const QByteArray symbol = line.mid(at + 3).trimmed().toUtf8();
    int status = 0;
    char* plain = abi::__cxa_demangle(symbol.constData(), nullptr, nullptr, &status);
    if (status != 0 || !plain) return line;
    QString out = line.left(at + 3) + QString::fromUtf8(plain);
    free(plain);
    return out;
}


bool FirmwareRuntime::call(const char* function) {
    wasm_function_inst_t fn = wasm_runtime_lookup_function(instance, function);
    if (!fn) {
        if (std::strcmp(function, "_initialize") == 0) return true;   // nothing to construct
        Log::error(tag + ": the firmware has no " + function + " (define setup() and loop())");
        return false;
    }
    if (wasm_runtime_call_wasm(execEnv, fn, 0, nullptr)) return true;
    if (gate.stopRequested() || gate.watchdogFired()) return false;   // our own terminate

    const QString where = std::strcmp(function, "rs_loop") == 0 ? "loop()"
                        : std::strcmp(function, "rs_setup") == 0 ? "setup()" : "startup";
    Log::error(tag + ": firmware stopped in " + where + ": " + QString::fromUtf8(wasm_runtime_get_exception(instance)));
    char stack[2048] = {};
    if (wasm_runtime_dump_call_stack_to_buf(execEnv, stack, sizeof stack) > 0) {
        for (const QString& line : QString::fromUtf8(stack).split('\n', Qt::SkipEmptyParts))
            Log::error(tag + ":   " + demangleFrame(line));
    }
    return false;
}


/*
 * One call of the firmware once released: _initialize + setup() the first
 * time, loop() after that. Called continuously by the controller thread.
 */
bool FirmwareRuntime::runCycle() {
    if (!gate.beginCall()) return false;

    // WAMR needs its signal/stack setup on every thread that calls into
    // wasm, and torn down before that thread ends (a stale one crashes the
    // next thread's setup).
    struct ThreadEnv {
        bool ready = false;
        ~ThreadEnv() { if (ready) wasm_runtime_destroy_thread_env(); }
    };
    static thread_local ThreadEnv threadEnv;
    if (!threadEnv.ready) threadEnv.ready = wasm_runtime_init_thread_env();

    const bool isSetup = !setupDone;
    setupDone = true;
    const bool ok = isSetup ? (call("_initialize") && call("rs_setup")) : call("rs_loop");

    gate.endCall();

    // stop() and the watchdog terminate the module; both already reported
    // themselves and left the gate idle.
    if (gate.stopRequested() || gate.watchdogFired()) {
        flushText();
        return true;
    }
    if (!ok) {
        flushText();
        gate.setIdle();
        return true;
    }
    if (!isSetup) gate.charge(LockstepGate::kLoopCost);
    return true;
}


int FirmwareRuntime::pinHandle(int pin, const char* what) {
    const int h = pins ? pins->handleForNumber(board.uid, pin) : -1;
    if (h < 0 && !reportedPins.contains(pin)) {
        reportedPins.insert(pin);
        if (pin < 0)
            Log::warning(QString("%1: %2 on pin %3: a pin that isn't set yet (in a Real copy, put the board pin "
                                 "into robot_pins.h once the part is wired)").arg(tag, what).arg(pin));
        else if (board.ideal)
            Log::warning(QString("%1: %2 on pin %3: no part has that pin (use the names in robot_pins.h)").arg(tag, what).arg(pin));
        else
            Log::warning(QString("%1: %2 on pin %3: %4 has no pin %3 (its pins are in the Inspector and on the Wiring page)")
                             .arg(tag, what).arg(pin).arg(board.name));
    }
    return h;
}


void FirmwareRuntime::appendText(int stream, const char* data, uint32_t len) {
    QByteArray& line = stream == SerialStream ? serialLine : stream == StdoutStream ? stdoutLine : stderrLine;
    const QString prefix = stream == SerialStream ? QString("[%1]: ").arg(tag)
                         : stream == StdoutStream ? QString("[%1][stdout]: ").arg(tag)
                                                  : QString("[%1][stderr]: ").arg(tag);
    for (uint32_t i = 0; i < len; ++i) {
        const char c = data[i];
        if (c == '\r') continue;
        if (c == '\n' || line.size() >= kMaxLine) {
            Log::info(prefix + QString::fromUtf8(line));
            line.clear();
            if (c == '\n') continue;
        }
        line.append(c);
    }
}


// Whatever is left without a newline (run stopped or crashed).
void FirmwareRuntime::flushText() {
    if (!serialLine.isEmpty()) appendText(SerialStream, "\n", 1);
    if (!stdoutLine.isEmpty()) appendText(StdoutStream, "\n", 1);
    if (!stderrLine.isEmpty()) appendText(StderrStream, "\n", 1);
}


int FirmwareRuntime::outHandle(int pin, const char* what) {
    const int h = pinHandle(pin, what);
    if (h >= 0 && pins && !pins->claim(h, board.uid)) {
        const int other = pins->writer(h);
        warnOnce(QString("drive %1").arg(pin),
                 QString("%1 on pin %2: %3 drives this net too. Two outputs on one wire fight (a short when they differ)")
                     .arg(what).arg(pin).arg(board.peers.value(other, "another board")));
    }
    return h;
}



#pragma region serial ports

// A receiver more than 2.5 % off the sender's rate samples the wrong bits.
static bool baudMatches(double sender, double receiver) {
    return sender <= 0.0 || receiver <= 0.0 || std::fabs(sender / receiver - 1.0) <= 0.025;
}

// What a receiver at the wrong rate reads instead of a byte: repeatable noise.
static uint8_t garbled(const UartByte& b) {
    uint64_t x = uint64_t(std::llround(b.arrival * 1e9)) ^ (uint64_t(b.value) << 40) ^ (uint64_t(uint32_t(b.sender)) << 48);
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return uint8_t(x);
}



void FirmwareRuntime::resetPorts() {
    for (const Port& p : ports)
        if (pins && p.reader >= 0) pins->uartClose(p.rxH, p.reader);
    ports.clear();
    listening = -1;
    nextSoft = 16;
    for (int i = 0; i < board.uarts.size() && i < 3; ++i) {
        const BoardProfile::Uart& u = board.uarts[i];
        Port p;
        p.name = u.name;
        p.usb = u.usb;
        p.matrix = u.matrix;
        p.fixedRx = u.rx;
        p.fixedTx = u.tx;
        const int id = u.name == "Serial" ? 0 : u.name == "Serial1" ? 1 : u.name == "Serial2" ? 2 : -1;
        if (id >= 0) ports.insert(id, p);
    }
}



FirmwareRuntime::Port* FirmwareRuntime::port(int id, const char* what) {
    auto it = ports.find(id);
    if (it != ports.end()) return &it.value();
    const QString name = id >= 16 ? QString("SoftwareSerial") : id == 0 ? QString("Serial") : QString("Serial%1").arg(id);
    warnOnce(QString("port %1").arg(id), QString("%1: %2 has no %3").arg(what, board.name, name));
    return nullptr;
}



bool FirmwareRuntime::uartBegin(int id, double baud, int rxPin, int txPin) {
    Port* p = port(id, "begin()");
    if (!p) return false;
    if (baud < 50.0) baud = 50.0;
    if (p->begun) uartEnd(id);
    if (!p->soft) {
        if (p->matrix) {
            // The ESP32 core's defaults when begin() names no pins.
            if (rxPin < 0) rxPin = id == 1 ? 9 : 16;
            if (txPin < 0) txPin = id == 1 ? 10 : 17;
        } else {
            rxPin = p->fixedRx;
            txPin = p->fixedTx;
        }
        p->rxH = rxPin >= 0 ? pinHandle(rxPin, qPrintable(p->name + ".begin() RX")) : -1;
        p->txH = txPin >= 0 ? pinHandle(txPin, qPrintable(p->name + ".begin() TX")) : -1;
    }
    p->begun = true;
    p->baud = baud;
    p->txEnd = gate.simTime();
    p->rx.clear();
    p->overflow = false;
    if (!pins) return true;
    if (p->txH >= 0) {
        if (!pins->claim(p->txH, board.uid))
            warnOnce(QString("tx %1").arg(id), QString("%1's TX: %2 drives that wire too (TX wired to TX?)")
                                                   .arg(p->name, board.peers.value(pins->writer(p->txH), "another board")));
        pins->writeLevel(p->txH, 1);   // a UART line idles HIGH
    }
    if (p->rxH >= 0) p->reader = pins->uartOpen(p->rxH, gate.simTime());
    for (int h : {p->txH, p->rxH}) {
        const PinState* s = pins->state(h);
        if (s && s->maxBitrate > 0.0 && baud > s->maxBitrate)
            warnOnce(QString("shifter %1").arg(id), QString("%1 at %2 baud goes through a level shifter rated for %3 bit/s: "
                                                            "on the bench the edges would be too slow").arg(p->name).arg(baud).arg(s->maxBitrate));
    }
    if (p->soft && board.softSerialMaxRxBaud > 0.0 && baud > board.softSerialMaxRxBaud)
        warnOnce(QString("softbaud %1").arg(id), QString("SoftwareSerial at %1 baud: reception is unreliable above %2 baud on a real %3 "
                                                         "(not simulated: here it works)").arg(baud).arg(board.softSerialMaxRxBaud).arg(board.name));
    if (p->soft) softListen(id);   // as the AVR library's begin() does
    return true;
}



void FirmwareRuntime::uartEnd(int id) {
    auto it = ports.find(id);
    if (it == ports.end() || !it->begun) return;
    if (pins && it->reader >= 0) pins->uartClose(it->rxH, it->reader);
    it->reader = -1;
    it->begun = false;
    it->rx.clear();
}



int FirmwareRuntime::uartWrite(int id, const uint8_t* data, int len) {
    Port* p = port(id, "write()");
    if (!p || len <= 0) return 0;
    if (board.ideal) {   // instant, and Serial works without begin()
        if (p->usb) {
            appendText(SerialStream, reinterpret_cast<const char*>(data), uint32_t(len));
            if (hub) hub->output(board.uid, gate.simTime(), 0.0, reinterpret_cast<const char*>(data), len);
        }
        return len;
    }
    if (!p->begun) {
        warnOnce(QString("nobegin %1").arg(id), QString("%1.print() before %1.begin(): a real board sends nothing").arg(p->name));
        return 0;
    }
    const double byteTime = 10.0 / p->baud;   // start bit, 8 data bits, stop bit
    // The core's TX buffer plus the byte in the shift register; SoftwareSerial
    // has none: it sends each byte bit by bit before returning.
    const double backlog = p->soft ? 0.0 : board.uartTxBuffer * byteTime;
    for (int i = 0; i < len; ++i) {
        if (gate.stopRequested()) return i;
        double now = gate.simTime();
        if (p->txEnd - now > backlog + 1e-12) {
            gate.wait(p->txEnd - now - backlog);
            now = gate.simTime();
        }
        UartByte b;
        b.start = std::max(now, p->txEnd);
        b.arrival = b.start + byteTime;
        b.baud = p->baud;
        b.sender = board.uid;
        b.value = data[i];
        p->txEnd = b.arrival;
        if (pins && p->txH >= 0) pins->uartSend(p->txH, b);
        if (p->usb) {
            appendText(SerialStream, reinterpret_cast<const char*>(data + i), 1);
            if (hub) hub->output(board.uid, b.start, p->baud, reinterpret_cast<const char*>(data + i), 1);
        }
        if (p->soft) gate.wait(byteTime);
    }
    return len;
}



void FirmwareRuntime::deliver(Port& p, const UartByte& b) {
    UartByte in = b;
    if (!board.ideal && !baudMatches(b.baud, p.baud)) {
        in.value = garbled(b);
        warnOnce(QString("baud %1 %2").arg(p.name).arg(b.sender),
                 QString("%1 receives at %2 baud, the sender (%3) sends at %4 baud: it reads garbage")
                     .arg(p.name).arg(p.baud).arg(b.sender == -3 ? QString("the Serial Monitor") : board.peers.value(b.sender, board.name))
                     .arg(b.baud));
    }
    const int cap = p.soft ? 64 : board.uartRxBuffer;
    if (int(p.rx.size()) >= cap) {
        p.overflow = true;
        warnOnce(QString("overflow %1").arg(p.name), QString("%1's receive buffer (%2 bytes) overflowed: bytes were lost. Read it more often")
                                                         .arg(p.soft ? "SoftwareSerial" : p.name).arg(cap));
        return;
    }
    p.rx.push_back(in.value);
}



// Moves the bytes that have arrived by now into the port's buffer.
void FirmwareRuntime::pull(int id, Port& p) {
    const double now = gate.simTime();
    UartByte b;
    const bool listens = !p.soft || listening == id;
    if (pins && p.reader >= 0) {
        while (pins->uartTake(p.rxH, p.reader, now, &b)) if (listens && p.begun) deliver(p, b);
    }
    if (!p.usb || !hub) return;
    // The USB chip drives RX through a resistor: another driver on the wire wins.
    bool external = false;
    if (pins && p.rxH >= 0) {
        for (int sender : pins->uartSenders(p.rxH)) external = external || sender != board.uid;
        const int w = pins->writer(p.rxH);
        external = external || (w != -2 && w != board.uid);
    }
    while (hub->takeInput(board.uid, now, &b)) {
        if (external) {
            warnOnce("usb rx", QString("the Serial Monitor's input doesn't reach %1: %2's RX pin is driven by another part "
                                       "(on a real board the external driver wins over the USB chip)").arg(board.name, p.name));
            continue;
        }
        if (!board.ideal && !p.begun) continue;
        deliver(p, b);
    }
}



int FirmwareRuntime::uartAvailable(int id) {
    Port* p = port(id, "available()");
    if (!p) return 0;
    pull(id, *p);
    return int(p->rx.size());
}



int FirmwareRuntime::uartRead(int id, bool peek) {
    Port* p = port(id, "read()");
    if (!p) return -1;
    pull(id, *p);
    if (p->rx.empty()) return -1;
    const int c = p->rx.front();
    if (!peek) p->rx.pop_front();
    return c;
}



int FirmwareRuntime::uartAvailableForWrite(int id) {
    Port* p = port(id, "availableForWrite()");
    if (!p) return 0;
    if (board.ideal || !p->begun || p->soft) return board.uartTxBuffer;
    const double queued = std::max(0.0, p->txEnd - gate.simTime()) * p->baud / 10.0;
    return std::max(0, board.uartTxBuffer - int(std::ceil(queued - 1e-9)));
}



void FirmwareRuntime::uartFlush(int id) {
    Port* p = port(id, "flush()");
    if (!p || board.ideal || !p->begun) return;
    const double left = p->txEnd - gate.simTime();
    if (left > 0.0) gate.wait(left);
}



bool FirmwareRuntime::uartWait(int id, double timeout) {
    Port* p = port(id, "read()");
    if (!p) return false;
    const double deadline = gate.simTime() + std::max(0.0, timeout);
    for (;;) {
        pull(id, *p);
        if (!p->rx.empty()) return true;
        const double left = deadline - gate.simTime();
        if (left <= 0.0 || gate.stopRequested()) return false;
        gate.wait(std::min(left, 50e-6));   // a byte at 115200 baud takes 87 us
    }
}



int FirmwareRuntime::softOpen(int rxPin, int txPin) {
    const int id = nextSoft++;
    Port p;
    p.name = "SoftwareSerial";
    p.soft = true;
    p.rxH = pinHandle(rxPin, "SoftwareSerial RX");
    p.txH = pinHandle(txPin, "SoftwareSerial TX");
    ports.insert(id, p);
    return id;
}



bool FirmwareRuntime::softListen(int id) {
    if (id < 0) {   // stopListening()
        const bool was = listening >= 0;
        listening = -1;
        return was;
    }
    auto it = ports.find(id);
    if (it == ports.end() || !it->soft || listening == id) return false;
    listening = id;
    // The library empties the new port's buffer; what came meanwhile is gone.
    UartByte b;
    if (pins && it->reader >= 0) while (pins->uartTake(it->rxH, it->reader, gate.simTime(), &b)) {}
    it->rx.clear();
    return true;
}



bool FirmwareRuntime::softOverflow(int id) {
    auto it = ports.find(id);
    if (it == ports.end()) return false;
    const bool o = it->overflow;
    it->overflow = false;
    return o;
}
