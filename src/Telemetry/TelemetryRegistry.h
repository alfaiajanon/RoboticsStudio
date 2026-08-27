// src/Simulation/Telemetry/TelemetryRegistry.h

#pragma once

#include <QMap>
#include <QList>
#include <memory>
#include <mutex>
#include "Channel.h"
#include "TelemetrySource.h"

class TelemetryRegistry {
private:
    TelemetryRegistry() = default;
    ~TelemetryRegistry() = default;

    int nextId = 1;
    std::mutex registryMutex;

    QMap<int, std::shared_ptr<ScalarChannel>> scalarChannels;
    QMap<int, std::shared_ptr<VectorChannel>> vectorChannels;
    QMap<int, std::shared_ptr<ImageChannel>> imageChannels;

    QList<std::shared_ptr<TelemetrySource>> sources;

public:
    static TelemetryRegistry& getInstance();

    int registerScalar(const ChannelMeta& meta);
    int registerVector(const ChannelMeta& meta);
    int registerImage(const ChannelMeta& meta);

    std::shared_ptr<ScalarChannel> getScalar(int id);
    std::shared_ptr<VectorChannel> getVector(int id);
    std::shared_ptr<ImageChannel> getImage(int id);

    QList<int> getChannelsOfType(DataType type);

    void addSource(const std::shared_ptr<TelemetrySource>& source);
    void removeSource(int channelId);
    void captureAll(mjModel* m, mjData* d, double time);

    void clear();
};