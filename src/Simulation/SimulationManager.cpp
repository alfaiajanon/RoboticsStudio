#include "SimulationManager.h"
#include "MujocoContext.h"

#include <chrono>
#include <qobject.h>
#include "Utils/Log.h"
#include "Document/Project.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "mujoco/mjdata.h"





/*
 * Initializes the simulation manager and spawns the background physics thread.
 * Starts in a stopped state with a default 1.0x real-time scale.
 */
SimulationManager::SimulationManager(Project* project): project(project),
                                                        isAlive(true),
                                                        currentState(SimulationState::PAUSED),
                                                        timeScale(1.0f),
                                                        stepAccumulator(0.0f) {}





/*
 * Safely signals the physics thread to terminate and waits for it to join.
 * Ensures clean memory cleanup when the application closes.
 */
SimulationManager::~SimulationManager() {
    isAlive = false;

    MicroController* mcu = project->getMicroController();
    mcu->stop();

    if (mcuThread.joinable()) {
        mcuThread.join();
    }
    if (physicsThread.joinable()) {
        physicsThread.join();
    }
}





/*
 * Following functions are the public API for controlling the simulation state and time scale.
 */
#pragma region play/edit

void SimulationManager::play() {

    ComponentInstance* root = project->getRootComponent();
    initEmulators(root);

    if (currentState == SimulationState::PAUSED) {
        if(!physicsThread.joinable()) {
            physicsThread = std::thread(&SimulationManager::physicsLoop, this);
        }
        if(!mcuThread.joinable()) {
            mcuThread = std::thread(&SimulationManager::mcuLoop, this);
        }
    }

    // move joint data to actuator targets before starting simulation
    #pragma region todo

    MicroController* mcu = project->getMicroController();
    mcu->compile(project->getScript(), project->getRootComponent());

    currentState = SimulationState::PLAYING;
}



void SimulationManager::edit() {
    if (currentState == SimulationState::PAUSED) {
        if(!physicsThread.joinable()){
            physicsThread = std::thread(&SimulationManager::physicsLoop, this);
        }
        if (!mcuThread.joinable()) {
            mcuThread = std::thread(&SimulationManager::mcuLoop, this);
        }
    }

    MujocoContext* mj = &mujocoContext;
    MicroController* mcu = project->getMicroController();
    mcu->stop();

    ComponentInstance* root = project->getRootComponent();
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


void SimulationManager::mcuLoop() {
    MicroController* mcu = project->getMicroController();

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
                    for(int i = 0; i < stepsToTake; ++i) {
                        mj->step();
                    }
                    syncFromMujocoSensor(root, mj->getModel(), mj->getData());

                    processEmulators(root); // position it here to capture both sensor and actuator properly

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



void SimulationManager::processEmulators(ComponentInstance* comp) {
    if (!comp) return;
    if (comp->emulator) {
        comp->emulator->update();
    }
    for (ComponentInstance* child : comp->children) {
        processEmulators(child);
    }
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

static bool isDegreeUnit(const QString& unit) {
    QString u = unit.toLower();
    return u == "degree" || u == "deg" || u == "degrees";
}


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
 *   emulator / camera / display kinds have no MuJoCo id and are left at -1.
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

                if (isDegreeUnit(it.value().unit)) {
                    val = val * (M_PI / 180.0);
                }

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

                if (isDegreeUnit(def.unit)) {
                    targetVal = targetVal * (M_PI / 180.0);
                }

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
                    bool isDegree = isDegreeUnit(def.unit);

                    if (dim > 1 || bp->interfaceShape(def) == SignalShape::Vector) {
                        std::vector<double> vec(dim);
                        for (int i = 0; i < dim; ++i) {
                            double val = d->sensordata[adr + i];
                            if (isDegree) val *= (180.0 / M_PI);
                            vec[i] = val;
                        }
                        data.data = vec;
                        root->setSensorValue(key, data);
                    } else {
                        double val = d->sensordata[adr];
                        if (isDegree) val *= (180.0 / M_PI);

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
