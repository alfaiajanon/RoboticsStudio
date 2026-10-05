#pragma once

#include <QString>
#include <QStringList>
#include <cmath>


/*
 * The one place that maps a signal's unit to MuJoCo's. Users author and see
 * angles in degrees (and angular rates in degrees per second); MuJoCo works
 * in radians. Every value crossing that boundary -- runtime signal values
 * (SimulationManager sync) and authored limits such as ctrlrange
 * (ComponentBlueprint) -- is scaled by toMujoco(unit).
 *
 * Any other unit (m, N·m, rad/s, ...) is passed through unchanged.
 */
namespace Units {
    inline bool isAngular(const QString& unit) {
        static const QStringList units = {
            "degree", "degrees", "deg",                       // angle
            "degree/s", "degrees/s", "deg/s", "dps"           // angular rate
        };
        return units.contains(unit.trimmed().toLower());
    }

    // Multiply a value in `unit` by this to get MuJoCo units; divide to go back.
    inline double toMujoco(const QString& unit) {
        return isAngular(unit) ? M_PI / 180.0 : 1.0;
    }
}
