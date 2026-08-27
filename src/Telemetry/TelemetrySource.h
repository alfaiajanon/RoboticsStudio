// src/Telemetry/TelemetrySource.h

#pragma once

#include <QString>
#include "mujoco/mujoco.h"

/*
 * Producer-side abstraction for telemetry data.
 * A TelemetrySource owns (or feeds) a registered channel and knows how to
 * extract its data from the live MuJoCo state. The physics thread calls
 * capture() after stepping; the UI only ever sees the channel.
 * Subclass this to add new telemetry kinds (body motion, forces, energy, ...).
 */
class TelemetrySource {
public:
    virtual ~TelemetrySource() = default;

    virtual int channelId() const = 0;
    virtual QString description() const = 0;

    // Called from the physics thread while physicsMutex is held.
    virtual void capture(mjModel* m, mjData* d, double time) = 0;
};
