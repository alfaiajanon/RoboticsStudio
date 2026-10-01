#pragma once

#include <QString>
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
};

struct PinDef {
    QString id;
    QString description;
    QPair<float, float> voltageRange;
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

struct DisplayDef {
    QString id;
    TargetRef target;
    QPair<int, int> resolution = qMakePair(0, 0);
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
    TargetRef target;

    QString channelType = "scalar";
    int dim = 1;
    QList<QString> componentLabels;
};

struct EmulatorDef {
    QString type;
    QJsonObject source;
    QMap<QString, QVariant> parameters;
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

    // Set by fromJson. On failure the returned data is otherwise empty.
    bool isValid = true;
    QString errorString;

    static constexpr int kSchemaVersion = 2;

    QJsonObject toJson() const;
    static ComponentData fromJson(const QJsonObject& obj, const QString& basePath);

    // Shape and dimension of a signal: stated for "emulator" kind, otherwise
    // derived from the target (DeviceTypes). dim 0 = image / unresolved.
    int interfaceDim(const InterfaceDef& def) const;
    SignalShape interfaceShape(const InterfaceDef& def) const;
};
