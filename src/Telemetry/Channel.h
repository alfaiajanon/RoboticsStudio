// src/Simulation/Telemetry/Channel.h

#pragma once

#include <QString>
#include <QList>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include "Sources/TelemetrySource.h"



// enum class DataType { SCALAR, VECTOR, IMAGE };
// enum class SourceKind { INPUT, OUTPUT, MCU, BODY_MOTION, CUSTOM };




struct ChannelMeta {
    QString name;
    SourceKind source; // probably not needed
    bool physical;
    int dim = 1;
    int width = 0;
    int height = 0;
};

struct ScalarPoint {
    double time;
    double value;
};

struct VectorPoint {
    double time;
    std::vector<double> value;
};

struct ImageFrame {
    double time;
    std::vector<unsigned char> pixels;
};






class Channel {
    protected:
        int id;
        ChannelMeta meta;
        std::atomic<int> refCount{0};

    public:
        Channel(int id, const ChannelMeta& meta) : id(id), meta(meta) {}
        virtual ~Channel() = default;

        int getId() const { return id; }
        const ChannelMeta& getMeta() const { return meta; }
        virtual DataType getType() const = 0;

        void subscribe() { refCount.fetch_add(1, std::memory_order_relaxed); }
        void unsubscribe() { refCount.fetch_sub(1, std::memory_order_relaxed); }
        bool isActive() const { return refCount.load(std::memory_order_relaxed) > 0; }
};






class ScalarChannel : public Channel {
    private:
        std::mutex bufferMutex;
        std::vector<ScalarPoint> buffer;
        size_t head = 0;
        size_t capacity;

    public:
        ScalarChannel(int id, const ChannelMeta& meta, size_t cap = 1000)
            : Channel(id, meta), capacity(cap) {
            buffer.resize(capacity);
        }

        DataType getType() const override { return DataType::SCALAR; }
        void push(double time, double value);
        std::vector<ScalarPoint> snapshot();
};





class VectorChannel : public Channel {
    private:
        std::mutex bufferMutex;
        std::vector<VectorPoint> buffer;
        size_t head = 0;
        size_t capacity;

    public:
        VectorChannel(int id, const ChannelMeta& meta, size_t cap = 1000)
            : Channel(id, meta), capacity(cap) {
            buffer.resize(capacity, {0.0, std::vector<double>(meta.dim, 0.0)});
        }

        DataType getType() const override { return DataType::VECTOR; }
        void push(double time, const std::vector<double>& value);
        std::vector<VectorPoint> snapshot();
};





class ImageChannel : public Channel {
    private:
        std::mutex bufferMutex;
        ImageFrame backBuffer;
        ImageFrame frontBuffer;
        bool isNewFrameAvailable = false;

    public:
        ImageChannel(int id, const ChannelMeta& meta) : Channel(id, meta) {}

        DataType getType() const override { return DataType::IMAGE; }
        void push(double time, const unsigned char* pixels, size_t size);
        bool snapshot(ImageFrame& outFrame);
};
