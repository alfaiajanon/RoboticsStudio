#pragma once

#include <vector>
#include <mutex>

struct PlotPoint {
    double time = 0.0;
    double value = 0.0;
};

template<typename T>
class RingBuffer {
    private:
        std::vector<T> data;
        int head = 0;
        bool isFull = false;
        mutable std::mutex bufferMutex;

    public:
        RingBuffer(int size = 0) : data(size) {}
        

        void push(const T& value) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (data.empty()) return; 

            data[head] = value;
            head = (head + 1) % data.size();
            if (head == 0) isFull = true;
        }




        void resize(int newSize) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            data.resize(newSize);
            head = 0;
            isFull = false;
        }



        void clear() {
            std::lock_guard<std::mutex> lock(bufferMutex);
            head = 0;
            isFull = false;
        }



        int getCurrentHead() const {
            std::lock_guard<std::mutex> lock(bufferMutex);
            return head;
        }



        std::vector<T> pullNewData(int& lastReadCursor) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            std::vector<T> newPoints;

            if (data.empty() || lastReadCursor == head) {
                return newPoints; 
            }

            if (lastReadCursor < head) {
                for (int i = lastReadCursor; i < head; ++i) {
                    newPoints.push_back(data[i]);
                }
            } else {
                for (int i = lastReadCursor; i < data.size(); ++i) {
                    newPoints.push_back(data[i]);
                }
                for (int i = 0; i < head; ++i) {
                    newPoints.push_back(data[i]);
                }
            }

            lastReadCursor = head;
            return newPoints;
        }

};