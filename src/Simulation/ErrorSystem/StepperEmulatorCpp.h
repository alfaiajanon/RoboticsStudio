#include "Emulator.h"

#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"

class StepperEmulatorCpp : public Emulator {
    Q_OBJECT
    private:
        double logicalTargetVelocity = 0.0;

        // Cached parameters from .rsdef
        double stepAngleDeg = 1.8;
        int microstepping = 1;

    public:
        using Emulator::Emulator;


        void init() override{
            if (component->getBlueprint()) {
                auto params = component->getBlueprint()->emulatorDef.parameters;
                if (params.contains("step_angle_deg")) stepAngleDeg = params["step_angle_deg"].toDouble();
                if (params.contains("microstepping")) microstepping = params["microstepping"].toInt();
            }
        }

        void update() override {
            // In a highly advanced emulator, you would use stepAngleDeg and the current simulation
            // delta-time to calculate discrete position steps and create a "choppy" velocity profile.
            // For now, we smoothly pass the requested target velocity to the MuJoCo actuator.
            BasicIOValue val = component->getActuatorValue("target_velocity");
            val.data[0]=logicalTargetVelocity;
            component->setActuatorValue("target_velocity", val);
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
            // return component->getSensorCurrent("current_position");
            if(component->getSensorValue("current_position").dim==1) {
                return component->getSensorValue("current_position").data[0];
            } else {
                return 0.0;
            }
        }

        Q_INVOKABLE double read_velocity() {
            // return component->getSensorCurrent("current_velocity");
            if(component->getSensorValue("current_velocity").dim==1) {
                return component->getSensorValue("current_velocity").data[0];
            } else {
                return 0.0;
            }
        }
};
