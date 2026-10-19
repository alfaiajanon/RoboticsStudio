#pragma once

#include <QMap>
#include <QString>
#include <QVariant>
#include "Document/Components/ComponentInstance.h"

/*
 * Small helpers every device model uses: its .rsdef parameters and its
 * scalar signals (BasicIOValue slots on the ComponentInstance).
 */
namespace EmulatorIO {

inline double param(const QMap<QString, QVariant>& p, const char* key, double def) {
    return p.contains(key) ? p.value(key).toDouble() : def;
}

// First component of an output (sensor or emulator-owned) signal; 0 if the
// part doesn't declare it.
inline double readOutput(const ComponentInstance* comp, const char* name) {
    BasicIOValue v = comp->getSensorValue(name);
    return v.data.empty() ? 0.0 : v.data[0];
}

inline bool hasOutput(const ComponentInstance* comp, const char* name) {
    return !comp->getSensorValue(name).data.empty();
}

// Writes the first component of an input (input = true) or output signal;
// does nothing if the part doesn't declare it.
inline void writeScalar(ComponentInstance* comp, const char* name, double value, bool input) {
    BasicIOValue v = input ? comp->getActuatorValue(name) : comp->getSensorValue(name);
    if (v.data.empty()) return;
    v.data[0] = value;
    if (input) comp->setActuatorValue(name, v);
    else       comp->setSensorValue(name, v);
}

}   // namespace EmulatorIO
