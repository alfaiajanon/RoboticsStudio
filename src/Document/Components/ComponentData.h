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
    bool isBoardPin() const { return !type.isEmpty() && !isPower() && type != "ground" && type != "gpio"; }
    bool isPower() const { return type == "power" || type == "power_out"; }
    bool hasCap(const QString& cap) const { return caps.contains(cap); }
};

// What a board (a component with a "board" block) is: what its firmware is
// compiled for and the limits of its pins. Only on board parts.
struct BoardDef {
    bool present = false;
    QString fqbn;              // arduino-cli board, e.g. arduino:avr:uno
    QString headerFolder;      // project subfolder for its robot_pins.h ("uno")
    double logicVoltage = 5.0;
    int adcBits = 10;
    double adcVref = 5.0;
    double analogWriteHz = 490.0;
    QMap<QString, double> analogWriteHzByPin;   // pin id -> frequency
    QString i2cSda, i2cScl;    // pin ids of the hardware I2C bus
    double i2cDefaultHz = 100000.0;
    QString spiSck, spiMosi, spiMiso;
    double spiMaxHz = 8000000.0;
    QMap<QString, QString> sources;   // field -> datasheet / estimate
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
