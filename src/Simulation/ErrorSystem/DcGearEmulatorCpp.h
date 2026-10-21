#pragma once

#include "Emulator.h"
#include "EmulatorIO.h"
#include "Steps.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Controller/PinBus.h"
#include <algorithm>
#include <cmath>

/*
 * Brushed DC gear motor: two terminals (motor_a, motor_b), in the order the
 * signal takes through the real parts.
 *
 * Prototype level: the ideal board drives the terminals through a built-in
 * ideal H-bridge (DRV8833-style) on two of its pins (IN1, IN2):
 *     A pulsing, B LOW   forward at A's duty
 *     B pulsing, A LOW   reverse at B's duty
 *     both HIGH          brake (terminals shorted)
 *     both LOW           coast (terminals open)
 *   duty = the pin's average (analogWrite: 490 Hz pulse width; a steady
 *   HIGH = 1). Mixed cases use A - B. Terminal voltage = duty x supply_voltage.
 *
 * Real level: the terminals are a small circuit, solved every step. Each one
 * is driven by whatever is on its net (PinBus::source): a motor driver's
 * output (its supply minus its drop, through its on-resistance), a board pin
 * (its logic voltage x duty, through the pin's output resistance), or nothing
 * (open: the motor coasts).
 *     I = (Va - Vb - Ke·w) / (Ra + Rb + R_arm),   torque = Kt·I
 *   R_arm = rated_voltage / stall_current_a, Kt = stall_torque / stall_current,
 *   Ke = rated_voltage / no-load speed: with an ideal source that is the same
 *   torque-speed line as the Prototype's. Both terminals on the same voltage
 *   brake (back-EMF current); an open terminal coasts. While a side is open
 *   part of the time (a driver's enable pin PWM'd: fast decay) the bridge
 *   can't carry current back, so the motor only pulls. The current goes back
 *   onto the nets (PinBus::setAmps): the driver draws it from its supply; a
 *   board pin carries it (and is warned about above its absolute maximum).
 *
 * Then: DcMotor (+ per-unit speed spread) -> motor actuator; the gearbox's
 * static friction is the joint's frictionloss, so a weak drive doesn't start
 * it. The averaged model ignores PWM ripple and the motor's inductance.
 *
 * A cheap gear motor has no encoder: the firmware can't read its speed
 * (plots can: current_velocity, current).
 *
 * Signals: in motor_torque (N·m, written here); out current_velocity (rad/s,
 * output shaft), drive (Prototype: net duty -1..1; Real: terminal voltage /
 * rated voltage), current (A, motor current; simulation-only).
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   stall_torque_nm, no_load_speed_rpm, rated_voltage   datasheet line
 *   stall_current_a                                     at rated_voltage (R_arm)
 *   supply_voltage                                      the ideal driver's (Prototype)
 *   speed_spread                                        unit-to-unit (stddev, fraction)
 */
class DcGearEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        Steps::Rng rng;
        Steps::DcMotor motor;
        Steps::UnitVariation unit;
        double supplyVoltage = 12.0;
        double stallCurrent = 1.0;   // A at rated_voltage
        int pinA = -1, pinB = -1;    // PinBus handles

        static bool steadyHigh(const PinState* s) {
            return s && s->drive == PinState::Drive::Level && s->level == 1;
        }

    public:
        using Emulator::Emulator;

        void init() override {
            using EmulatorIO::param;
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            QMap<QString, QVariant> p = bp ? bp->emulatorDef.parameters : QMap<QString, QVariant>{};

            motor.stallTorque = param(p, "stall_torque_nm", 0.15);
            motor.ratedVoltage = std::max(0.1, param(p, "rated_voltage", 12.0));
            supplyVoltage = param(p, "supply_voltage", motor.ratedVoltage);

            rng.seed(seed());
            unit.draw(rng, 0.0, param(p, "speed_spread", 0.0));
            motor.noLoadSpeed = param(p, "no_load_speed_rpm", 60.0) * M_PI / 30.0 * unit.gain;
            stallCurrent = std::max(1e-3, param(p, "stall_current_a", 1.0));

            pinA = pins ? pins->ensurePin(component->uid, "motor_a", "motor") : -1;
            pinB = pins ? pins->ensurePin(component->uid, "motor_b", "motor") : -1;
        }

        void update() override {
            double torque = 0.0, duty = 0.0, amps = 0.0;
            const double kt = motor.stallTorque / stallCurrent;
            if (pins) {
                const double omega = EmulatorIO::readOutput(component, "current_velocity");
                const PinState* a = pins->state(pinA);
                const PinState* b = pins->state(pinB);
                if ((a && a->ideal) || (b && b->ideal)) {
                    // Prototype: the ideal built-in driver.
                    if (steadyHigh(a) && steadyHigh(b)) {
                        torque = motor.brakeTorque(omega);
                    } else {
                        duty = pins->dutyCycle(pinA) - pins->dutyCycle(pinB);
                        torque = motor.torque(duty, supplyVoltage, omega);
                    }
                    amps = kt > 0.0 ? torque / kt : 0.0;
                } else {
                    // Real: the circuit at the terminals.
                    const PinSource sa = pins->source(pinA), sb = pins->source(pinB);
                    if (!sa.open && !sb.open && pinA != pinB && motor.noLoadSpeed > 0.0) {
                        const double conn = std::min(sa.conn, sb.conn);
                        const double applied = sa.v - sb.v;
                        const double rArm = motor.ratedVoltage / stallCurrent;
                        const double ke = motor.ratedVoltage / motor.noLoadSpeed;
                        double i = (applied - ke * omega) / (sa.r + sb.r + rArm);
                        if (conn < 1.0 && i * applied < 0.0) i = 0.0;   // fast decay: no current back through the bridge
                        amps = conn * i;
                        torque = kt * amps;
                        duty = std::clamp(conn * applied / motor.ratedVoltage, -1.0, 1.0);
                    }
                    pins->setAmps(pinA, amps);
                    pins->setAmps(pinB, -amps);
                }
            }
            lastAmps = amps;
            EmulatorIO::writeScalar(component, "motor_torque", torque, true);
            EmulatorIO::writeScalar(component, "drive", duty, false);
            EmulatorIO::writeScalar(component, "current", amps, false);
        }

        // The pins are rebuilt (undriven: coast) at every Play.
        void reset() override {
            lastAmps = 0.0;
            EmulatorIO::writeScalar(component, "motor_torque", 0.0, true);
            EmulatorIO::writeScalar(component, "drive", 0.0, false);
            EmulatorIO::writeScalar(component, "current", 0.0, false);
        }

        double lastAmps = 0.0;
};
