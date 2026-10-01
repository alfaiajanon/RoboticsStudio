#pragma once

#include <QString>
#include <QStringList>
#include <QList>


enum class DeviceFamily { Actuator, Sensor, Camera, Display };

enum class SignalShape { Scalar, Vector, Image };


/*
 * One row per device type. Single source of truth (D10) for loader
 * validation, XML generators, runtime map sizing and (later) editor dropdowns.
 *
 * dim: sensors = size of the sensor's output (matches mjModel::sensor_dim);
 *      actuators = width of the control input; cameras/displays = 0 (image).
 * mjcfElement: empty for devices without a plain MJCF element (displays).
 * defaultLabels: default legend names for vector shapes (may be empty).
 */
struct DeviceTypeInfo {
    DeviceFamily family = DeviceFamily::Sensor;
    QString type;
    QString mjcfElement;
    QStringList allowedTargets;
    int dim = 1;
    SignalShape shape = SignalShape::Scalar;
    QStringList defaultLabels;
};


namespace DeviceTypes {

    // nullptr if (family, type) is not in the table. Displays have a single
    // row whose type is the empty string.
    const DeviceTypeInfo* find(DeviceFamily family, const QString& type);

    const QList<DeviceTypeInfo>& all();
    QStringList typeNames(DeviceFamily family);

    // JSON key under "devices": "actuators" | "sensors" | "cameras" | "displays"
    QString familyKey(DeviceFamily family);

    QString shapeName(SignalShape shape);
}
