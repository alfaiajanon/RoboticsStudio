#include "Emulator.h"

#include "Document/Components/ComponentInstance.h"


class ServoEmulatorCpp : public Emulator {
    Q_OBJECT
    private:
        double logicalTarget = 0.0;

    public:
        using Emulator::Emulator;

        void init() override{}

        void update() override {
            IOData targetData = logicalTarget;
            component->setActuatorTarget("target_angle", targetData);
        }

        void reset() override {
            logicalTarget = 0.0;
        }



        // API exposed to JS
        Q_INVOKABLE void write_angle(double angle) {
            logicalTarget = angle;
        }

        Q_INVOKABLE double read_angle() {
            if(std::holds_alternative<double>(component->getSensorCurrent("target_angle"))) {
                return std::get<double>(component->getSensorCurrent("target_angle"));
            } else {
                return 0.0;
            }
        }
};
