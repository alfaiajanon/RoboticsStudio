#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include "mujoco/mujoco.h"
#include "Simulation/Components/ComponentInstance.h"

enum class SimulationState {
    STOPPED,
    PAUSED,
    PLAYING
};

class SimulationManager {
private:
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
    void pause();
    void stop();
    void setTimeScale(float scale);
    SimulationState getState() const;

    static void cacheMujocoIds(ComponentInstance* root, mjModel* m);
    static void syncToMujoco(ComponentInstance* root, mjModel* m, mjData* d);
    static void syncFromMujoco(ComponentInstance* root, mjModel* m, mjData* d);
};