#include "BaseEmulator.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Core/Log.h"


class ServoEmulatorCpp : public BaseEmulator {
    Q_OBJECT
    private:
        double logicalTarget = 0.0; 

    public:
        using BaseEmulator::BaseEmulator;

        void update() override {
            component->setActuatorTarget("target_angle", logicalTarget);
        }



        // API exposed to JS
        Q_INVOKABLE void write_angle(double angle) {
            logicalTarget = angle;
        }
        
        Q_INVOKABLE double read_angle() {
            return component->getSensorCurrent("target_angle");
        }
};