#pragma once

#include <algorithm>
#include <cmath>
#include <random>

/*
 * Reusable error-model steps. Each one stands for a physical mechanism inside
 * a part; a device model composes them in the order the signal actually takes
 * through the hardware (see docs/dev/architecture-v2.md, device models).
 * All randomness comes from the caller's generator, seeded from
 * Emulator::seed(), so runs repeat.
 */
namespace Steps {

using Rng = std::mt19937_64;


/*
 * A receiver that samples its input once per frame, e.g. a hobby servo that
 * acts on one PWM pulse every 20 ms. The frame phase differs per unit (the
 * servo's clock is not synchronised with the MCU), so the latency between a
 * command and its effect is anywhere in [0, period).
 */
struct FrameLatch {
    double period = 0.02;
    double nextDue = 0.0;
    bool hasValue = false;   // false while the last frame saw no input
    double value = 0.0;

    void reset(double phase) { nextDue = phase; hasValue = false; value = 0.0; }

    // Call every tick. `inputPresent` is false while nothing drives the
    // input: before the first pulse, or after the pulses stop (Servo
    // detach()). A frame without a pulse reports no value, so an analog
    // servo goes limp, as the real ones do.
    void tick(double simTime, bool inputPresent, double input) {
        if (simTime + 1e-12 < nextDue) return;
        hasValue = inputPresent;
        if (inputPresent) value = input;
        nextDue += period;
        if (nextDue <= simTime) nextDue = simTime + period;   // skipped frames (not expected)
    }
};


/*
 * Unit-to-unit spread: no two cheap parts share the same zero point or
 * scale. Drawn once per instance (normal, stddev = spread).
 */
struct UnitVariation {
    double offset = 0.0;   // in the input's unit (e.g. µs of pulse width)
    double gain = 1.0;     // multiplies the nominal scale

    void draw(Rng& rng, double offsetSpread, double gainSpread) {
        std::normal_distribution<double> n(0.0, 1.0);
        offset = n(rng) * offsetSpread;
        gain = 1.0 + n(rng) * gainSpread;
        if (gain < 0.5) gain = 0.5;   // keep a wild draw physical
    }
};


/*
 * Deadband of a controller: inside +-halfWidth of zero error it does not
 * drive at all (it stops hunting around the setpoint, and lets the output
 * sit anywhere inside the band). Datasheets give the full width.
 */
struct Deadband {
    double halfWidth = 0.0;

    bool inside(double error) const { return std::abs(error) <= halfWidth; }
};


/*
 * Noise where an analog quantity is measured (a potentiometer, an ADC
 * input, a sensor element). Never applied to a clean digital command.
 */
struct MeasurementNoise {
    double stddev = 0.0;

    double measure(double trueValue, Rng& rng) const {
        if (stddev <= 0.0) return trueValue;
        std::normal_distribution<double> n(0.0, stddev);
        return trueValue + n(rng);
    }
};


/*
 * Brushed DC motor behind a driver, at the output shaft (gear ratio folded
 * in). Linear torque-speed line from the datasheet: stall torque at zero
 * speed, zero torque at no-load speed, both at the rated voltage; the line
 * scales with the applied voltage. Back-EMF makes speed the natural limit.
 * Duty 0 means the driver is off (terminals open): no torque, no braking.
 * brakeTorque(): the driver shorts the terminals, so back-EMF drives a
 * current that opposes the motion (stall torque at no-load speed, 0 at rest).
 */
struct DcMotor {
    double stallTorque = 0.0;   // N·m at ratedVoltage
    double noLoadSpeed = 0.0;   // rad/s at ratedVoltage
    double ratedVoltage = 1.0;

    // duty -1..1, supply in volts, omega in rad/s -> torque in N·m
    double torque(double duty, double supplyVoltage, double omega) const {
        if (duty == 0.0 || stallTorque <= 0.0 || noLoadSpeed <= 0.0 || ratedVoltage <= 0.0) return 0.0;
        const double v = std::clamp(duty, -1.0, 1.0) * supplyVoltage / ratedVoltage;
        const double limit = stallTorque * supplyVoltage / ratedVoltage;
        return std::clamp(stallTorque * (v - omega / noLoadSpeed), -limit, limit);
    }

    // Supply current (A) for a drive that gave `torque` at `duty` (a
    // chopped H-bridge: the motor's current flows from the supply only while
    // the switches are on, so the average is |duty| x motor current; current
    // the motor pushes back while braking is not returned). stallCurrent is
    // at ratedVoltage: torque per amp = stallTorque / stallCurrent.
    double supplyCurrent(double duty, double torque, double stallCurrent) const {
        if (stallTorque <= 0.0 || stallCurrent <= 0.0 || duty == 0.0) return 0.0;
        const double motorCurrent = torque * stallCurrent / stallTorque;
        return std::abs(duty) * std::max(0.0, duty > 0.0 ? motorCurrent : -motorCurrent);
    }

    double brakeTorque(double omega) const {
        if (stallTorque <= 0.0 || noLoadSpeed <= 0.0) return 0.0;
        return -stallTorque * omega / noLoadSpeed;
    }
};


/*
 * A measurement range: an analog front end or ADC clips at full scale.
 */
struct Saturation {
    double limit = 0.0;   // symmetric, in the measured unit; 0 = none

    double apply(double v) const { return limit > 0.0 ? std::clamp(v, -limit, limit) : v; }
};


/*
 * An ADC or a sensor's digital output: a value in units of one LSB, rounded
 * and clamped to the register's width (signed 16-bit by default).
 */
struct Quantize {
    double lsb = 1.0;   // measured unit per count
    long minCount = -32768, maxCount = 32767;

    long counts(double v) const {
        return std::clamp(std::lround(v / lsb), minCount, maxCount);
    }
};


/*
 * Bias of a sensor: a per-unit offset (drawn once from the seed, like a
 * datasheet's "zero offset" spread) plus a slow random walk (bias drift).
 */
struct UnitBias {
    double offset = 0.0;   // drawn per unit
    double walk = 0.0;     // accumulated drift
    double walkRate = 0.0; // random-walk stddev per sqrt(second)

    void draw(Rng& rng, double spread) {
        std::normal_distribution<double> n(0.0, 1.0);
        offset = n(rng) * spread;
        walk = 0.0;
    }
    double step(double dt, Rng& rng) {
        if (walkRate > 0.0 && dt > 0.0) {
            std::normal_distribution<double> n(0.0, walkRate * std::sqrt(dt));
            walk += n(rng);
        }
        return offset + walk;
    }
};


/*
 * White noise given as a datasheet noise density (unit/sqrt(Hz)): the
 * stddev seen at the output is density * sqrt(bandwidth).
 */
struct NoiseDensity {
    double density = 0.0;     // unit / sqrt(Hz)
    double bandwidth = 0.0;   // Hz, the filter in front of the output

    double sample(Rng& rng) const {
        const double sigma = density * std::sqrt(std::max(0.0, bandwidth));
        if (sigma <= 0.0) return 0.0;
        std::normal_distribution<double> n(0.0, sigma);
        return n(rng);
    }
};


/*
 * First-order low-pass at a cutoff frequency (a sensor's digital low-pass
 * filter, approximated by one pole). cutoff <= 0 passes through.
 */
struct FirstOrderLowPass {
    double cutoff = 0.0;   // Hz
    double y = 0.0;
    bool primed = false;

    double apply(double x, double dt) {
        if (cutoff <= 0.0 || !primed) { y = x; primed = true; return y; }
        const double rc = 1.0 / (2.0 * M_PI * cutoff);
        y += (x - y) * (dt / (rc + dt));
        return y;
    }
    void reset() { primed = false; y = 0.0; }
};

/*
 * Rotor of a hybrid stepper on a driver that holds the commanded (micro)step
 * (an A4988-style driver, always enabled).
 *
 * Torque-angle curve: T = T_avail * sin(pi/2 * lag / fullStep), lag = the
 * commanded angle minus the rotor's. It peaks (the holding torque) one full
 * step from the equilibrium and repeats every 4 full steps (one electrical
 * cycle), so a rotor pushed more than 2 full steps behind falls into the next
 * equilibrium by itself: 4 full steps lost for good. At power-on the rotor
 * snaps to the nearest equilibrium the same way. Available torque falls with
 * speed (pull-out): T_avail = holding / (1 + |w| / w_c). Damping: the
 * driver's electrical damping, a fraction of critical.
 *
 * The real curve is stiff (holding * pi/2 / fullStep, ~22 N·m/rad for a
 * NEMA17). To stay stable at a physics step dt the driven inertia must be at
 * least stiffness * (2 dt)^2 (minInertia()); the part's armature sees to it
 * (about 3.6e-4 kg·m² at 2 ms, more than a real rotor's ~5e-6), which slows
 * an almost unloaded stepper's acceleration but keeps holding torque,
 * stiffness and missed steps real.
 */
struct StepperRotor {
    double holdingTorque = 0.45;           // N·m
    double fullStep = 1.8 * M_PI / 180.0;  // rad
    int microsteps = 1;
    double cornerSpeed = 31.4;             // rad/s, pull-out torque halves here
    double dampingRatio = 0.5;             // fraction of critical

    long commanded = 0;        // microsteps since power-on
    bool powered = false;
    long startCycle = 0;       // electrical cycle the rotor was in at power-on
    long missedFullSteps = 0;  // signed: positive = rotor behind the command

    double microStep() const { return fullStep / microsteps; }
    double cycle() const { return 4.0 * fullStep; }
    double stiffness() const { return holdingTorque * M_PI / (2.0 * fullStep); }
    double minInertia(double dt) const { return stiffness() * 4.0 * dt * dt; }
    void reset() { commanded = 0; powered = false; startCycle = 0; missedFullSteps = 0; }

    // One physics step: `steps` new microsteps (signed), the rotor's angle and
    // speed, the inertia it drives, the timestep -> motor torque.
    double torque(long steps, double theta, double omega, double inertia, double dt) {
        commanded += steps;
        const double lag = commanded * microStep() - theta;
        const long cycleNow = std::lround(lag / cycle());
        if (!powered) { startCycle = cycleNow; powered = true; }
        missedFullSteps = 4 * (cycleNow - startCycle);

        const double commandedRate = dt > 0.0 ? steps * microStep() / dt : 0.0;
        const double available = holdingTorque / (1.0 + std::abs(omega) / cornerSpeed);
        const double c = 2.0 * dampingRatio * std::sqrt(stiffness() * std::max(1e-9, inertia));
        return available * std::sin(M_PI / 2.0 * lag / fullStep) + c * (commandedRate - omega);
    }
};

}   // namespace Steps
