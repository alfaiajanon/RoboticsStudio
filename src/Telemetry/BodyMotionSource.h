// src/Telemetry/BodyMotionSource.h

#pragma once

#include "TelemetrySource.h"
#include "Channel.h"
#include <memory>

/*
 * Captures linear/angular velocity or acceleration of a single MuJoCo body
 * (one body = one physical part of a component) via mj_objectVelocity /
 * mj_objectAcceleration and feeds a registered VectorChannel (dim=3).
 */
class BodyMotionSource : public TelemetrySource {
public:
    enum class Quantity {
        LinearVelocity,
        AngularVelocity,
        LinearAcceleration,
        AngularAcceleration
    };

    BodyMotionSource(int compUid, const QString& bodyName, Quantity quantity, bool localFrame = false);

    int channelId() const override { return channelId_; }
    QString description() const override { return description_; }

    void capture(mjModel* m, mjData* d, double time) override;

private:
    int compUid;
    QString bodyName;
    Quantity quantity;
    bool localFrame;

    int bodyId = -1;
    int channelId_ = -1;
    QString description_;
    std::shared_ptr<VectorChannel> channel;
};
