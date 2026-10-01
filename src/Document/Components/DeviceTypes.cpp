#include "DeviceTypes.h"


static QList<DeviceTypeInfo> buildTable() {
    QList<DeviceTypeInfo> t;
    auto add = [&t](DeviceFamily f, const char* type, const char* element,
                    QStringList targets, int dim, SignalShape shape,
                    QStringList labels = {}) {
        DeviceTypeInfo i;
        i.family = f;
        i.type = type;
        i.mjcfElement = element;
        i.allowedTargets = targets;
        i.dim = dim;
        i.shape = shape;
        i.defaultLabels = labels;
        t.append(i);
    };

    const QStringList joint_tendon = {"joint", "tendon"};
    const QStringList xyz = {"x", "y", "z"};

    add(DeviceFamily::Actuator, "position", "position", joint_tendon, 1, SignalShape::Scalar);
    add(DeviceFamily::Actuator, "velocity", "velocity", joint_tendon, 1, SignalShape::Scalar);
    add(DeviceFamily::Actuator, "motor",    "motor",    joint_tendon, 1, SignalShape::Scalar);

    add(DeviceFamily::Sensor, "jointpos",      "jointpos",      {"joint"},  1, SignalShape::Scalar);
    add(DeviceFamily::Sensor, "jointvel",      "jointvel",      {"joint"},  1, SignalShape::Scalar);
    add(DeviceFamily::Sensor, "tendonpos",     "tendonpos",     {"tendon"}, 1, SignalShape::Scalar);
    add(DeviceFamily::Sensor, "tendonvel",     "tendonvel",     {"tendon"}, 1, SignalShape::Scalar);
    add(DeviceFamily::Sensor, "accelerometer", "accelerometer", {"site"},   3, SignalShape::Vector, xyz);
    add(DeviceFamily::Sensor, "gyro",          "gyro",          {"site"},   3, SignalShape::Vector, xyz);

    add(DeviceFamily::Camera, "rgb",   "camera", {"site"}, 0, SignalShape::Image);
    add(DeviceFamily::Camera, "depth", "camera", {"site"}, 0, SignalShape::Image);

    add(DeviceFamily::Display, "", "", {"geom"}, 0, SignalShape::Image);

    return t;
}


const QList<DeviceTypeInfo>& DeviceTypes::all() {
    static const QList<DeviceTypeInfo> table = buildTable();
    return table;
}


const DeviceTypeInfo* DeviceTypes::find(DeviceFamily family, const QString& type) {
    for (const DeviceTypeInfo& row : all()) {
        if (row.family == family && row.type == type) return &row;
    }
    return nullptr;
}


QStringList DeviceTypes::typeNames(DeviceFamily family) {
    QStringList names;
    for (const DeviceTypeInfo& row : all()) {
        if (row.family == family && !row.type.isEmpty()) names.append(row.type);
    }
    return names;
}


QString DeviceTypes::familyKey(DeviceFamily family) {
    switch (family) {
        case DeviceFamily::Actuator: return "actuators";
        case DeviceFamily::Sensor:   return "sensors";
        case DeviceFamily::Camera:   return "cameras";
        case DeviceFamily::Display:  return "displays";
    }
    return QString();
}


QString DeviceTypes::shapeName(SignalShape shape) {
    switch (shape) {
        case SignalShape::Scalar: return "scalar";
        case SignalShape::Vector: return "vector";
        case SignalShape::Image:  return "image";
    }
    return QString();
}
