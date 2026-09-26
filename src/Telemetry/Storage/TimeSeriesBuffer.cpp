#include "TimeSeriesBuffer.h"

TimeSeriesBuffer::TimeSeriesBuffer(int capacity, int dim)
    : capacity(capacity), dim(dim), data(capacity) {
}

void TimeSeriesBuffer::push(double time, const std::vector<double>& values) {
    // Drop malformed data rather than crashing the physics loop
    if (values.size() != static_cast<size_t>(dim)) return;

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (capacity == 0) return;

    data[head] = {time, values};
    head = (head + 1) % capacity;
    if (head == 0) {
        isFull = true;
    }
}

void TimeSeriesBuffer::resize(int newCapacity) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    capacity = newCapacity;
    data.resize(capacity);
    head = 0;
    isFull = false;
}

void TimeSeriesBuffer::clear() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    head = 0;
    isFull = false;
}

int TimeSeriesBuffer::getCurrentHead() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return head;
}

std::vector<TimeSeriesDataPoint> TimeSeriesBuffer::pullNewData(int& lastReadCursor) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::vector<TimeSeriesDataPoint> newPoints;

    if (capacity == 0 || lastReadCursor == head) {
        return newPoints;
    }

    if (lastReadCursor < head) {
        // Continuous block
        for (int i = lastReadCursor; i < head; ++i) {
            newPoints.push_back(data[i]);
        }
    } else {
        // Wrapped around: read to the end of the array, then from 0 to head
        for (int i = lastReadCursor; i < capacity; ++i) {
            newPoints.push_back(data[i]);
        }
        for (int i = 0; i < head; ++i) {
            newPoints.push_back(data[i]);
        }
    }

    lastReadCursor = head;
    return newPoints;
}



std::vector<TimeSeriesDataPoint> TimeSeriesBuffer::snapshot() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::vector<TimeSeriesDataPoint> result;

    if (capacity == 0) return result;

    if (isFull) {
        result.reserve(capacity);
        // Oldest data is at `head` to `capacity - 1`
        for (int i = head; i < capacity; ++i) {
            result.push_back(data[i]);
        }
        // Newest data is at `0` to `head - 1`
        for (int i = 0; i < head; ++i) {
            result.push_back(data[i]);
        }
    } else {
        result.reserve(head);
        // Buffer hasn't wrapped yet, data is just 0 to head
        for (int i = 0; i < head; ++i) {
            result.push_back(data[i]);
        }
    }

    return result;
}

int TimeSeriesBuffer::getDimension() const {
    return dim;
}

int TimeSeriesBuffer::getCapacity() const {
    return capacity;
}

bool TimeSeriesBuffer::getIsFull() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return isFull;
}
