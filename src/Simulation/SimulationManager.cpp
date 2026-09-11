#include "SimulationManager.h"
#include "MujocoContext.h"

#include <chrono>
#include "Utils/Log.h"
#include "Document/Project.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"





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
        emit fpsUpdated(currentFps);

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

                    pushTelemetry(root, mj->getData()->time);
                    TelemetryRegistry::getInstance().captureAll(mj->getModel(), mj->getData(), mj->getData()->time);
                }
            }
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




void SimulationManager::pushTelemetry(ComponentInstance* comp, double time) {
    if (!comp || !comp->blueprint) return;

    auto& registry = TelemetryRegistry::getInstance();

    for (const QString& key : comp->blueprint->inputDefs.keys()) {
        int channelId = comp->getActuatorChannelId(key);
        if (channelId != -1) {
            IOData data = comp->getActuatorTarget(key);
            QString cType = comp->blueprint->inputDefs[key].channelType;

            if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
                if (auto channel = registry.getVector(channelId)) {
                    channel->push(time, std::get<std::vector<double>>(data));
                }
            } else if (std::holds_alternative<double>(data)) {
                if (auto channel = registry.getScalar(channelId)) {
                    channel->push(time, std::get<double>(data));
                }
            }
        }
    }

    for (const QString& key : comp->blueprint->outputDefs.keys()) {
        int channelId = comp->getSensorChannelId(key);
        if (channelId != -1) {
            IOData data = comp->getSensorCurrent(key);
            QString cType = comp->blueprint->outputDefs[key].channelType;

            if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
                if (auto channel = registry.getVector(channelId)) {
                    channel->push(time, std::get<std::vector<double>>(data));
                }
            } else if (std::holds_alternative<double>(data)) {
                if (auto channel = registry.getScalar(channelId)) {
                    channel->push(time, std::get<double>(data));
                }
            }
        }
    }

    for (ComponentInstance* child : comp->children) {
        pushTelemetry(child, time);
    }
}




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
 * One-time setup to map string names to MuJoCo's internal C-array indices.
 * Traverses the component tree recursively to cache actuator and sensor IDs.
 */
void SimulationManager::cacheMujocoIds(ComponentInstance* root, mjModel* m) {
    if (!m || !root) return;

    QString prefix = "comp_" + QString::number(root->uid) + "_";

    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            QString targetJoint = root->blueprint->inputDefs[key].targetJoint;
            QString actuatorName = prefix + targetJoint + "_actuator";
            int id = mj_name2id(m, mjOBJ_ACTUATOR, actuatorName.toStdString().c_str());
            root->setMujocoActuatorId(key, id);
        }

        for(const QString& key : root->blueprint->inputDefs.keys()) {
            QString jkey = root->blueprint->inputDefs[key].targetJoint;
            QString jointName = prefix + jkey;
            int id = mj_name2id(m, mjOBJ_JOINT, jointName.toStdString().c_str());
            root->setMujocoJointId(jkey, id);
        }

        for (const QString& key : root->blueprint->outputDefs.keys()) {
            // FIXED: Fallback to targetSite if targetJoint is empty (Crucial for IMU)
            const IODef& def = root->blueprint->outputDefs[key];
            QString target = def.targetJoint.isEmpty() ? def.targetSite : def.targetJoint;
            QString sensorName = prefix + target + "_sensor";
            int id = mj_name2id(m, mjOBJ_SENSOR, sensorName.toStdString().c_str());
            root->setMujocoSensorId(key, id);
        }
    }

    for (ComponentInstance* child : root->children) {
        cacheMujocoIds(child, m);
    }
}




/*
 * Pre-step hook to push live input commands into the engine.
 * Safely extracts the double from IOData and writes to d->ctrl.
 */
void SimulationManager::syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            int mujocoId = root->getMujocoActuatorId(key);
            if (mujocoId >= 0 && mujocoId < m->nu) {

                IOData data = root->getActuatorTarget(key);
                double val = std::holds_alternative<double>(data) ? std::get<double>(data) : 0.0;

                QString unit = root->blueprint->inputDefs[key].unit.toLower();
                if (unit == "degree" || unit == "deg" || unit == "degrees") {
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






void SimulationManager::syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            QString jkey = root->blueprint->inputDefs[key].targetJoint;
            int mujocoId = root->getMujocoJointId(jkey);

            if (mujocoId >= 0 && mujocoId < m->njnt) {

                IOData data = root->getJointTarget(jkey);
                double targetVal = std::holds_alternative<double>(data) ? std::get<double>(data) : 0.0;

                QString unit = root->blueprint->inputDefs[key].unit.toLower();
                if (unit == "degree" || unit == "deg" || unit == "degrees") {
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
 * Safely extracts scalars or vectors based on dimension, packing them into IOData.
 */
void SimulationManager::syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->outputDefs.keys()) {
            int sensorId = root->getMujocoSensorId(key);

            if (sensorId >= 0 && sensorId < m->nsensor) {
                int adr = m->sensor_adr[sensorId];
                int dim = root->blueprint->outputDefs[key].dim;
                QString channelType = root->blueprint->outputDefs[key].channelType;

                if (adr + dim <= m->nsensordata) {
                    QString unit = root->blueprint->outputDefs[key].unit.toLower();
                    bool isDegree = (unit == "degree" || unit == "deg" || unit == "degrees");

                    if (channelType == "vector" || dim > 1) {
                        std::vector<double> vec(dim);
                        for (int i = 0; i < dim; ++i) {
                            double val = d->sensordata[adr + i];
                            if (isDegree) val *= (180.0 / M_PI);
                            vec[i] = val;
                        }
                        IOData data = vec;
                        root->setSensorCurrent(key, data);
                    } else {
                        double val = d->sensordata[adr];
                        if (isDegree) val *= (180.0 / M_PI);

                        IOData data = val;
                        root->setSensorCurrent(key, data);
                    }
                }
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncFromMujocoSensor(child, m, d);
    }
}
