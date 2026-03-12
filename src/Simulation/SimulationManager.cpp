#include "SimulationManager.h"
#include "Core/Application.h"
#include "MujocoContext.h"
#include "Core/Log.h"
#include <chrono>





/*
 * Initializes the simulation manager and spawns the background physics thread.
 * Starts in a stopped state with a default 1.0x real-time scale.
 */
SimulationManager::SimulationManager() : isAlive(true), currentState(SimulationState::STOPPED), timeScale(1.0f), stepAccumulator(0.0f) {}





/*
 * Safely signals the physics thread to terminate and waits for it to join.
 * Ensures clean memory cleanup when the application closes.
 */
SimulationManager::~SimulationManager() {
    isAlive = false;
    if (physicsThread.joinable()) {
        physicsThread.join();
    }
}





/*
 * Transitions the simulation into the active playing state.
 * Allows the background thread to begin executing physics steps.
 */
void SimulationManager::play() {
    if (currentState == SimulationState::STOPPED && !physicsThread.joinable()) {
        physicsThread = std::thread(&SimulationManager::physicsLoop, this);
    }
    currentState = SimulationState::PLAYING;
}





/*
 * Pauses the simulation while preserving the current kinematic state.
 * The background thread remains alive but skips physics calculations.
 */
void SimulationManager::pause() {
    currentState = SimulationState::PAUSED;
}





/*
 * Completely stops the simulation and prepares for a timeline reset.
 * Halts physics execution and clears the fractional step accumulator.
 */
void SimulationManager::stop() {
    currentState = SimulationState::STOPPED;
    stepAccumulator = 0.0f;
    // TODO: Later, call a reset function here to snap the robot back to origin
}





/*
 * Adjusts the speed multiplier of the physics execution.
 * 1.0 is real-time, 0.5 is slow-motion, and 2.0 is fast-forward.
 */
void SimulationManager::setTimeScale(float scale) {
    timeScale = scale;
}





/*
 * Returns the current execution state of the simulation engine.
 */
SimulationState SimulationManager::getState() const {
    return currentState.load();
}





/*
 * The continuous background physics loop.
 * Calculates scaled timesteps, locks the engine, processes math, and yields.
 */
void SimulationManager::physicsLoop() {
    while (isAlive) {
        if (currentState == SimulationState::PLAYING) {
            
            stepAccumulator += (8.0f * timeScale.load());
            int stepsToTake = static_cast<int>(stepAccumulator);
            stepAccumulator -= stepsToTake;

            if (stepsToTake > 0) {
                std::lock_guard<std::mutex> lock(physicsMutex);
                
                Project* proj = Application::getInstance()->getProject();
                MujocoContext* mj = MujocoContext::getInstance();
                ComponentInstance* root = proj->getRootComponent();

                syncToMujoco(root, mj->getModel(), mj->getData());
                
                for(int i = 0; i < stepsToTake; ++i) {
                    mj->step();
                }
                
                syncFromMujoco(root, mj->getModel(), mj->getData());
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}





/*
 * One-time setup to map string names to MuJoCo's internal C-array indices.
 * Traverses the component tree recursively to cache actuator and sensor IDs.
 */
void SimulationManager::cacheMujocoIds(ComponentInstance* root, mjModel* m) {
    if (!m || !root) return;

    QString prefix = "comp_" + QString::number(root->uid) + "_";
    
    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            QString actuatorName = prefix + root->blueprint->inputDefs[key].name;
            int id = mj_name2id(m, mjOBJ_ACTUATOR, actuatorName.toStdString().c_str());
            Log::info("Caching Actuator: " + actuatorName + " --> ID: " + QString::number(id));
            root->setMujocoId(key, id, true);
        }
        
        for (const QString& key : root->blueprint->outputDefs.keys()) {
            QString sensorName = prefix + root->blueprint->outputDefs[key].name;
            int id = mj_name2id(m, mjOBJ_SENSOR, sensorName.toStdString().c_str());
            Log::info("Caching Sensor: " + sensorName + " --> ID: " + QString::number(id));
            root->setMujocoId(key, id, false);
        }
    }
    
    for (ComponentInstance* child : root->children) {
        cacheMujocoIds(child, m);
    }
}





/*
 * Pre-step hook to push user or script commands into the physics engine.
 * Safely extracts target values from components and injects them into d->ctrl.
 */
void SimulationManager::syncToMujoco(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            int mujocoId = root->getMujocoId(key, true);
            if (mujocoId >= 0 && mujocoId < m->nu) {
                double val = root->getInputTarget(key);
                QString unit = root->blueprint->inputDefs[key].unit.toLower();

                if (unit == "degree" || unit == "deg" || unit == "degrees") {
                    val = val * (M_PI / 180.0);
                }

                d->ctrl[mujocoId] = val;
            }
        }
    }
    
    for (ComponentInstance* child : root->children) {
        syncToMujoco(child, m, d);
    }
}





/*
 * Post-step hook to pull live physics telemetry out of the engine.
 * Safely extracts sensor readings from d->sensordata and updates the components.
 */
void SimulationManager::syncFromMujoco(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->outputDefs.keys()) {
            int mujocoId = root->getMujocoId(key, false);
            if (mujocoId >= 0 && mujocoId < m->nsensordata) {
                double val = d->sensordata[mujocoId];
                QString unit = root->blueprint->outputDefs[key].unit.toLower();

                if (unit == "degree" || unit == "deg" || unit == "degrees") {
                    val = val * (180.0 / M_PI);
                }

                root->setOutputCurrent(key, val);
            }
        }
    }
    
    for (ComponentInstance* child : root->children) {
        syncFromMujoco(child, m, d);
    }
}