#pragma once

#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/Components/ComponentInstance.h"


class IMUEmulatorCpp : public Emulator {
    Q_OBJECT

public:
    using Emulator::Emulator;

    void update() override {}
    void reset() override {}

    // --- API EXPOSED TO JAVASCRIPT ---
    Q_INVOKABLE double read_accel_x() const { return component->getSensorCurrent("accel_x"); }
    Q_INVOKABLE double read_accel_y() const { return component->getSensorCurrent("accel_y"); }
    Q_INVOKABLE double read_accel_z() const { return component->getSensorCurrent("accel_z"); }

    Q_INVOKABLE double read_gyro_x() const { return component->getSensorCurrent("gyro_x"); }
    Q_INVOKABLE double read_gyro_y() const { return component->getSensorCurrent("gyro_y"); }
    Q_INVOKABLE double read_gyro_z() const { return component->getSensorCurrent("gyro_z"); }
};