#include "LockstepGate.h"

#include <algorithm>
#include <cmath>
#include "Utils/Log.h"


// Wake times this close to a step's end count as inside the step.
static constexpr double kTimeEpsilon = 1e-9;



LockstepGate::LockstepGate(const QString& tag, std::function<void()> abort)
    : tag(tag), abortFn(std::move(abort)) {}



void LockstepGate::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    stopFlag = false;
    watchdogFlag = false;
    state = State::Idle;
    wakeTime = 0.0;
    releaseTime = 0.0;
    stepEnd = 0.0;
    mcuTime = 0.0;
    origin = 0.0;
    sliceEnd = 0.0;
}



void LockstepGate::restartAt(double simTime) {
    std::lock_guard<std::mutex> lock(mutex);
    stopFlag = false;
    watchdogFlag = false;
    state = State::Waiting;
    wakeTime = simTime;
    releaseTime = simTime;
    mcuTime = simTime;
    origin = simTime;
    sliceEnd = simTime;
}



void LockstepGate::start() {
    std::lock_guard<std::mutex> lock(mutex);
    state = State::Waiting;
    wakeTime = 0.0;
    releaseTime = 0.0;
}



void LockstepGate::stop() {
    stopFlag = true;
    setIdle();
}



void LockstepGate::setIdle() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = State::Idle;
    }
    cv.notify_all();
}



bool LockstepGate::waitUntilOutOfCall(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this] { return !inCall; });
}



/*
 * Physics thread. Releases the controller as long as it is due inside this
 * step, and waits each time until it yields again (or stops).
 */
void LockstepGate::runStep(double simTime, double dt) {
    std::unique_lock<std::mutex> lock(mutex);
    const double end = simTime + dt;

    while (state == State::Waiting && wakeTime < end - kTimeEpsilon) {
        state = State::Running;
        releaseTime = simTime;
        stepEnd = end;
        cv.notify_all();

        if (!cv.wait_for(lock, kWatchdog, [this] { return state != State::Running; })) {
            Log::error(tag + ": code did not yield for 2 s (use delay() or return from loop()); stopped");
            state = State::Idle;
            watchdogFlag = true;
            if (abortFn) abortFn();   // thread-safe interrupt of the running code
            cv.notify_all();
            return;
        }
    }
}



/*
 * Controller thread. Blocks (briefly) until the physics thread releases the
 * controller; its clock then catches up to the release.
 */
bool LockstepGate::beginCall() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(50), [this] { return state == State::Running; });
    if (state != State::Running || stopFlag) return false;
    mcuTime = std::max({mcuTime, wakeTime, releaseTime});
    sliceEnd = stepEnd;
    inCall = true;
    return true;
}



void LockstepGate::endCall() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        inCall = false;
    }
    cv.notify_all();
}



/*
 * Controller thread. Hands control back to physics until sim time reaches
 * `simTime`. Returns early if the controller is stopped meanwhile.
 */
void LockstepGate::yieldUntil(double simTime) {
    std::unique_lock<std::mutex> lock(mutex);
    if (state != State::Running) return;

    state = State::Waiting;
    wakeTime = simTime;
    cv.notify_all();

    cv.wait(lock, [this] { return state == State::Running || stopFlag; });
    if (state != State::Running) return;
    mcuTime = std::max({mcuTime, wakeTime, releaseTime});
    sliceEnd = stepEnd;
}



void LockstepGate::charge(double seconds) {
    mcuTime += seconds;
    if (mcuTime >= sliceEnd - kTimeEpsilon) yieldUntil(mcuTime);
}



void LockstepGate::wait(double seconds) {
    if (stopFlag) return;
    charge(kTimeCallCost);
    yieldUntil(mcuTime + std::max(0.0, seconds));
}



double LockstepGate::millis() {
    charge(kTimeCallCost);
    return std::floor((mcuTime - origin) * 1000.0 + kTimeEpsilon);
}



double LockstepGate::micros() {
    charge(kTimeCallCost);
    return std::floor((mcuTime - origin) * 1e6 + kTimeEpsilon);
}
