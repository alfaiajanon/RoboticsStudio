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
#include "Simulation/Controller/PinBus.h"
#include "Simulation/Controller/DeviceBus.h"
#include "Simulation/Power/PowerSystem.h"
#include <QSet>

class ComponentInstance;
class Project;
class Stage;
class RobotController;
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
    double timeScale;       // selected scale; <= 0 = as fast as possible
    double realTimeFactor;  // sim seconds per wall second over the last window
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
    double fpsWindowSimStart = 0.0;   // sim time when the current fps window began
    std::thread physicsThread;
    std::thread firmwareThread;  // runs the robot's firmware
    std::thread stageThread;   // runs the stage script, same loop as the MCU

    std::chrono::steady_clock::time_point lastTickTime;

    std::atomic<bool> isAlive;
    std::atomic<SimulationState> currentState;
    std::atomic<float> timeScale;   // <= 0: as fast as possible

    // Sim seconds the pacing allows ahead of the steps taken (real-time modes).
    double simBudget = 0.0;

    // Held by the physics thread for one whole step (controller handoff,
    // camera wait, mj_step); play()/edit() take it to change the schedule and
    // the scripts between steps. Lock order: stepMutex -> physicsMutex.
    std::mutex stepMutex;

    // The robot's controller for this run (JS or firmware), chosen at play();
    // the physics thread steps it. Changed under stepMutex.
    RobotController* robotController = nullptr;

    // Telemetry is captured on a fixed sim-time period, not per step.
    static constexpr double kTelemetryPeriod = 0.01;
    double nextTelemetryDue = 0.0;   // physics thread (and play(), under stepMutex)

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

    // The pins between the robot's controller and its parts, rebuilt at Play.
    PinBus pinBus;
    DeviceBus deviceBus;   // the board's I2C/SPI buses, rebuilt at Play
    // Real level: parts with no supply or ground (set at Play): their device
    // models don't run and their actuators get no drive, like a dead part.
    QSet<int> unpoweredUids;

    // Real level: the power network during the run (physics thread). Parts
    // whose supply sags below their min_v are off: not updated, no drive,
    // silent on the buses (offEmulators); deadUids = unpowered + off.
    PowerSystem power;
    QSet<Emulator*> offEmulators;
    QSet<int> deadUids;
    QHash<int, int> powerCutsLogged;   // uid -> power cuts reported (capped)
    QHash<int, int> resetsLogged;      // board uid -> brownout resets reported (capped)
    void startPower(ComponentInstance* root);
    void applyPower(double now, double dt);
    void setPartPowered(ComponentInstance* comp, bool powered, double now = 0.0);

    // Renders camera frames off the physics thread; created on the UI thread
    // (GLFW windows must be), lives as long as the manager.
    CameraRenderer* cameraRenderer = nullptr;

    // Display frames -> screen quad textures, for every GL context below.
    DisplayTextures* displayTextures = nullptr;

    void physicsLoop();
    bool stepOnce();   // one lockstep mj_step; false if no longer playing
    void mcuLoop(RobotController* controller);
    void startThreads();

    void buildSchedule(ComponentInstance* root);
    void clearSchedule();
    void collectEmulators(ComponentInstance* comp, double now, double minPeriod);
    void runDueAsync(double now);
    void runEmulators(double now);

    SimStatus status{};

    public:
        std::mutex physicsMutex;

        SimulationManager(Project* project);
        ~SimulationManager();

        void play();
        void edit();
        void pause();    // freeze a running simulation (physics + scripts)
        void resume();   // continue a paused one without recompiling
        void trackFps();
        // Sim seconds per wall second; <= 0 runs as fast as possible.
        void setTimeScale(float scale);
        float getTimeScale() const { return timeScale.load(); }

        void pushTelemetry(ComponentInstance* comp, double time);
        void initEmulators(ComponentInstance* comp);
        void resetEmulators(ComponentInstance* comp);
        SimulationState getState() const;

        MujocoContext* getMujocoContext() { return &mujocoContext; }
        CameraRenderer* getCameraRenderer() { return cameraRenderer; }
        PinBus* getPinBus() { return &pinBus; }
        DeviceBus* getDeviceBus() { return &deviceBus; }
        // Real level, physics thread (telemetry capture): the run's power state.
        const PowerSystem& getPowerSystem() const { return power; }

        // Swaps in a new model and re-resolves every signal's MuJoCo id. Takes
        // physicsMutex itself. The single entry point for model reloads, so the
        // camera renderer can drop work that refers to the old model.
        bool reloadModel(const QString& xml, ComponentInstance* root);

        static void cacheMujocoIds(ComponentInstance* root, mjModel* m);

        static void syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d);
        // dead: parts that drive nothing (unpowered at the Real level)
        static void syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d, const QSet<int>& dead);
        static void syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d);

        // Stage props + robot pose (stage script side), see Stage.h.
        static void cacheStageIds(Stage* stage, mjModel* m);
        static void syncToMujocoProps(Stage* stage, mjModel* m, mjData* d);
        static void syncFromMujocoProps(Stage* stage, mjModel* m, mjData* d);

    signals:
        // void fpsUpdated(int currentFps);
        void statusUpdated(const SimStatus& status);
};
