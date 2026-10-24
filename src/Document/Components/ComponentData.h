#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include "Utils/Graph.h"
#include "DeviceTypes.h"




struct MechanicDef {
    QList<float> snapAngles;
};

struct MetaDef {
    // QString id;
    QString name;
    QString version;
    QString author;
    QString iconPath;
    QString hintClass;   // one of HintClass::all(), empty = generic
};

struct PinDef {
    QString id;
    QString description;
    QPair<float, float> voltageRange;
    // Electrical role. Part pins: "pwm", "digital", "analog", the bus lines
    // "i2c_sda", "i2c_scl", "spi_sck", "spi_mosi", "spi_miso", "uart_tx",
    // "uart_rx", "power" (a load), "ground". Board pins: "gpio" (what it can
    // do is in `caps`), "power_out" (a source: the board's 5V/3V3 regulator).
    // Empty = not typed yet.
    QString type;
    int number = -1;        // the number a sketch uses (board gpio pins and internal pins)
    QStringList caps;       // gpio: digital, pwm, analog, interrupt, i2c_sda, ...
    bool internal = false;  // hard-wired on the board's PCB (ESP32-CAM camera): can't be wired

    static const QStringList& types();
    static const QStringList& capabilities();
    // A part's signal or bus pin: the ideal board gives it a number, and at
    // the Real level it is wired to a board gpio pin.
    // motor = a motor's terminal (it gets an ideal-board pin: the ideal driver);
    // motor_out = a motor driver's output, driven by its own part, never a board pin.
    bool isBoardPin() const { return !type.isEmpty() && !isPower() && type != "ground" && type != "gpio" && type != "motor_out"; }
    bool isPower() const { return type == "power" || type == "power_out"; }
    bool hasCap(const QString& cap) const { return caps.contains(cap); }
};

// What a board (a component with a "board" block) is: what its firmware is
// compiled for and the limits of its pins. Only on board parts.
// One serial port of a board ("uart" in the board block). Fixed ports name
// their pins; a matrix port (ESP32 Serial1/Serial2) takes any gpio in begin().
struct UartPortDef {
    QString name;              // the core's object: Serial, Serial1, ...
    QString rx, tx;            // pin ids; empty on a matrix port
    bool usb = false;          // also wired to the board's USB-serial chip (the Serial Monitor)
    bool matrix = false;
};

struct BoardDef {
    bool present = false;
    QString fqbn;              // arduino-cli board, e.g. arduino:avr:uno
    QString headerFolder;      // unused since 2026-10-21 (the folder of a generated robot_pins.h); kept on save
    // The real Arduino core's variant for this board (its pins_arduino.h):
    // the pin names a sketch uses (A0, LED_BUILTIN, SDA, ...) and the
    // board's macros (ARDUINO_AVR_UNO), so the sketch compiles here as there.
    QMap<QString, int> variantPins;
    QStringList defines;
    double logicVoltage = 5.0;
    int adcBits = 10;
    double adcVref = 5.0;
    double analogWriteHz = 490.0;
    QMap<QString, double> analogWriteHzByPin;   // pin id -> frequency
    QString i2cSda, i2cScl;    // pin ids of the hardware I2C bus
    double i2cDefaultHz = 100000.0;
    QString spiSck, spiMosi, spiMiso;
    double spiMaxHz = 8000000.0;
    // A gpio pin as a source: its output resistance (Ω, from the VOH/VOL vs
    // current curves) and its absolute maximum current (A). 0 = not modelled.
    double pinOutputOhm = 0.0;
    double pinAbsMaxA = 0.0;
    // Serial ports, their buffers (bytes) and SoftwareSerial's limit (0 = the
    // core has no SoftwareSerial; above it, reception is unreliable).
    QList<UartPortDef> uarts;
    int uartRxBuffer = 64, uartTxBuffer = 64;
    double softSerialMaxRxBaud = 0.0;
    const UartPortDef* uart(const QString& name) const {
        for (const UartPortDef& u : uarts) if (u.name == name) return &u;
        return nullptr;
    }
    QMap<QString, QString> sources;   // field -> datasheet / estimate
};

// The part's electrical side at the Real level ("power" block, any part): what
// it supplies, what it converts, what it draws. Numbers in SI units (V, A, Ω,
// s); the JSON keys say their unit (current_ma, restart_ms, ...).
struct SupplyDef {             // a source on a power_out pin
    QString pin;
    QString kind;              // usb (a board's default feed), battery, supply (bench supply)
    double voltage = 0.0;      // open circuit (battery: from the discharge curve)
    double rInternal = 0.0;    // Ω
    double currentLimit = 0.0; // A; supply: constant-current limit; 0 = none
    double holdCurrent = 0.0;  // A; usb: the polyfuse's hold current (warnings); 0 = none

    static const QStringList& kinds();
};

struct RegulatorDef {          // in (a power or power_out pin) -> out (a power_out pin)
    QString id;
    QString in, out;
    QString kind = "linear";   // linear, buck
    double vOut = 0.0;
    double dropout = 0.0;      // linear: V below the input; buck: minimum headroom
    double efficiency = 0.85;  // buck
    double iMax = 0.0;         // A, rated output current (the budget check)
    double iLimit = 0.0;       // A, where the output folds back (its current limit); 0 = iMax
    double iQuiescent = 0.0;   // A, drawn from the input on top of the output

    static const QStringList& kinds();
};

struct PowerLoadDef {          // static draw of a power pin
    QString pin;
    double current = 0.0;      // A, typical
    double minV = 0.0;         // the part works at or above this supply voltage
};

struct BrownoutDef {           // boards: the MCU resets when its rail drops below
    bool present = false;
    QString rail;              // pin id of the MCU's supply rail
    double threshold = 0.0;    // V
    double hysteresis = 0.0;   // V; released at threshold + hysteresis
    double restart = 0.0;      // s from release to setup()
};

struct BatteryDef {            // battery parts: state of charge (docs/user/create_power.md)
    bool present = false;
    int cells = 1;             // in series; the curve and the thresholds are per cell
    double capacityAh = 0.0;   // rated: at the datasheet's drain down to its cut-off (= 0 %)
    // Discharge curve per cell: (charge %, open-circuit V, internal Ω), sorted
    // by charge, from the dead point (at or below 0 %: the charge a cell still
    // gives past its rated cut-off) to 100 %.
    struct Point { double percent, voltage, resistance; };
    QList<Point> curve;
    // Rate-capacity and recovery (Kinetic Battery Model): the load draws from
    // an available well holding `availableFraction` of the charge; the rest
    // flows in at rateConstant·(h_bound − h_available) per hour. 1 = one well.
    double availableFraction = 1.0;
    double rateConstant = 0.0;   // 1/h
    double overDischargeV = 0.0; // per cell, open circuit; below it a real cell is damaged; 0 = no limit
    struct Protection {          // a protection circuit (BMS): cuts the pack off
        bool present = false;
        double cutoffV = 0.0;    // per cell, at the terminals (under load)
        double delay = 0.0;      // s below cutoffV before it cuts
        bool fitted = false;     // default for an instance (power.protection)
    } protection;

    Point at(double percent) const;       // per cell, interpolated, clamped to the curve
    Point packAt(double percent) const;   // the whole pack (cells in series)
    double deadPercent() const { return curve.isEmpty() ? 0.0 : curve.first().percent; }
};

struct PowerDef {
    bool present = false;
    QList<SupplyDef> supplies;
    QList<RegulatorDef> regulators;
    QList<PowerLoadDef> loads;
    BrownoutDef brownout;
    BatteryDef battery;
    QMap<QString, QString> sources;   // field -> datasheet / estimate

    const PowerLoadDef* loadOn(const QString& pin) const;
    // The pin is driven by this part's supply or regulator output.
    bool sourcesPin(const QString& pin) const;
};

struct ConnectorDef {
    QString id;
    QString body;
    QString description;
    Transform transform;
    MechanicDef mechanics;
};

// Reference to something else in the same file: a physical object
// (joint/tendon/site/geom), a device (actuator/sensor/camera/display) or
// "emulator" (no id).
struct TargetRef {
    QString kind;
    QString id;
};

struct TendonTerm {
    QString joint;
    double coef = 0.0;
};

struct TendonDef {
    QString id;
    QString type = "fixed";
    QList<TendonTerm> terms;
};

struct ActuatorDef {
    QString id;
    QString type;
    TargetRef target;
    QList<double> ctrlrange;
    QList<double> forceRange;
    double kp = 0.0;
    double kv = 0.0;
};

struct SensorDef {
    QString id;
    QString type;
    TargetRef target;
};

struct CameraDef {
    QString id;
    QString type;
    TargetRef target;
    QPair<int, int> resolution = qMakePair(0, 0);
    double fovy = 0.0;
};

// The screen surface is a rectangle in the target geom's parent body frame:
// it faces its +Z, image right = +X, image up = +Y (pixel 0,0 at the -X,+Y
// corner). surfaceSize is half-extents in meters, like a box geom's x/y.
struct DisplayDef {
    QString id;
    TargetRef target;
    QPair<int, int> resolution = qMakePair(0, 0);
    Transform surface;
    QPair<double, double> surfaceSize = qMakePair(0.0, 0.0);
};

// Advisory value domain of a signal, in the signal's own unit.
// type: "ranged" (parameters min, max) | "unbounded" (no parameters).
struct DomainDef {
    QString type = "unbounded";
    QMap<QString, double> parameters;
};

// A public signal of the component (schema 2 "interface" entry).
// channelType/dim are only meaningful for target.kind == "emulator"; for every
// other kind they are derived from the target (see ComponentData::interfaceDim).
struct InterfaceDef {
    QString name;
    QString unit;
    DomainDef domain;
    bool physical = true;
    // The part's own internal state (motor drive, pot reading, gear speed):
    // plotted for insight, listed apart from the part's inputs and outputs.
    bool internal = false;
    TargetRef target;

    QString channelType = "scalar";
    int dim = 1;
    QList<QString> componentLabels;
};

struct EmulatorDef {
    QString type;
    QJsonObject source;
    QMap<QString, QVariant> parameters;
    // parameter -> where its value comes from (datasheet, measurement, estimate)
    QMap<QString, QString> parameterSources;
};





struct ComponentData {
    QString basePath;

    QString modelId;
    MetaDef meta;
    QJsonObject specs;
    QList<PinDef> pins;

    QMap<QString, QString> meshResources;
    QMap<QString, QString> materialResources;

    QMap<QString, Node> bodies;
    QList<Edge> joints;
    QList<TendonDef> tendons;
    QString defaultBodyId;

    QMap<QString, ConnectorDef> connectors;

    QList<ActuatorDef> actuators;
    QList<SensorDef> sensors;
    QList<CameraDef> cameras;
    QList<DisplayDef> displays;

    QMap<QString, InterfaceDef> interfaceInputs;    // keyed by signal name
    QMap<QString, InterfaceDef> interfaceOutputs;   // keyed by signal name
    EmulatorDef emulatorDef;
    BoardDef board;
    PowerDef power;

    // Set by fromJson. On failure the returned data is otherwise empty.
    bool isValid = true;
    QString errorString;

    static constexpr int kSchemaVersion = 3;      // written on save
    static constexpr int kMinSchemaVersion = 2;   // oldest still loaded (upgraded in memory)

    QJsonObject toJson() const;
    static ComponentData fromJson(const QJsonObject& obj, const QString& basePath);

    // Joints stacked on the same body pair (e.g. servo_joint + backlash_joint)
    // all need armature > 0, otherwise MuJoCo's mass matrix is singular.
    // Returns the error for the first offending joint, or "" if all are fine.
    static QString checkStackedJoints(const QList<Edge>& joints);

    // Shape and dimension of a signal: stated for "emulator" kind, otherwise
    // derived from the target (DeviceTypes). dim 0 = image / unresolved.
    int interfaceDim(const InterfaceDef& def) const;
    SignalShape interfaceShape(const InterfaceDef& def) const;
};
