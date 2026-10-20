#pragma once

#include "Emulator.h"
#include "Steps.h"
#include "Simulation/Controller/PinBus.h"
#include "EmulatorIO.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include <algorithm>
#include <cmath>

/*
 * Analog hobby servo (SG90, MG996R). The servo's own controller is modelled,
 * in the order the signal takes through the real part:
 *
 *   signal pin       the pulse width on the part's `signal` pin (PinBus),
 *                    written by the firmware (Servo library)
 *                    below; ideal timer for now (quantization comes with boards)
 *   FrameLatch       one pulse per frame_period_ms, per-unit phase. No pulse
 *                    yet: the servo is limp.
 *   UnitVariation    pulse -> target angle with this unit's centre and scale
 *   MeasurementNoise the pot on the OUTPUT shaft (after the gearbox backlash,
 *                    so backlash is inside the loop and causes hunting)
 *   Deadband         on the position error: no drive inside the band, so the
 *                    horn sits anywhere in it and sags under load
 *   duty             error / proportional_band, saturated
 *   DcMotor          torque-speed line at the supply voltage -> motor actuator
 *                    (Real level: the voltage of the net on `vcc`, so a
 *                    sagging supply weakens it; the motor's current is drawn
 *                    from that net)
 *
 * The comparator of an analog servo is continuous, so this runs after every
 * mj_step; the torque reaches MuJoCo on the next step.
 *
 * Signals (names fixed, see docs/user/create_servo.md):
 *   in  motor_torque        N·m, drives the motor actuator (written here)
 *   in  target_angle        latched target, degrees (written here, plots and
 *                           edit-mode posing only)
 *   out current_angle_real  output shaft angle (tendon) -> the pot; parts
 *                           without backlash use current_angle
 *   out gear_velocity       gear-side speed, deg/s -> back-EMF
 *   out pot_reading, drive  emulator-owned, for plots
 *
 * A real hobby servo has no feedback wire, so the firmware cannot read any of
 * these; plots can.
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   pulse_min_us, pulse_max_us, travel_deg   nominal pulse <-> angle mapping
 *   frame_period_ms                          pulse frame (default 20)
 *   deadband_us                              full dead band width
 *   proportional_band_deg                    error giving full drive
 *   stall_torque_nm, no_load_speed_dps, rated_voltage, supply_voltage
 *   stall_current_a                          at rated_voltage (supply current)
 *   pot_noise_deg                            pot reading noise (stddev)
 *   center_spread_us, gain_spread            unit-to-unit spread (stddev)
 */
class ServoEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        // Nominal mapping, shared by the MCU side (write) and the servo.
        double pulseMin = 1000.0, pulseMax = 2000.0, travelDeg = 180.0;
        double usPerDeg() const { return (pulseMax - pulseMin) / travelDeg; }
        double pulseCenter() const { return 0.5 * (pulseMin + pulseMax); }

        double proportionalBand = 5.0;
        double supplyVoltage = 5.0;    // Prototype (ideal power)
        double stallCurrent = 0.0;
        double drawn = 0.0;            // supply current of the last update
        double rangeMin = -90.0, rangeMax = 90.0;   // target clamp: the joint's range

        Steps::Rng rng;
        Steps::FrameLatch latch;
        Steps::UnitVariation unit;
        Steps::Deadband deadband;
        Steps::MeasurementNoise potNoise;
        Steps::DcMotor motor;

        int signalPin = -1;   // PinBus handle of the `signal` pin

        // Where the pot sits: the output shaft after the backlash gap, or the
        // gear joint itself for a part modelled without backlash.
        double outputAngle() const {
            return EmulatorIO::hasOutput(component, "current_angle_real") ? EmulatorIO::readOutput(component, "current_angle_real")
                                                                          : EmulatorIO::readOutput(component, "current_angle");
        }

    public:
        using Emulator::Emulator;

        void init() override {
            using EmulatorIO::param;
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            QMap<QString, QVariant> p = bp ? bp->emulatorDef.parameters : QMap<QString, QVariant>{};

            pulseMin = param(p, "pulse_min_us", 1000.0);
            pulseMax = param(p, "pulse_max_us", 2000.0);
            travelDeg = std::max(1.0, param(p, "travel_deg", 180.0));
            proportionalBand = std::max(0.1, param(p, "proportional_band_deg", 5.0));
            supplyVoltage = param(p, "supply_voltage", 5.0);
            stallCurrent = param(p, "stall_current_a", 0.0);

            deadband.halfWidth = 0.5 * param(p, "deadband_us", 0.0) / usPerDeg();
            potNoise.stddev = param(p, "pot_noise_deg", 0.0);
            motor.stallTorque = param(p, "stall_torque_nm", 0.0);
            motor.noLoadSpeed = param(p, "no_load_speed_dps", 0.0) * M_PI / 180.0;
            motor.ratedVoltage = param(p, "rated_voltage", 5.0);
            latch.period = std::max(1e-3, param(p, "frame_period_ms", 20.0) / 1000.0);

            rangeMin = -0.5 * travelDeg;
            rangeMax = 0.5 * travelDeg;
            if (bp) {
                for (const InterfaceDef& def : bp->interfaceInputs) {
                    if (def.name != "target_angle" || def.target.kind != "joint") continue;
                    for (const Edge& j : bp->joints) {
                        if (j.id == def.target.id && j.range.size() == 2) { rangeMin = j.range[0]; rangeMax = j.range[1]; }
                    }
                }
            }

            rng.seed(seed());
            unit.draw(rng, param(p, "center_spread_us", 0.0), param(p, "gain_spread", 0.0));
            std::uniform_real_distribution<double> phase(0.0, latch.period);
            latch.reset(phase(rng));

            signalPin = pins ? pins->ensurePin(component->uid, "signal", "pwm") : -1;
        }

        void update() override {
            double pulseUs = 0.0;
            const bool pulsePresent = pins && pins->pulse(signalPin, &pulseUs);
            latch.tick(simTime, pulsePresent, pulseUs);

            double target = 0.0, pot = 0.0, duty = 0.0, torque = 0.0;
            if (latch.hasValue) {
                // This unit's reading of the pulse.
                target = (latch.value - (pulseCenter() + unit.offset)) / (usPerDeg() * unit.gain);
                target = std::clamp(target, rangeMin, rangeMax);

                pot = potNoise.measure(outputAngle(), rng);
                double error = target - pot;
                if (!deadband.inside(error)) duty = std::clamp(error / proportionalBand, -1.0, 1.0);

                double omega = EmulatorIO::readOutput(component, "gear_velocity") * M_PI / 180.0;
                torque = motor.torque(duty, supplyVolts >= 0.0 ? supplyVolts : supplyVoltage, omega);
            }
            drawn = motor.supplyCurrent(duty, torque, stallCurrent);

            EmulatorIO::writeScalar(component, "motor_torque", torque, true);
            EmulatorIO::writeScalar(component, "target_angle", target, true);
            EmulatorIO::writeScalar(component, "pot_reading", pot, false);
            EmulatorIO::writeScalar(component, "drive", duty, false);
        }

        double supplyCurrent() const override { return drawn; }

        // Power back: the control IC restarts and takes the first pulse of
        // the next full frame (an estimate: no datasheet gives it).
        void powerRestored(double now) override { latch.reset(now + latch.period); }

        // The pins are rebuilt (undriven) at every Play.
        void reset() override {
            drawn = 0.0;
            latch.reset(0.0);
            EmulatorIO::writeScalar(component, "motor_torque", 0.0, true);
            EmulatorIO::writeScalar(component, "drive", 0.0, false);
        }
};
