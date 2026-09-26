// src/Telemetry/BodyMotionSource.h

#pragma once

#include "Telemetry/Storage/TimeSeriesBuffer.h"
#include "TelemetrySource.h"
#include <any>

/*
 * Captures linear/angular velocity or acceleration of a single MuJoCo body
 * (one body = one physical part of a component) via mj_objectVelocity /
 * mj_objectAcceleration and feeds a registered VectorChannel (dim=3).
 */
class BodyMotionSource : public TelemetrySource {
    public:
        enum class MotionType {
            LinearVelocity=0,
            AngularVelocity=1,
            LinearAcceleration=2,
            AngularAcceleration=3
        };

        BodyMotionSource(int compUid, const QString& bodyName, MotionType type, bool localFrame = false);

        void capture(double time) override;
        std::any snapshotAll() override;

    private:
        int compUid;
        QString bodyName;
        MotionType type;
        bool localFrame;

        int bodyMjId = -1;

        TimeSeriesBuffer storage;
};
