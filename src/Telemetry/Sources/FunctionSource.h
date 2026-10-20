#pragma once

#include <functional>
#include "TelemetrySource.h"
#include "Telemetry/Storage/TimeSeriesBuffer.h"


// A scalar channel whose value comes from a function, called on the physics
// thread at each capture (e.g. the power model's rail voltages). The function
// returns false when there is no value right now (nothing is recorded).
class FunctionSource : public TelemetrySource {
    std::function<bool(double&)> read;
    TimeSeriesBuffer storage;

    public:
        explicit FunctionSource(std::function<bool(double&)> reader) : read(std::move(reader)), storage(100, 1) {}

        void capture(double time) override {
            double v = 0.0;
            if (isActive() && read && read(v)) storage.push(time, {v});
        }
        std::any snapshotAll() override { return storage.snapshot(); }
        std::any snapshotFrom(double time) override { return storage.snapshotFrom(time); }
};
