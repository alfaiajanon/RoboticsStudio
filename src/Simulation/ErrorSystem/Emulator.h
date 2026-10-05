#pragma once

#include <QObject>
#include "mujoco/mujoco.h"


class ComponentInstance;
class CameraRenderer;



/*
 * When the physics thread runs an emulator. Periods are in SIM time, so a
 * 30 Hz device stays 30 Hz at any time scale.
 *
 *   Synchronous : update() once per physics tick, after the sensor sync
 *                 (today's behaviour for every hobby-grade servo/IMU/motor).
 *   Throttled   : update() once per period, also after the sensor sync, on
 *                 the first tick whose sim time reaches the deadline. For
 *                 cheap low-rate devices (display refresh, ultrasonic, GPS).
 *   Async       : sample() once per period, from INSIDE the step loop at the
 *                 exact step that reaches the deadline, under physicsMutex.
 *                 sample() must only grab what it needs and hand the heavy
 *                 work to a worker -- it may never block. update() still runs
 *                 every tick, to publish what the worker finished.
 *
 * The physics thread is the only clock: an emulator never paces or waits on
 * it, and a worker that falls behind drops frames instead of slowing physics.
 */
enum class Cadence { Synchronous, Throttled, Async };

struct Schedule {
    Cadence cadence = Cadence::Synchronous;
    double periodSec = 0.0;   // Throttled / Async only
};

// What sample() gets: the live model/data (valid only during the call) and
// the services an async emulator may hand work to.
struct SampleContext {
    const mjModel* m = nullptr;
    mjData* d = nullptr;
    double simTime = 0.0;
    CameraRenderer* cameraRenderer = nullptr;
};



// Inheriting from QObject to allow subclasses to expose methods to JS
class Emulator : public QObject {
    Q_OBJECT

    protected:
        ComponentInstance* component;
        double simTime = 0.0;   // sim time of the current update(); set by SimulationManager

    public:
        explicit Emulator(ComponentInstance* comp, QObject* parent = nullptr)
            : QObject(parent), component(comp) {}

        virtual ~Emulator() = default;
        virtual void init() = 0;
        virtual void update() = 0;
        virtual void reset() = 0;

        // Read when the simulation starts (after init()).
        virtual Schedule schedule() const { return {}; }
        virtual void sample(const SampleContext&) {}

        void setSimTime(double t) { simTime = t; }
};
