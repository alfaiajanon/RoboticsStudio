#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <QElapsedTimer>
#include "Utils/Global.h"
#include <chrono>
#include "mujoco/mujoco.h"
#include "Simulation/MujocoContext.h"

class ComponentInstance;
class Project;
class Emulator;
class CameraRenderer;
class DisplayTextures;


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

    // Emulator schedule, flattened from the component tree at play() (see
    // Cadence in Emulator.h). Only the physics thread reads it while PLAYING;
    // it is rebuilt under physicsMutex.
    struct ScheduledEmulator {
        Emulator* emulator;
        double periodSec;
        double nextDue;
    };
    std::vector<Emulator*> syncEmulators;
    std::vector<ScheduledEmulator> throttledEmulators;
    std::vector<ScheduledEmulator> asyncEmulators;
    double nextAsyncDue = 0.0;   // min nextDue over asyncEmulators

    // Renders camera frames off the physics thread; created on the UI thread
    // (GLFW windows must be), lives as long as the manager.
    CameraRenderer* cameraRenderer = nullptr;

    // Display frames -> screen quad textures, for every GL context below.
    DisplayTextures* displayTextures = nullptr;

    void physicsLoop();
    void mcuLoop();

    void buildSchedule(ComponentInstance* root);
    void clearSchedule();
    void collectEmulators(ComponentInstance* comp, double now, double minPeriod);
    void runDueAsync(double now);
    void runEmulators(double now);

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
        void resetEmulators(ComponentInstance* comp);
        SimulationState getState() const;

        MujocoContext* getMujocoContext() { return &mujocoContext; }
        CameraRenderer* getCameraRenderer() { return cameraRenderer; }

        // Swaps in a new model and re-resolves every signal's MuJoCo id. Takes
        // physicsMutex itself. The single entry point for model reloads, so the
        // camera renderer can drop work that refers to the old model.
        bool reloadModel(const QString& xml, ComponentInstance* root);

        static void cacheMujocoIds(ComponentInstance* root, mjModel* m);

        static void syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d);

    signals:
        // void fpsUpdated(int currentFps);
        void statusUpdated(const SimStatus& status);
};
