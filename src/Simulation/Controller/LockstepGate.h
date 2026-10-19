#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <QString>


/*
 * Runs a controller (the robot's script or firmware, the stage script) in
 * lockstep with the physics: sim time is the only clock.
 *
 * The controller thread and the physics thread hand control back and forth
 * through this gate, so only one of them runs at a time:
 *   - runStep() (physics thread) releases the controller for the step that
 *     starts at simTime and blocks until it yields past that step;
 *   - the controller yields in wait() (delay()), or when its charged run time
 *     (see the costs below) crosses the end of the step.
 * Controller code itself takes no sim time; only the charged costs and wait()
 * advance its clock. A controller that never yields is stopped by a watchdog,
 * which calls the abort callback (the runtime's interrupt) from the physics
 * thread.
 *
 * States: Idle (not started, stopped or errored: physics never waits for it),
 * Waiting (blocked until sim time reaches wakeTime), Running.
 */
class LockstepGate {
    public:
        // Sim time charged per call, so polling loops advance (seconds).
        static constexpr double kTimeCallCost = 4e-6;   // millis(), micros(), delay()
        static constexpr double kLoopCost = 10e-6;      // one loop() return
        // Wall time a released controller may run without yielding.
        static constexpr std::chrono::milliseconds kWatchdog{2000};

    private:
        enum class State { Idle, Waiting, Running };

        QString tag;
        std::function<void()> abortFn;

        std::atomic<bool> stopFlag{false};
        std::atomic<bool> watchdogFlag{false};

        // Guarded by mutex.
        std::mutex mutex;
        std::condition_variable cv;
        State state = State::Idle;
        double wakeTime = 0.0;      // Waiting: resume once sim time reaches this
        double releaseTime = 0.0;   // sim time of the step it was last released for
        double stepEnd = 0.0;       // end of that step
        bool inCall = false;        // controller thread is inside the controller's code

        // Controller thread only (and reset(), while the controller is idle).
        double mcuTime = 0.0;       // the controller's clock, sim seconds
        double sliceEnd = 0.0;      // copy of stepEnd for the current release

        void yieldUntil(double simTime);

    public:
        // tag prefixes log messages ("MCU", "STAGE"); abort interrupts the
        // running code (thread-safe; called by the watchdog).
        LockstepGate(const QString& tag, std::function<void()> abort);

        // Any thread. Clears the stop/watchdog flags and the clock; Idle.
        // Only while the controller is out of its code (see waitUntilOutOfCall).
        void reset();
        // Any thread. Due on the first step, at sim time 0.
        void start();
        // Any thread. Leaves the gate idle and wakes a blocked wait(); the
        // controller still has to interrupt its own code.
        void stop();
        // Any thread: idle, without the stop flag (the controller errored).
        void setIdle();
        // Any thread: true once the controller thread left its code (or timeout).
        bool waitUntilOutOfCall(std::chrono::milliseconds timeout);

        bool stopRequested() const { return stopFlag; }
        bool watchdogFired() const { return watchdogFlag; }

        // Physics thread: run the controller for the step [simTime, simTime + dt).
        void runStep(double simTime, double dt);

        // Controller thread. beginCall() blocks briefly until released and
        // returns false while not (so the caller can check for shutdown);
        // after true, run the controller's code, then endCall().
        bool beginCall();
        void endCall();

        // Controller thread: spend sim time; yields once the step is used up.
        void charge(double seconds);
        // Controller thread: the controller spends `seconds` of sim time
        // (delay(), a bus transfer); the physics runs meanwhile.
        void wait(double seconds);
        // Controller thread: the clock as Arduino reports it (charged calls).
        double millis();
        double micros();
        // Controller thread: the clock, uncharged.
        double now() const { return mcuTime; }
};
