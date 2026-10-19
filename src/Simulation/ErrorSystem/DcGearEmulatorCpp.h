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
 * Brushed DC gear motor behind an H-bridge, in the order the signal takes
 * through the real parts:
 *
 *   motor_a, motor_b  the board's two pins into the driver (IN1, IN2). At the
 *                     Prototype level the driver is ideal and built in
 *                     (DRV8833-style; real driver parts come with Phase 2):
 *                       A pulsing, B LOW   forward at A's duty
 *                       B pulsing, A LOW   reverse at B's duty
 *                       both HIGH          brake (terminals shorted)
 *                       both LOW           coast (terminals open)
 *                     duty = the pin's average (analogWrite: 490 Hz pulse
 *                     width; a steady HIGH = 1). Mixed cases use A - B.
 *   terminal voltage  duty x supply_voltage
 *   DcMotor           torque-speed line at that voltage (back-EMF), this
 *                     unit's no-load speed (UnitVariation) -> motor actuator
 *   gearbox           static friction is the joint's frictionloss, so a low
 *                     duty doesn't start the motor (no deadband step needed)
 *
 * The averaged-voltage model ignores the PWM ripple and the difference
 * between the driver's fast and slow decay.
 *
 * A cheap gear motor has no encoder: the firmware can't read its
 * speed (plots can: current_velocity).
 *
 * Signals: in motor_torque (N·m, written here); out current_velocity (rad/s,
 * output shaft), drive (net duty -1..1, for plots).
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   stall_torque_nm, no_load_speed_rpm, rated_voltage   datasheet line
 *   supply_voltage                                      what the driver gets
 *   speed_spread                                        unit-to-unit (stddev, fraction)
 */
class DcGearEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        Steps::Rng rng;
        Steps::DcMotor motor;
        Steps::UnitVariation unit;
        double supplyVoltage = 12.0;
        int pinA = -1, pinB = -1;   // PinBus handles

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

            pinA = pins ? pins->ensurePin(component->uid, "motor_a", "pwm") : -1;
            pinB = pins ? pins->ensurePin(component->uid, "motor_b", "pwm") : -1;
        }

        void update() override {
            double torque = 0.0, duty = 0.0;
            if (pins) {
                const double omega = EmulatorIO::readOutput(component, "current_velocity");
                if (steadyHigh(pins->state(pinA)) && steadyHigh(pins->state(pinB))) {
                    torque = motor.brakeTorque(omega);
                } else {
                    duty = pins->dutyCycle(pinA) - pins->dutyCycle(pinB);
                    torque = motor.torque(duty, supplyVoltage, omega);
                }
            }
            EmulatorIO::writeScalar(component, "motor_torque", torque, true);
            EmulatorIO::writeScalar(component, "drive", duty, false);
        }

        // The pins are rebuilt (undriven: coast) at every Play.
        void reset() override {
            EmulatorIO::writeScalar(component, "motor_torque", 0.0, true);
            EmulatorIO::writeScalar(component, "drive", 0.0, false);
        }
};
