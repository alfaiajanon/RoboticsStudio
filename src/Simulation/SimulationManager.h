#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include "mujoco/mujoco.h"
#include "Simulation/Components/ComponentInstance.h"

enum class SimulationState {
    PAUSED,
    PLAYING,
    EDITING
};

class SimulationManager {
private:
    int counter = 0;
    std::thread physicsThread;
    std::atomic<bool> isAlive;
    std::atomic<SimulationState> currentState;
    std::atomic<float> timeScale;
    float stepAccumulator;

    void physicsLoop();

public:
    std::mutex physicsMutex;

    SimulationManager();
    ~SimulationManager();
    
    void play();
    void edit();
    void pause();
    void setTimeScale(float scale);
    void storePlotData(mjData* d);
    void processEmulators(ComponentInstance* comp);
    void resetEmulators(ComponentInstance* comp);
    SimulationState getState() const;

    static void cacheMujocoIds(ComponentInstance* root, mjModel* m);

    static void syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d);
    static void syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d);
    static void syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d);
};