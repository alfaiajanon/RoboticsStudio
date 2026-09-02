#pragma once

#include "Simulation/ErrorSystem/Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include <QJsonObject>

class IMUEmulatorCpp : public Emulator {
    Q_OBJECT

public:
    using Emulator::Emulator;

    void init() override{}
    void update() override {}
    void reset() override {}

    Q_INVOKABLE QJsonObject getAcceleration() const {
        QJsonObject accel;
        IOData data = component->getSensorCurrent("acceleration");

        // Safely check if the variant is currently holding a vector
        if (std::holds_alternative<std::vector<double>>(data)) {
            const auto& vec = std::get<std::vector<double>>(data);
            accel["x"] = vec.size() > 0 ? vec[0] : 0.0;
            accel["y"] = vec.size() > 1 ? vec[1] : 0.0;
            accel["z"] = vec.size() > 2 ? vec[2] : 0.0;
        } else {
            accel["x"] = 0.0; accel["y"] = 0.0; accel["z"] = 0.0;
        }
        return accel;
    }

    Q_INVOKABLE QJsonObject getRotation() const {
        QJsonObject gyro;
        IOData data = component->getSensorCurrent("gyroscope");

        if (std::holds_alternative<std::vector<double>>(data)) {
            const auto& vec = std::get<std::vector<double>>(data);
            gyro["x"] = vec.size() > 0 ? vec[0] : 0.0;
            gyro["y"] = vec.size() > 1 ? vec[1] : 0.0;
            gyro["z"] = vec.size() > 2 ? vec[2] : 0.0;
        } else {
            gyro["x"] = 0.0; gyro["y"] = 0.0; gyro["z"] = 0.0;
        }
        return gyro;
    }
};
