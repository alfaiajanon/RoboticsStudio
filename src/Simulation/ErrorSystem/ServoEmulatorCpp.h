#pragma once

#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include <cmath>
#include <deque>
#include <random>

/*
 * SG90/MG996R-style hobby servo emulator. All error is modeled on the
 * INPUT signal path (logicalTarget -> effectiveTarget) -- current_angle is
 * a non-physical output (physical=false, no real feedback pin exists on
 * real hardware) and is intentionally left untouched.
 *
 * Signal path, in causal order: delay (wire/internal latency) -> noise
 * (ADC sampling) -> deadband (digital threshold logic) -> effectiveTarget.
 *
 * update() runs once per physics tick, after syncFromMujocoSensor and
 * before pushTelemetry (see SimulationManager::physicsLoop), so
 * effectiveTarget reaches MuJoCo on the NEXT tick (one-tick input latency,
 * separate from and in addition to the modeled delay_ms).
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   deadband_threshold  (degrees, default 0 = disabled)
 *   noise_stddev         (degrees, default 0 = disabled)
 */
class ServoEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        double logicalTarget = 0.0;    // last value JS wrote via write()
        double heldTarget = 0.0;  // what's actually sent to MuJoCo

        double deadbandThreshold = 0.0;
        double noiseStdDev = 0.0;

        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<double> noise{0.0, 1.0};

    public:
        using Emulator::Emulator;

        void init() override {
            if (component && component->getBlueprint()) {
                const auto& params = component->getBlueprint()->emulatorDef.parameters;
                deadbandThreshold = params.value("deadband_threshold", 0.0).toDouble();
                noiseStdDev = params.value("noise_stddev", 0.0).toDouble();
            }
        }

        void update() override {
            // 1. Deadband: only move the held (clean) setpoint if the delayed command changed enough to matter.
            if (std::abs(logicalTarget - heldTarget) > deadbandThreshold) {
                heldTarget = logicalTarget;
            }

            // 2. Noise: fresh sample every tick, added on top of the held setpoint
            double effectiveTarget = heldTarget + noise(rng) * noiseStdDev;

            BasicIOValue targetData = component->getActuatorValue("target_angle");
            targetData.data[0]= effectiveTarget;
            component->setActuatorValue("target_angle", targetData);
        }

        void reset() override {
            logicalTarget = 0.0;
            heldTarget = 0.0;
        }

        // API exposed to JS
        Q_INVOKABLE void write(double angle) {
            logicalTarget = angle;
        }

        Q_INVOKABLE double read() {
            BasicIOValue data = component->getSensorValue("target_angle");
            return data.dim==1 ? data.data[0] : 0.0;
        }
};
