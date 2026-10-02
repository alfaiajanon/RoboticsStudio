#pragma once
#include <vector>
#include <utility>
#include <mutex>


class TimeSeriesDataPoint {
    public:
        double time;
        std::vector<double> values;
        TimeSeriesDataPoint():time(0.0) {}
        TimeSeriesDataPoint(double time, const std::vector<double>& values):time(time), values(values) {};
};


class TimeSeriesBuffer {
    private:
        int capacity;
        int dim;
        std::vector<TimeSeriesDataPoint> data;
        int head = 0;
        bool isFull = false;
        mutable std::mutex bufferMutex;

        std::vector<TimeSeriesDataPoint> snapshotLocked() const;   // caller holds bufferMutex

    public:
        explicit TimeSeriesBuffer(int capacity, int dim);

        // Thread-safe core operations
        void push(double time, const std::vector<double>& values);
        void resize(int newCapacity);
        void clear();

        // Cursor-based delta reading (for efficient live plotting)
        int getCurrentHead() const;
        std::vector<TimeSeriesDataPoint> pullNewData(int& lastReadCursor);

        // Full buffer extraction (for saving/exporting)
        std::vector<TimeSeriesDataPoint> snapshot() const;

        // Points strictly newer than `afterTime`, oldest first. If the newest
        // stored point is OLDER than `afterTime` the simulation clock was reset,
        // so the whole buffer is returned (first point <= afterTime tells the caller).
        std::vector<TimeSeriesDataPoint> snapshotFrom(double afterTime) const;

        // Accessors
        int getDimension() const;
        int getCapacity() const;
        bool getIsFull() const;
};
