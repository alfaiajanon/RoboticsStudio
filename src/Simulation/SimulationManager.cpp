#include "SimulationManager.h"
#include "Core/Application.h"
#include "MujocoContext.h"
#include "Core/Log.h"
#include <chrono>





/*
 * Initializes the simulation manager and spawns the background physics thread.
 * Starts in a stopped state with a default 1.0x real-time scale.
 */
SimulationManager::SimulationManager() : isAlive(true), currentState(SimulationState::PAUSED), timeScale(1.0f), stepAccumulator(0.0f) {}





/*
 * Safely signals the physics thread to terminate and waits for it to join.
 * Ensures clean memory cleanup when the application closes.
 */
SimulationManager::~SimulationManager() {
    isAlive = false;
    
    Project* proj = Application::getInstance()->getProject();
    MicroController* mcu = proj->getMicroController();
    mcu->stop();

    if (mcuThread.joinable()) {
        mcuThread.join();
    }
    if (physicsThread.joinable()) {
        physicsThread.join();
    }
}





/*
 *  Following functions are the public API for controlling the simulation state and time scale.
 */
#pragma region play/edit

void SimulationManager::play() {
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

    Project* proj = Application::getInstance()->getProject();
    MicroController* mcu = proj->getMicroController();
    mcu->compile(proj->getScript(), proj->getRootComponent());

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

    Project* proj = Application::getInstance()->getProject();
    MujocoContext* mj = MujocoContext::getInstance();
    MicroController* mcu = proj->getMicroController();
    mcu->stop(); 

    ComponentInstance* root = proj->getRootComponent();
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
    Project* proj = Application::getInstance()->getProject();
    MicroController* mcu = proj->getMicroController();

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
    while (isAlive) {
        if (currentState == SimulationState::PLAYING) {
            
            stepAccumulator += (8.0f * timeScale.load());
            int stepsToTake = static_cast<int>(stepAccumulator);
            stepAccumulator -= stepsToTake;

            if (stepsToTake > 0) {
                Project* proj = Application::getInstance()->getProject();
                MujocoContext* mj = MujocoContext::getInstance();
                ComponentInstance* root = proj->getRootComponent();

                processEmulators(root);

                {
                    std::lock_guard<std::mutex> lock(physicsMutex);
                    
                    syncToMujocoActuator(root, mj->getModel(), mj->getData());
                    for(int i = 0; i < stepsToTake; ++i) {
                        mj->step();
                    }
                    syncFromMujocoSensor(root, mj->getModel(), mj->getData());
                    
                    storePlotData(mj->getData());
                } 
            }
        } 
        
        else if (currentState == SimulationState::EDITING) {
            // In editing mode, we sync joint positions to allow dragging components
            std::lock_guard<std::mutex> lock(physicsMutex);
            
            Project* proj = Application::getInstance()->getProject();
            MujocoContext* mj = MujocoContext::getInstance();
            ComponentInstance* root = proj->getRootComponent();

            syncToMujocoJoint(root, mj->getModel(), mj->getData());
            mj->forward(); 

            syncFromMujocoSensor(root, mj->getModel(), mj->getData());
        }

        trackFps(); 
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
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




// In your header file: void storePlotData(mjData* d);

void SimulationManager::storePlotData(mjData* d) {
    Project* proj = Application::getInstance()->getProject();
    QList<PlotTarget> target = proj->getActivePlotsVal();
    
    for(const PlotTarget& t : target) {
        ComponentInstance* comp = proj->getComponentByUid(t.compUid);
        if(!comp) continue;

        RingBuffer<PlotPoint>* buffer = nullptr;
        double currentVal = 0.0;

        if (t.type == PlotTargetType::SENSOR) {
            buffer = comp->getSensorBuffer(t.ioKey);
            currentVal = comp->getSensorCurrent(t.ioKey);
        } else {
            buffer = comp->getActuatorBuffer(t.ioKey);
            currentVal = comp->getActuatorTarget(t.ioKey);
        }

        if(buffer){
            buffer->push(PlotPoint{d->time, currentVal});
        }
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
            QString actuatorName = prefix + root->blueprint->inputDefs[key].name;
            int id = mj_name2id(m, mjOBJ_ACTUATOR, actuatorName.toStdString().c_str());
            // Log::info("Caching Actuator: " + actuatorName + " --> ID: " + QString::number(id));
            root->setMujocoActuatorId(key, id);
        }

        for(const QString& key : root->blueprint->inputDefs.keys()) {
            QString jkey = root->blueprint->inputDefs[key].targetJoint;
            QString jointName = prefix + jkey;
            int id = mj_name2id(m, mjOBJ_JOINT, jointName.toStdString().c_str());
            // Log::info("Caching Joint: " + jointName + " --> ID: " + QString::number(id));
            root->setMujocoJointId(jkey, id);
        }
        
        for (const QString& key : root->blueprint->outputDefs.keys()) {
            QString sensorName = prefix + root->blueprint->outputDefs[key].name;
            int id = mj_name2id(m, mjOBJ_SENSOR, sensorName.toStdString().c_str());
            // Log::info("Caching Sensor: " + sensorName + " --> ID: " + QString::number(id));
            root->setMujocoSensorId(key, id);
        }
    }
    
    for (ComponentInstance* child : root->children) {
        cacheMujocoIds(child, m);
    }
}





/*
 * Pre-step hook to push live input commands into the engine.
 * Safely writes actuator targets from the component tree into d->ctrl.
 */
void SimulationManager::syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->inputDefs.keys()) {
            int mujocoId = root->getMujocoActuatorId(key);
            if (mujocoId >= 0 && mujocoId < m->nu) {
                double val = root->getActuatorTarget(key);
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
                double targetVal = root->getJointTarget(jkey);
                QString unit = root->blueprint->inputDefs[key].unit.toLower();

                if (unit == "degree" || unit == "deg" || unit == "degrees") {
                    targetVal = targetVal * (M_PI / 180.0);
                }

                int qposIndex = m->jnt_qposadr[mujocoId];
                double currentPos = d->qpos[qposIndex];
                
                // Lower = Slower/Smoother. Higher = Faster/Snappier.
                double smoothSpeed = 0.3; 
                
                if (std::abs(targetVal - currentPos) > 0.0001) {
                    double newPos = currentPos + (targetVal - currentPos) * smoothSpeed; // The Lerp Formula
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
 * Safely extracts sensor readings from d->sensordata and updates the components.
 */

void SimulationManager::syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (root->blueprint) {
        for (const QString& key : root->blueprint->outputDefs.keys()) {
            int sensorId = root->getMujocoSensorId(key);
            
            if (sensorId >= 0 && sensorId < m->nsensor) {
                int adr = m->sensor_adr[sensorId];
                int axisOffset = root->blueprint->outputDefs[key].axis; 

                if (adr + axisOffset < m->nsensordata) {
                    double val = d->sensordata[adr + axisOffset];
                    QString unit = root->blueprint->outputDefs[key].unit.toLower();

                    if (unit == "degree" || unit == "deg" || unit == "degrees") {
                        val = val * (180.0 / M_PI);
                    }

                    root->setSensorCurrent(key, val);
                }
            }
        }
    }
    
    for (ComponentInstance* child : root->children) {
        syncFromMujocoSensor(child, m, d);
    }
}