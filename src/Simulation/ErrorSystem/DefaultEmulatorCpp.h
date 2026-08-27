#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Utils/Log.h"


class DefaultEmulatorCpp : public Emulator {
    Q_OBJECT

    public:
        using Emulator::Emulator;

        void update() override {
        }

        void reset() override {
        }
};