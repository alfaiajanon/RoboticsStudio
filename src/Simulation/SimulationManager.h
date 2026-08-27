#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <QElapsedTimer>
#include "Utils/Global.h"
#include "mujoco/mujoco.h"

class ComponentInstance;
class Project;


enum class SimulationState {
    PAUSED,
    PLAYING,
    EDITING
};





class SimulationManager : public QObject {
    Q_OBJECT

    Project* project;

    int frameCount = 0;
    QElapsedTimer fpsTimer;
    std::thread physicsThread;
    std::thread mcuThread;

    std::atomic<bool> isAlive;
    std::atomic<SimulationState> currentState;
    std::atomic<float> timeScale;
    float stepAccumulator;

    void physicsLoop();
    void mcuLoop();

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
        void processEmulators(ComponentInstance* comp);
        void resetEmulators(ComponentInstance* comp);
        SimulationState getState() const;

        static void cacheMujocoIds(ComponentInstance* root, mjModel* m);

        static void syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d);
        static void syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d);
    
    signals:
        void fpsUpdated(int currentFps);
};