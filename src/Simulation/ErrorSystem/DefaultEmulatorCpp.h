#include "Emulator.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Core/Log.h"


class DefaultEmulatorCpp : public Emulator {
    Q_OBJECT

    public:
        using Emulator::Emulator;

        void update() override {
        }

        void reset() override {
        }
};