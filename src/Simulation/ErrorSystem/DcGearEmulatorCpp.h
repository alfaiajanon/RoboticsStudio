#include "Emulator.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Core/Log.h"
#include <cmath>
#include <algorithm>

class DcGearEmulatorCpp : public Emulator {
    Q_OBJECT
    private:
        int currentPwm = 0;
        double currentVelocityRadS = 0.0;
        
        double maxRpm = 60.0;
        int deadbandPwm = 30;
        double accelDelayMs = 100.0; 

    public:
        using Emulator::Emulator;

        
        void update() override {
            if (component->blueprint) {
                auto params = component->blueprint->emulatorDef.parameters;
                if (params.contains("max_rpm_at_12v")) maxRpm = params["max_rpm_at_12v"].toDouble();
                if (params.contains("deadband_pwm")) deadbandPwm = params["deadband_pwm"].toInt();
                if (params.contains("acceleration_delay_ms")) accelDelayMs = params["acceleration_delay_ms"].toDouble();
            }

            double targetVelocity = 0.0;
            if (std::abs(currentPwm) >= deadbandPwm) {
                double targetRpm = (static_cast<double>(currentPwm) / 255.0) * maxRpm;
                targetVelocity = targetRpm * (M_PI / 30.0);
            }

            if (accelDelayMs > 0) {
                double dt = 16.0; 
                double alpha = std::clamp(dt / accelDelayMs, 0.0, 1.0);
                currentVelocityRadS += alpha * (targetVelocity - currentVelocityRadS);
            } else {
                currentVelocityRadS = targetVelocity;
            }

            component->setActuatorTarget("target_velocity", currentVelocityRadS);
        }


        void reset() override {
            currentPwm = 0;
            currentVelocityRadS = 0.0;
        }



        // Exposed to JS

        Q_INVOKABLE void set_pwm(int pwm) {
            currentPwm = std::clamp(pwm, -255, 255);
        }

        Q_INVOKABLE void brake() {
            currentPwm = 0;
            currentVelocityRadS = 0.0; 
        }

        Q_INVOKABLE double read_velocity() {
            return component->getSensorCurrent("current_velocity");
        }
};