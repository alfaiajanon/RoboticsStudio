#pragma once

#include <QString>
#include <QStringList>



/*
 * meta.hint_class: optional, purely descriptive category of a component
 * (the robot tree picks an icon from it). Absent means "generic".
 * Keep in sync with schema/rsdef.schema.json and tools/validate_rsdef.py.
 */
namespace HintClass {
    inline const QStringList& all() {
        static const QStringList values = {
            "actuator",   // servos, motors, steppers
            "sensor",     // IMUs, encoders, ...
            "vision",     // cameras
            "display",    // screens
            "mcu",        // microcontrollers / boards
            "weight",     // dummy masses
            "battery",
            "part",       // structural: brackets, plates, wheels
        };
        return values;
    }

    inline bool isValid(const QString& value) { return all().contains(value); }
}
