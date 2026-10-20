#pragma once

#include <QObject>
#include "mujoco/mujoco.h"


class ComponentInstance;
class CameraRenderer;
class PinBus;
class DeviceBus;



/*
 * When the physics thread runs an emulator. Periods are in SIM time, so a
 * 30 Hz device stays 30 Hz at any time scale.
 *
 *   Synchronous : update() once per physics tick, after the sensor sync
 *                 (today's behaviour for every hobby-grade servo/IMU/motor).
 *   Throttled   : update() once per period, also after the sensor sync, on
 *                 the first tick whose sim time reaches the deadline. For
 *                 cheap low-rate devices (display refresh, ultrasonic, GPS).
 *   Async       : sample() once per period, right after the step that
 *                 reaches the deadline, under physicsMutex. sample() must only
 *                 grab what it needs and hand the heavy work to a worker -- it
 *                 may never block. Before each step (outside physicsMutex)
 *                 awaitDue() waits for the worker results due by the end of
 *                 that step, so delivery depends on sim time only, never on
 *                 how fast the host renders. update() still runs every tick,
 *                 to publish what the worker finished.
 *
 * "Tick" is one mj_step: synchronous emulators run after every step.
 * Sim time is the only clock: a slow worker slows the simulation down (lower
 * real-time factor) instead of changing what the firmware sees.
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



// A part's device model. A QObject for ownership and Qt's type system; the
// firmware reaches the part through its pins and the board's buses, not
// through methods on this object.
class Emulator : public QObject {
    Q_OBJECT

    protected:
        ComponentInstance* component;
        double simTime = 0.0;   // sim time of the current update(); set by SimulationManager
        // The run's pins (set before init()): a part reads the pins it
        // receives on, the controller writes them.
        PinBus* pins = nullptr;
        // The board's I2C/SPI buses (set before init()): bus parts register here.
        DeviceBus* buses = nullptr;
        // The live model/data during update() (physics thread, physicsMutex
        // held); read-only. For models that need the mechanism's state, e.g.
        // the effective inertia a stepper drives.
        const mjModel* physicsModel = nullptr;
        const mjData* physicsData = nullptr;
        quint64 instanceSeed = 0;   // set before init(); see seed()
        // Real level: the voltage on the part's supply pin, set before each
        // update() by the power model. Negative = ideal power (Prototype, or
        // a part without a supply pin): use the part's supply_voltage parameter.
        double supplyVolts = -1.0;

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
        // Async only. Physics thread, outside physicsMutex: wait for worker
        // results that become visible by sim time `until`.
        virtual void awaitDue(double until) { (void)until; }

        void setSimTime(double t) { simTime = t; }
        void setPins(PinBus* p) { pins = p; }
        void setBuses(DeviceBus* b) { buses = b; }
        void setPhysics(const mjModel* m, const mjData* d) { physicsModel = m; physicsData = d; }
        void setSeed(quint64 s) { instanceSeed = s; }
        void setSupply(double volts) { supplyVolts = volts; }

        // Current drawn from the supply pin on top of the part's static load
        // (a motor's), A, as of the last update(). Read by the power model.
        virtual double supplyCurrent() const { return 0.0; }
        // The part's supply went away: it loses its state and starts from its
        // power-on state when the supply returns (init() is not called again).
        virtual void powerCut() { reset(); }
        // The supply is back at sim time `now` (after a powerCut()).
        virtual void powerRestored(double now) { (void)now; }

        // Reproducible per-instance seed (from the project seed and the uid);
        // seed every random generator from it in init(), never from random_device.
        quint64 seed() const { return instanceSeed; }
};
