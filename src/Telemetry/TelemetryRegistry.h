// src/Simulation/Telemetry/TelemetryRegistry.h

#pragma once

#include <QMap>
#include <QList>
#include <memory>
#include <mutex>
#include "Channel.h"
#include "Sources/TelemetrySource.h"

using namespace std;

class TelemetryRegistry {
private:
    TelemetryRegistry() = default;
    ~TelemetryRegistry() = default;

    int nextId = 1;
    mutex registryMutex;
    QMap<QString, shared_ptr<TelemetrySource>> sources;

public:
    static TelemetryRegistry& getInstance();
    QList<QString> getKeysOfType(DataType type) const;
    shared_ptr<TelemetrySource> getSource(const QString& key) const;
    shared_ptr<TelemetrySource> getOrCreateSource(const QString& key,
                                                  const function<shared_ptr<TelemetrySource>()>& factory);

    void clearAll();
    void clearInactive();
    void captureAll(double time);

};
