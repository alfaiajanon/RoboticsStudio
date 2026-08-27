// src/Telemetry/BodyMotionSource.cpp

#include "BodyMotionSource.h"
#include "TelemetryRegistry.h"




/*
 * Registers the backing vector channel and builds the UI label.
 * The MuJoCo body id is resolved lazily on first capture, since the
 * model may be (re)loaded after this source is created.
 */
BodyMotionSource::BodyMotionSource(int compUid, const QString& bodyName, Quantity quantity, bool localFrame)
    : compUid(compUid), bodyName(bodyName), quantity(quantity), localFrame(localFrame) {

    static const char* quantityNames[] = { "lin vel", "ang vel", "lin accel", "ang accel" };
    description_ = QString("comp_%1.%2 · %3%4")
                        .arg(compUid)
                        .arg(bodyName)
                        .arg(quantityNames[static_cast<int>(quantity)])
                        .arg(localFrame ? " (local)" : "");

    ChannelMeta meta;
    meta.name = description_;
    meta.source = SourceKind::BODY_MOTION;
    meta.physical = true;
    meta.dim = 3;

    auto& registry = TelemetryRegistry::getInstance();
    channelId_ = registry.registerVector(meta);
    channel = registry.getVector(channelId_);
}




/*
 * Pulls the requested 6D motion quantity for the body out of MuJoCo and
 * pushes the matching 3 components into the channel.
 * res6 layout is [angular(3); linear(3)] for both mj_objectVelocity and
 * mj_objectAcceleration. Values are valid right after mj_step/mj_forward.
 * Runs on the physics thread while physicsMutex is held.
 */
void BodyMotionSource::capture(mjModel* m, mjData* d, double time) {
    if (!m || !d || !channel || !channel->isActive()) return;

    if (bodyId < 0) {
        QString fullName = "comp_" + QString::number(compUid) + "_" + bodyName;
        bodyId = mj_name2id(m, mjOBJ_BODY, fullName.toStdString().c_str());
        if (bodyId < 0) return; // component not in the current model
    }

    mjtNum res6[6] = {0, 0, 0, 0, 0, 0};
    bool isVelocity = (quantity == Quantity::LinearVelocity || quantity == Quantity::AngularVelocity);

    if (isVelocity) {
        mj_objectVelocity(m, d, mjOBJ_BODY, bodyId, res6, localFrame ? 1 : 0);
    } else {
        mj_objectAcceleration(m, d, mjOBJ_BODY, bodyId, res6, localFrame ? 1 : 0);
    }

    bool isAngular = (quantity == Quantity::AngularVelocity || quantity == Quantity::AngularAcceleration);
    int offset = isAngular ? 0 : 3;

    channel->push(time, { res6[offset], res6[offset + 1], res6[offset + 2] });
}
