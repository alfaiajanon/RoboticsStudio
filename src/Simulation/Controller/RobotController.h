#pragma once

#include "Simulation/Controller/LockstepGate.h"


/*
 * Something that runs in lockstep with the physics on its own thread: the
 * robot's JS script (MicroController), later its firmware, and the stage
 * script. SimulationManager drives every controller the same way:
 *   - its thread calls runCycle() in a loop;
 *   - each physics step calls runStep(), which releases it through the gate;
 *   - stop() interrupts it.
 * How code is loaded differs per runtime, so that stays on the subclasses.
 */
class RobotController {
    protected:
        LockstepGate gate;

    public:
        // abort: thread-safe interrupt of the running code (the watchdog's).
        RobotController(const QString& tag, std::function<void()> abort) : gate(tag, std::move(abort)) {}
        virtual ~RobotController() = default;

        // Controller thread: one call (setup() first, then loop()) once
        // released. False (after a short wait) while not released, so the
        // caller can check for shutdown.
        virtual bool runCycle() = 0;

        // Any thread: abort the running code and wake a blocked delay().
        virtual void stop() = 0;

        // Physics thread: run the controller for the step [simTime, simTime + dt).
        void runStep(double simTime, double dt) { gate.runStep(simTime, dt); }

        // For emulators whose script-facing calls take sim time (a bus transfer).
        LockstepGate* getGate() { return &gate; }
};
