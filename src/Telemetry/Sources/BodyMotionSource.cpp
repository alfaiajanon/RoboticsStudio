// src/Telemetry/BodyMotionSource.cpp

#include "BodyMotionSource.h"
#include "Application/Application.h"
#include "Simulation/MujocoContext.h"
#include "Simulation/SimulationManager.h"
#include "TelemetrySource.h"
#include "mujoco/mjmodel.h"
#include <any>




/*
 * Registers the backing vector channel and builds the UI label.
 * The MuJoCo body id is resolved lazily on first capture, since the
 * model may be (re)loaded after this source is created.
 */
BodyMotionSource::BodyMotionSource(int compUid, const QString& bodyName, MotionType type, bool localFrame) :
            storage(1000, 3){

    this->compUid=compUid;
    this->bodyName=bodyName;
    this->type=type;
    this->localFrame=localFrame;

    static const char* motionTypeNames[] = { "lin vel", "ang vel", "lin accel", "ang accel" };
    description=QString("comp_%1.%2 · %3%4")
                        .arg(compUid)
                        .arg(bodyName)
                        .arg(motionTypeNames[static_cast<int>(type)])
                        .arg(localFrame ? " (local)" : "");
    name=description;
}




/*
 * Pulls the requested 6D motion quantity for the body out of MuJoCo and
 * pushes the matching 3 components into the channel.
 * res6 layout is [angular(3); linear(3)] for both mj_objectVelocity and
 * mj_objectAcceleration. Values are valid right after mj_step/mj_forward.
 * Runs on the physics thread while physicsMutex is held.
 */
void BodyMotionSource::capture(double time) {
    MujocoContext *mj=Application::getInstance()->getSimulationManager()->getMujocoContext();
    mjModel *m = mj->getModel();
    mjData *d = mj->getData();

    if (!m || !d || !isActive()) return;

    if (bodyMjId < 0) {
        QString fullName = "comp_" + QString::number(compUid) + "_" + bodyName;
        bodyMjId = mj_name2id(m, mjOBJ_BODY, fullName.toStdString().c_str());
        if (bodyMjId < 0) return; // component not in the current model
    }

    mjtNum res6[6] = {0, 0, 0, 0, 0, 0};
    bool isVelocity = (type == MotionType::LinearVelocity || type == MotionType::AngularVelocity);

    if (isVelocity) {
        mj_objectVelocity(m, d, mjOBJ_BODY, bodyMjId, res6, localFrame ? 1 : 0);
    } else {
        mj_objectAcceleration(m, d, mjOBJ_BODY, bodyMjId, res6, localFrame ? 1 : 0);
    }

    bool isAngular = (type == MotionType::AngularVelocity || type == MotionType::AngularAcceleration);
    int offset = isAngular ? 0 : 3;

    storage.push(time, { res6[offset], res6[offset + 1], res6[offset + 2] });
}



any BodyMotionSource::snapshotAll(){
    return storage.snapshot();
}
