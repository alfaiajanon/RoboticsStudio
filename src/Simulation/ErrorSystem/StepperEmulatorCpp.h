#include "Emulator.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Core/Log.h"
#include <cmath>

class StepperEmulatorCpp : public Emulator {
    Q_OBJECT
    private:
        double logicalTargetVelocity = 0.0; 
        
        // Cached parameters from .rsdef
        double stepAngleDeg = 1.8;
        int microstepping = 1;

    public:
        using Emulator::Emulator;

        void update() override {
            // Read hardware parameters from the blueprint (fallback to defaults if missing)
            if (component->blueprint) {
                auto params = component->blueprint->emulatorDef.parameters;
                if (params.contains("step_angle_deg")) stepAngleDeg = params["step_angle_deg"].toDouble();
                if (params.contains("microstepping")) microstepping = params["microstepping"].toInt();
            }

            // In a highly advanced emulator, you would use stepAngleDeg and the current simulation 
            // delta-time to calculate discrete position steps and create a "choppy" velocity profile. 
            // For now, we smoothly pass the requested target velocity to the MuJoCo actuator.
            component->setActuatorTarget("target_velocity", logicalTargetVelocity);
        }

        void reset() override {
            logicalTargetVelocity = 0.0;
        }


        // ==========================================
        // API exposed to the JavaScript MCU Thread
        // ==========================================
        
        Q_INVOKABLE void set_velocity(double rad_per_sec) {
            logicalTargetVelocity = rad_per_sec;
        }

        Q_INVOKABLE void set_rpm(double rpm) {
            // Convert RPM to Radians per second
            logicalTargetVelocity = rpm * (M_PI / 30.0);
        }

        Q_INVOKABLE void stop() {
            logicalTargetVelocity = 0.0;
        }
        
        Q_INVOKABLE double read_position() {
            return component->getSensorCurrent("current_position");
        }

        Q_INVOKABLE double read_velocity() {
            return component->getSensorCurrent("current_velocity");
        }
};