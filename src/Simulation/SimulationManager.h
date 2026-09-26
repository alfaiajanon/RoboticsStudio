#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <QElapsedTimer>
#include "Utils/Global.h"
#include <chrono>
#include "mujoco/mujoco.h"
#include "Simulation/MujocoContext.h"

class ComponentInstance;
class Project;


enum class SimulationState {
    PAUSED,
    PLAYING,
    EDITING
};

struct SimStatus {
    int fps;
    double simTime;
    double timeScale;
    int activeContacts; // mjData->ncon, read alongside fps/simTime in trackFps
    int invasiveCollisions;
};





class SimulationManager : public QObject {
    Q_OBJECT

    Project* project;

    // The app's MuJoCo model/scene lives here (no longer a global singleton)
    // so auxiliary viewers can own separate contexts without clobbering the
    // running simulation. One manager per app lifetime -- Application keeps
    // this alive across project (re)loads, so the context outlives them too.
    MujocoContext mujocoContext;

    int frameCount = 0;
    QElapsedTimer fpsTimer;
    std::thread physicsThread;
    std::thread mcuThread;

    std::chrono::steady_clock::time_point lastTickTime;

    std::atomic<bool> isAlive;
    std::atomic<SimulationState> currentState;
    std::atomic<float> timeScale;

    float stepAccumulator;

    void physicsLoop();
    void mcuLoop();

    SimStatus status;

    public:
        std::mutex physicsMutex;

        SimulationManager(Project* project);
        ~SimulationManager();

        void play();
        void edit();
        void pause();
        void trackFps();
        void setTimeScale(float scale);

        void pushTelemetry(ComponentInstance* comp, double time);
        void initEmulators(ComponentInstance* comp);
        void processEmulators(ComponentInstance* comp);
        void resetEmulators(ComponentInstance* comp);
        SimulationState getState() const;

        MujocoContext* getMujocoContext() { return &mujocoContext; }

        static void cacheMujocoIds(ComponentInstance* root, mjModel* m);

        static void syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d);

    signals:
        // void fpsUpdated(int currentFps);
        void statusUpdated(const SimStatus& status);
};
