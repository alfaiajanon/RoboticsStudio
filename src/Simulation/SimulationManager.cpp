#include "SimulationManager.h"
#include "MujocoContext.h"

#include <chrono>
#include <limits>
#include <qobject.h>
#include "Utils/Log.h"
#include "Document/Project.h"
#include "Document/Stage.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/Units.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/Rendering/CameraRenderer.h"
#include "Simulation/Rendering/DisplayTextures.h"
#include "mujoco/mjdata.h"





/*
 * Initializes the simulation manager and spawns the background physics thread.
 * Starts in a stopped state with a default 1.0x real-time scale.
 */
SimulationManager::SimulationManager(Project* project): project(project),
                                                        isAlive(true),
                                                        currentState(SimulationState::PAUSED),
                                                        timeScale(1.0f),
                                                        stepAccumulator(0.0f) {
    displayTextures = new DisplayTextures();
    cameraRenderer = new CameraRenderer(&mujocoContext, &physicsMutex, displayTextures);

    // Main viewport: upload display frames in the viewport's GL context. The
    // cache lives in the lambda; only the UI thread renders this context.
    mujocoContext.beforeRender = [this, cache = DisplayTextures::UploadCache()](const mjrContext* con) mutable {
        displayTextures->upload(con, cache);
    };
}





/*
 * Safely signals the physics thread to terminate and waits for it to join.
 * Ensures clean memory cleanup when the application closes.
 */
SimulationManager::~SimulationManager() {
    isAlive = false;

    project->getMicroController()->stop();
    project->getStageController()->stop();

    if (mcuThread.joinable()) {
        mcuThread.join();
    }
    if (stageThread.joinable()) {
        stageThread.join();
    }
    if (physicsThread.joinable()) {
        physicsThread.join();
    }

    // After the physics thread is gone: it is the only producer.
    delete cameraRenderer;
    cameraRenderer = nullptr;
    mujocoContext.beforeRender = nullptr;
    delete displayTextures;
    displayTextures = nullptr;
}





/*
 * Following functions are the public API for controlling the simulation state and time scale.
 */
#pragma region play/edit

void SimulationManager::startThreads() {
    if (currentState != SimulationState::PAUSED) return;
    if (!physicsThread.joinable()) {
        physicsThread = std::thread(&SimulationManager::physicsLoop, this);
    }
    if (!mcuThread.joinable()) {
        mcuThread = std::thread(&SimulationManager::mcuLoop, this, project->getMicroController());
    }
    if (!stageThread.joinable()) {
        stageThread = std::thread(&SimulationManager::mcuLoop, this, project->getStageController());
    }
}


void SimulationManager::play() {

    ComponentInstance* root = project->getRootComponent();
    initEmulators(root);
    buildSchedule(root);

    startThreads();

    // move joint data to actuator targets before starting simulation
    #pragma region todo

    MicroController* mcu = project->getMicroController();
    mcu->compile(project->getScript(), project->getRootComponent());

    // Stage script: sees the controlled props and the robot pose, nothing else.
    Stage* stage = project->getStage();
    MicroController* stageMcu = project->getStageController();
    QString stageScript = project->readStageScript();
    if (stageScript.isEmpty()) {
        stageMcu->stop();
    } else {
        for (const auto& h : stage->getHandles()) {
            stageMcu->setGlobal("prop_" + h->id, h.get());
        }
        stageMcu->setGlobal("robot", stage->getRobotPose());
        stageMcu->compile(stageScript, nullptr);
    }

    currentState = SimulationState::PLAYING;
}



void SimulationManager::edit() {
    startThreads();

    MujocoContext* mj = &mujocoContext;
    project->getMicroController()->stop();
    project->getStageController()->stop();
    for (const auto& h : project->getStage()->getHandles()) h->resetRuntime();

    ComponentInstance* root = project->getRootComponent();
    clearSchedule();
    resetEmulators(root);

    mj_resetData(mj->getModel(), mj->getData());
    mj->forward();

    currentState = SimulationState::EDITING;
}



void SimulationManager::pause() {
    currentState = SimulationState::PAUSED;
}






SimulationState SimulationManager::getState() const {
    return currentState.load();
}







void SimulationManager::setTimeScale(float scale) {
    timeScale = scale;
}







void SimulationManager::trackFps() {
    if (!fpsTimer.isValid()) {
        fpsTimer.start();
    }

    frameCount++;

    if (fpsTimer.elapsed() >= 400) {
        int currentFps = static_cast<int>((frameCount*1000.0 / fpsTimer.elapsed()));
        // emit fpsUpdated(currentFps);
        status.fps=currentFps;
        emit statusUpdated(status);

        frameCount = 0;
        fpsTimer.restart();
    }
}







#pragma region mcuLoop


void SimulationManager::mcuLoop(MicroController* mcu) {
    while (isAlive) {
        if (currentState == SimulationState::PLAYING) {

            mcu->run();

            // Throttle the MCU to simulate a cheap processor
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }else{
            // Sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
}









/*
 * The continuous background physics loop.
 * Calculates scaled timesteps, locks the engine, processes math, and yields.
 */
#pragma region physicsLoop

void SimulationManager::physicsLoop() {
    lastTickTime = std::chrono::steady_clock::now();

    while (isAlive) {
        auto now = std::chrono::steady_clock::now();
        double dtSeconds = std::chrono::duration<double>(now - lastTickTime).count();
        lastTickTime = now;
        dtSeconds = std::min(dtSeconds, 0.25);  // Clamp so a debugger pause / OS hitch doesn't force a huge
                                                // catch-up burst of steps on the next tick.

        if (currentState == SimulationState::PLAYING) {
            MujocoContext* mj = &mujocoContext;
            double modelTimestep = mj->getModel()->opt.timestep;

            stepAccumulator += (dtSeconds * timeScale.load()) / modelTimestep;
            int stepsToTake = static_cast<int>(stepAccumulator);
            stepAccumulator -= stepsToTake;

            if (stepsToTake > 0) {
                ComponentInstance* root = project->getRootComponent();
                {
                    std::lock_guard<std::mutex> lock(physicsMutex);

                    syncToMujocoActuator(root, mj->getModel(), mj->getData());
                    syncToMujocoProps(project->getStage(), mj->getModel(), mj->getData());
                    for(int i = 0; i < stepsToTake; ++i) {
                        mj->step();
                        // Async devices sample at the exact step that reaches their deadline.
                        if (!asyncEmulators.empty() && mj->getData()->time >= nextAsyncDue) {
                            runDueAsync(mj->getData()->time);
                        }
                    }
                    syncFromMujocoSensor(root, mj->getModel(), mj->getData());
                    syncFromMujocoProps(project->getStage(), mj->getModel(), mj->getData());

                    runEmulators(mj->getData()->time); // position it here to capture both sensor and actuator properly
                    displayTextures->collect();

                    // pushTelemetry(root, mj->getData()->time);
                    TelemetryRegistry::getInstance().captureAll(mj->getData()->time);
                }
            }
            status.simTime=mj->getData()->time;
            status.activeContacts=mj->getData()->ncon;

            int invasiveCount = 0;
            const double PENETRATION_THRESHOLD = -0.002;
            mjData* d = mj->getData();
            for (int i = 0; i < d->ncon; ++i) {
                if (d->contact[i].dist < PENETRATION_THRESHOLD) {
                    invasiveCount++;
                }
            }
            status.invasiveCollisions = invasiveCount;
        }

        else if (currentState == SimulationState::EDITING) {
            // In editing mode, we sync joint positions to allow dragging components
            std::lock_guard<std::mutex> lock(physicsMutex);

            MujocoContext* mj = &mujocoContext;
            ComponentInstance* root = project->getRootComponent();

            syncToMujocoJoint(root, mj->getModel(), mj->getData());
            mj->forward();

            syncFromMujocoSensor(root, mj->getModel(), mj->getData());
            displayTextures->collect();   // emulator resets blank the screens
        }

        trackFps();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}







void SimulationManager::initEmulators(ComponentInstance* comp) {
    if (!comp) return;
    if (comp->emulator) {
        comp->emulator->init();
    }
    for (ComponentInstance* child : comp->children) {
        initEmulators(child);
    }
}



/*
 * Flattens the component tree into the per-cadence emulator lists (pre-order,
 * so synchronous emulators keep the order the old recursive pass had).
 * Throttled/async deadlines start at the current sim time: the first update or
 * sample happens on the first tick. Periods shorter than one physics step are
 * clamped to one step.
 */
void SimulationManager::buildSchedule(ComponentInstance* root) {
    std::lock_guard<std::mutex> lock(physicsMutex);

    syncEmulators.clear();
    throttledEmulators.clear();
    asyncEmulators.clear();

    mjModel* m = mujocoContext.getModel();
    mjData* d = mujocoContext.getData();
    if (!m || !d) return;

    collectEmulators(root, d->time, m->opt.timestep);

    nextAsyncDue = d->time;
}


void SimulationManager::clearSchedule() {
    std::lock_guard<std::mutex> lock(physicsMutex);
    syncEmulators.clear();
    throttledEmulators.clear();
    asyncEmulators.clear();
}


void SimulationManager::collectEmulators(ComponentInstance* comp, double now, double minPeriod) {
    if (!comp) return;
    if (Emulator* e = comp->emulator) {
        Schedule s = e->schedule();
        if (s.cadence == Cadence::Synchronous) {
            syncEmulators.push_back(e);
        } else {
            double period = s.periodSec;
            if (period < minPeriod) {
                Log::warning(QString("SimulationManager: component %1 (%2): emulator period %3 s is shorter than one physics step; using %4 s")
                                .arg(comp->uid).arg(comp->modelId).arg(period).arg(minPeriod));
                period = minPeriod;
            }
            ScheduledEmulator entry{e, period, now};
            if (s.cadence == Cadence::Throttled) throttledEmulators.push_back(entry);
            else                                 asyncEmulators.push_back(entry);
        }
    }
    for (ComponentInstance* child : comp->children) {
        collectEmulators(child, now, minPeriod);
    }
}


// Next deadline after `now`, keeping the phase; if the loop fell more than a
// period behind (a hitch), skip the missed samples instead of bursting.
static double advanceDeadline(double due, double period, double now) {
    due += period;
    if (due <= now) due = now + period;
    return due;
}


/*
 * Called from inside the step loop (physicsMutex held) once sim time reaches
 * nextAsyncDue: samples every async emulator that is due, then recomputes the
 * earliest deadline.
 */
void SimulationManager::runDueAsync(double now) {
    SampleContext ctx;
    ctx.m = mujocoContext.getModel();
    ctx.d = mujocoContext.getData();
    ctx.simTime = now;
    ctx.cameraRenderer = cameraRenderer;

    double earliest = std::numeric_limits<double>::infinity();
    for (ScheduledEmulator& s : asyncEmulators) {
        if (now >= s.nextDue) {
            s.emulator->sample(ctx);
            s.nextDue = advanceDeadline(s.nextDue, s.periodSec, now);
        }
        earliest = std::min(earliest, s.nextDue);
    }
    nextAsyncDue = earliest;
}


/*
 * Once per physics tick, after the sensor sync (physicsMutex held):
 * synchronous emulators every tick, throttled ones when due, async ones every
 * tick so they can publish what their worker finished.
 */
void SimulationManager::runEmulators(double now) {
    for (Emulator* e : syncEmulators) {
        e->setSimTime(now);
        e->update();
    }
    for (ScheduledEmulator& s : throttledEmulators) {
        if (now >= s.nextDue) {
            s.emulator->setSimTime(now);
            s.emulator->update();
            s.nextDue = advanceDeadline(s.nextDue, s.periodSec, now);
        }
    }
    for (ScheduledEmulator& s : asyncEmulators) {
        s.emulator->setSimTime(now);
        s.emulator->update();
    }
}



/*
 * Swaps the model in under physicsMutex and re-resolves MuJoCo ids. The
 * camera renderer is told first, while the old model is still alive, so it
 * drops queued frames and rebuilds its GPU context for the new model.
 */
bool SimulationManager::reloadModel(const QString& xml, ComponentInstance* root) {
    std::lock_guard<std::mutex> lock(physicsMutex);

    if (cameraRenderer) cameraRenderer->invalidate();
    // Fog is a render flag, not MJCF; set before the load so the rebuilt
    // scene (and every camera scene) picks it up.
    mujocoContext.setSceneFlag(mjRND_FOG, project->getStage()->getData().env.fog);
    bool ok = mujocoContext.loadModelFromString(xml.toStdString());
    cacheMujocoIds(root, mujocoContext.getModel());
    cacheStageIds(project->getStage(), mujocoContext.getModel());
    displayTextures->rebuild(root, mujocoContext.getModel());
    return ok;
}




// void SimulationManager::pushTelemetry(ComponentInstance* comp, double time) {
//     if (!comp || !comp->blueprint) return;

//     auto& registry = TelemetryRegistry::getInstance();

//     for (const QString& key : comp->blueprint->inputDefs.keys()) {
//         int channelId = comp->getActuatorChannelId(key);
//         if (channelId != -1) {
//             IOData data = comp->getActuatorTarget(key);
//             QString cType = comp->blueprint->inputDefs[key].channelType;

//             if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
//                 if (auto channel = registry.getVector(channelId)) {
//                     channel->push(time, std::get<std::vector<double>>(data));
//                 }
//             } else if (std::holds_alternative<double>(data)) {
//                 if (auto channel = registry.getScalar(channelId)) {
//                     channel->push(time, std::get<double>(data));
//                 }
//             }
//         }
//     }

//     for (const QString& key : comp->blueprint->outputDefs.keys()) {
//         int channelId = comp->getSensorChannelId(key);
//         if (channelId != -1) {
//             IOData data = comp->getSensorCurrent(key);
//             QString cType = comp->blueprint->outputDefs[key].channelType;

//             if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
//                 if (auto channel = registry.getVector(channelId)) {
//                     channel->push(time, std::get<std::vector<double>>(data));
//                 }
//             } else if (std::holds_alternative<double>(data)) {
//                 if (auto channel = registry.getScalar(channelId)) {
//                     channel->push(time, std::get<double>(data));
//                 }
//             }
//         }
//     }

//     for (ComponentInstance* child : comp->children) {
//         pushTelemetry(child, time);
//     }
// }




void SimulationManager::resetEmulators(ComponentInstance* comp) {
    if (!comp) return;
    if (comp->emulator) {
        comp->emulator->reset();
    }
    for (ComponentInstance* child : comp->children) {
        resetEmulators(child);
    }
}








#pragma region mujoco stuff



/*
 * One-time setup to map signals to MuJoCo's internal C-array indices.
 * Signal -> device -> MJCF name comp_<uid>_<deviceId> (D9). Traverses the
 * component tree recursively.
 *
 *   input  kind joint    : joint id from the joint name; actuator id from the first
 *                          actuator targeting that joint (none: logged, no ctrl written)
 *   input  kind actuator : actuator id from the actuator name
 *   output kind sensor   : sensor id from the sensor name (dimension cross-checked
 *                          against the model once, here)
 *   output kind camera   : camera id from the camera name (into the ImageIOValue)
 *   emulator / display kinds have no MuJoCo id and are left at -1.
 */
void SimulationManager::cacheMujocoIds(ComponentInstance* root, mjModel* m) {
    if (!m || !root) return;

    QString prefix = "comp_" + QString::number(root->uid) + "_";

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            QString actuatorId;
            if (def.target.kind == "joint") {
                int jointMjId = mj_name2id(m, mjOBJ_JOINT, (prefix + def.target.id).toStdString().c_str());
                BasicIOValue jvalue = root->getJointValue(def.target.id);
                jvalue.mujocoId = jointMjId;
                root->setJointValue(def.target.id, jvalue);

                for (const ActuatorDef& a : bp->actuators) {
                    if (a.target.kind == "joint" && a.target.id == def.target.id) { actuatorId = a.id; break; }
                }
                if (actuatorId.isEmpty()) {
                    Log::warning(QString("SimulationManager: component %1 (%2): input '%3' targets joint '%4' which has no actuator; commands will not drive it")
                                    .arg(root->uid).arg(root->modelId, key, def.target.id));
                }
            } else if (def.target.kind == "actuator") {
                actuatorId = def.target.id;
            }

            if (!actuatorId.isEmpty()) {
                int id = mj_name2id(m, mjOBJ_ACTUATOR, (prefix + actuatorId).toStdString().c_str());
                BasicIOValue value = root->getActuatorValue(key);
                value.mujocoId = id;
                root->setActuatorValue(key, value);
            }
        }

        for (auto it = bp->interfaceOutputs.constBegin(); it != bp->interfaceOutputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            if (def.target.kind == "camera") {
                ImageIOValue img = root->getImageValue(key);
                img.mujocoId = mj_name2id(m, mjOBJ_CAMERA, (prefix + def.target.id).toStdString().c_str());
                if (img.mujocoId < 0) {
                    Log::error(QString("SimulationManager: component %1 (%2): camera '%3' not found in the MuJoCo model; signal '%4' disabled")
                                    .arg(root->uid).arg(root->modelId, def.target.id, key));
                }
                root->setImageValue(key, img);
                continue;
            }
            if (def.target.kind != "sensor") continue;

            int id = mj_name2id(m, mjOBJ_SENSOR, (prefix + def.target.id).toStdString().c_str());
            int dim = bp->interfaceDim(def);
            if (id >= 0 && m->sensor_dim[id] != dim) {
                Log::error(QString("SimulationManager: component %1 (%2): sensor '%3' has dimension %4 in MuJoCo but %5 in the device table; signal '%6' disabled")
                                .arg(root->uid).arg(root->modelId, def.target.id).arg(m->sensor_dim[id]).arg(dim).arg(key));
                id = -1;
            }
            BasicIOValue value = root->getSensorValue(key);
            value.mujocoId = id;
            root->setSensorValue(key, value);
        }
    }

    for (ComponentInstance* child : root->children) {
        cacheMujocoIds(child, m);
    }
}




/*
 * Pre-step hook to push live input commands into the engine.
 * Writes d->ctrl for every input signal that resolved to an actuator.
 * The unit conversion comes from the interface signal.
 */
void SimulationManager::syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            BasicIOValue data = root->getActuatorValue(it.key());
            // Signals without a runtime slot (display/camera) come back as a default value whose
            // mujocoId is 0, not -1 -- skip them or they would overwrite ctrl[0].
            if (data.data.empty()) continue;
            int mujocoId = data.mujocoId;
            if (mujocoId >= 0 && mujocoId < m->nu) {

                double val = data.dim == 1 ? data.data[0] : 0.0;

                val *= Units::toMujoco(it.value().unit);

                d->ctrl[mujocoId] = val;
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncToMujocoActuator(child, m, d);
    }
}






/*
 * Edit-mode posing: lerps qpos of the joint each kind-joint input targets.
 */
void SimulationManager::syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            const InterfaceDef& def = it.value();
            if (def.target.kind != "joint") continue;

            BasicIOValue data = root->getJointValue(def.target.id);
            if (data.data.empty()) continue;
            int mujocoId = data.mujocoId;

            if (mujocoId >= 0 && mujocoId < m->njnt) {

                double targetVal = data.dim == 1 ? data.data[0] : 0.0;

                targetVal *= Units::toMujoco(def.unit);

                int qposIndex = m->jnt_qposadr[mujocoId];
                double currentPos = d->qpos[qposIndex];

                double smoothSpeed = 0.3;
                if (std::abs(targetVal - currentPos) > 0.0001) {
                    double newPos = currentPos + (targetVal - currentPos) * smoothSpeed;
                    d->qpos[qposIndex] = newPos;
                }
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncToMujocoJoint(child, m, d);
    }
}





/*
 * Post-step hook to pull live physics telemetry out of the engine.
 * Reads each output signal that resolved to a sensor; the dimension comes
 * from the device type (checked against sensor_dim in cacheMujocoIds) and the
 * unit conversion from the interface signal.
 */
void SimulationManager::syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceOutputs.constBegin(); it != bp->interfaceOutputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            BasicIOValue data = root->getSensorValue(key);
            if (data.data.empty()) continue;   // no runtime slot (camera); see syncToMujocoActuator
            int sensorId = data.mujocoId;

            if (sensorId >= 0 && sensorId < m->nsensor) {
                int adr = m->sensor_adr[sensorId];
                int dim = bp->interfaceDim(def);

                if (dim > 0 && adr + dim <= m->nsensordata) {
                    const double fromMujoco = 1.0 / Units::toMujoco(def.unit);

                    if (dim > 1 || bp->interfaceShape(def) == SignalShape::Vector) {
                        std::vector<double> vec(dim);
                        for (int i = 0; i < dim; ++i) {
                            double val = d->sensordata[adr + i];
                            val *= fromMujoco;
                            vec[i] = val;
                        }
                        data.data = vec;
                        root->setSensorValue(key, data);
                    } else {
                        double val = d->sensordata[adr];
                        val *= fromMujoco;

                        data.data[0] = val;
                        root->setSensorValue(key, data);
                    }
                }
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncFromMujocoSensor(child, m, d);
    }
}





#pragma region stage



/*
 * Resolves the controlled props' joints/actuators and the robot root body.
 * Names that are absent (edit mode: no joints/actuators, no robot freejoint)
 * leave the ids at -1 and the syncs skip them.
 */
void SimulationManager::cacheStageIds(Stage* stage, mjModel* m) {
    if (!stage || !m) return;

    for (const auto& h : stage->getHandles()) {
        auto id = [&](mjtObj type, const QString& suffix) {
            return mj_name2id(m, type, ("prop_" + h->id + suffix).toStdString().c_str());
        };
        h->bodyId = id(mjOBJ_BODY, "");
        int jx = id(mjOBJ_JOINT, "_x");
        h->qposAdr = jx >= 0 ? m->jnt_qposadr[jx] : -1;
        h->dofAdr  = jx >= 0 ? m->jnt_dofadr[jx] : -1;
        h->actX    = id(mjOBJ_ACTUATOR, "_vx");
        h->actY    = id(mjOBJ_ACTUATOR, "_vy");
        h->actYaw  = id(mjOBJ_ACTUATOR, "_vyaw");
    }

    int freeJoint = mj_name2id(m, mjOBJ_JOINT, "robot_freejoint");
    stage->getRobotPose()->bodyId = freeJoint >= 0 ? m->jnt_bodyid[freeJoint] : -1;
}



/*
 * Pre-step: script commands -> velocity servo targets. Local (move) commands
 * are turned into world velocities with the prop's current yaw. Teleports
 * write the joint positions directly and zero the prop's velocity.
 */
void SimulationManager::syncToMujocoProps(Stage* stage, mjModel* m, mjData* d) {
    if (!stage || !m || !d) return;

    for (const auto& h : stage->getHandles()) {
        if (h->qposAdr < 0 || h->actX < 0) continue;
        PropHandle::Command c = h->takeCommand();
        double* q = d->qpos + h->qposAdr;   // x, y, z, yaw

        if (c.teleport) {
            q[0] = c.tp[0] - h->basePos[0];
            q[1] = c.tp[1] - h->basePos[1];
            q[2] = c.tp[2] - h->basePos[2];
            q[3] = (c.tp[3] - h->baseYaw) * M_PI / 180.0;
            for (int i = 0; i < 4; ++i) d->qvel[h->dofAdr + i] = 0;
        }

        double vx = c.vx, vy = c.vy;
        if (c.local) {
            double yaw = h->baseYaw * M_PI / 180.0 + q[3];
            vx = c.vx * std::cos(yaw) - c.vy * std::sin(yaw);
            vy = c.vx * std::sin(yaw) + c.vy * std::cos(yaw);
        }
        d->ctrl[h->actX] = vx;
        d->ctrl[h->actY] = vy;
        d->ctrl[h->actYaw] = c.yawRate * M_PI / 180.0;
    }
}



// Post-step: publish prop state and the robot pose to the stage script.
void SimulationManager::syncFromMujocoProps(Stage* stage, mjModel* m, mjData* d) {
    if (!stage || !m || !d) return;

    for (const auto& h : stage->getHandles()) {
        if (h->bodyId < 0 || h->qposAdr < 0) continue;
        const double* p = d->xpos + 3 * h->bodyId;
        const double* v = d->qvel + h->dofAdr;
        h->publish({p[0], p[1], p[2]}, {v[0], v[1], v[2]},
                   h->baseYaw + d->qpos[h->qposAdr + 3] * 180.0 / M_PI);
    }

    RobotPose* robot = stage->getRobotPose();
    if (robot->bodyId >= 0) {
        const double* p = d->xpos + 3 * robot->bodyId;
        const double* q = d->xquat + 4 * robot->bodyId;
        Rotation r(q[0], q[1], q[2], q[3]);
        robot->publish({p[0], p[1], p[2]}, {r.roll(), r.pitch(), r.yaw()});
    }
}
