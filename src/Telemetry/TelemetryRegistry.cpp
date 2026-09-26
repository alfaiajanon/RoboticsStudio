
#include "TelemetryRegistry.h"
#include "Telemetry/Sources/TelemetrySource.h"
#include <qlist.h>
#include <qobject.h>




// /*
//  * Inserts a new data point into the ring buffer.
//  * Automatically wraps the head index based on the allocated capacity.
//  */
// void ScalarChannel::push(double time, double value) {
//     if (!isActive()) return;
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     buffer[head] = {time, value};
//     head = (head + 1) % capacity;
// }




// /*
//  * Safely extracts an ordered copy of the ring buffer for UI rendering.
//  * Starts from the oldest available data point and wraps to the newest.
//  */
// std::vector<ScalarPoint> ScalarChannel::snapshot() {
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     std::vector<ScalarPoint> result;
//     result.reserve(capacity);
//     for (size_t i = 0; i < capacity; ++i) {
//         size_t idx = (head + i) % capacity;
//         if (buffer[idx].time > 0) {
//             result.push_back(buffer[idx]);
//         }
//     }
//     return result;
// }




// /*
//  * Inserts a multi-dimensional data point into the vector ring buffer.
//  * Validates the input dimension against the channel metadata before writing.
//  */
// void VectorChannel::push(double time, const std::vector<double>& value) {
//     if (!isActive() || value.size() != meta.dim) return;
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     buffer[head] = {time, value};
//     head = (head + 1) % capacity;
// }




// /*
//  * Safely extracts an ordered copy of the vector ring buffer for UI rendering.
//  * Reconstructs the timeline from the oldest to newest points.
//  */
// std::vector<VectorPoint> VectorChannel::snapshot() {
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     std::vector<VectorPoint> result;
//     result.reserve(capacity);
//     for (size_t i = 0; i < capacity; ++i) {
//         size_t idx = (head + i) % capacity;
//         if (buffer[idx].time > 0) {
//             result.push_back(buffer[idx]);
//         }
//     }
//     return result;
// }




// /*
//  * Overwrites the background frame buffer with new pixel data.
//  * Flags the buffer swap as ready for the next UI polling cycle.
//  */
// void ImageChannel::push(double time, const unsigned char* pixels, size_t size) {
//     if (!isActive()) return;
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     backBuffer.time = time;
//     backBuffer.pixels.assign(pixels, pixels + size);
//     isNewFrameAvailable = true;
// }




// /*
//  * Swaps the front and back buffers if a new frame was completely rendered.
//  * Returns true if the output frame contains fresh data, false if no update occurred.
//  */
// bool ImageChannel::snapshot(ImageFrame& outFrame) {
//     std::lock_guard<std::mutex> lock(bufferMutex);
//     if (!isNewFrameAvailable) return false;

//     frontBuffer = std::move(backBuffer);
//     isNewFrameAvailable = false;
//     outFrame = frontBuffer;
//     return true;
// }




/*
 * Retrieves the global singleton instance of the TelemetryRegistry.
 */
TelemetryRegistry& TelemetryRegistry::getInstance() {
    static TelemetryRegistry instance;
    return instance;
}




// /*
//  * Instantiates and registers a new Scalar channel.
//  * Returns the stable integer ID used to identify the channel across the application.
//  */
// int TelemetryRegistry::registerScalar(const ChannelMeta& meta) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     int id = nextId++;
//     scalarChannels.insert(id, std::make_shared<ScalarChannel>(id, meta));
//     return id;
// }




// /*
//  * Instantiates and registers a new Vector channel.
//  * Returns the stable integer ID used to identify the channel across the application.
//  */
// int TelemetryRegistry::registerVector(const ChannelMeta& meta) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     int id = nextId++;
//     vectorChannels.insert(id, std::make_shared<VectorChannel>(id, meta));
//     return id;
// }




// /*
//  * Instantiates and registers a new Image channel.
//  * Returns the stable integer ID used to identify the channel across the application.
//  */
// int TelemetryRegistry::registerImage(const ChannelMeta& meta) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     int id = nextId++;
//     imageChannels.insert(id, std::make_shared<ImageChannel>(id, meta));
//     return id;
// }




// /*
//  * Retrieves a pointer to a specific Scalar channel by its unique ID.
//  * Returns nullptr if the ID does not exist.
//  */
// std::shared_ptr<ScalarChannel> TelemetryRegistry::getScalar(int id) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     return scalarChannels.value(id, nullptr);
// }




// /*
//  * Retrieves a pointer to a specific Vector channel by its unique ID.
//  * Returns nullptr if the ID does not exist.
//  */
// std::shared_ptr<VectorChannel> TelemetryRegistry::getVector(int id) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     return vectorChannels.value(id, nullptr);
// }




// /*
//  * Retrieves a pointer to a specific Image channel by its unique ID.
//  * Returns nullptr if the ID does not exist.
//  */
// std::shared_ptr<ImageChannel> TelemetryRegistry::getImage(int id) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     return imageChannels.value(id, nullptr);
// }




// /*
//  * Collects a list of all active channel IDs that match the requested data type.
//  * Used primarily by the UI to populate the "Add Target" selection dialogs.
//  */
// QList<int> TelemetryRegistry::getChannelsOfType(DataType type) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     QList<int> ids;

//     if (type == DataType::SCALAR) {
//         ids.append(scalarChannels.keys());
//     } else if (type == DataType::VECTOR) {
//         ids.append(vectorChannels.keys());
//     } else if (type == DataType::IMAGE) {
//         ids.append(imageChannels.keys());
//     }
//     return ids;
// }




// /*
//  * Registers a producer-side telemetry source.
//  * The physics thread will call its capture() via captureAll() each tick.
//  */
// void TelemetryRegistry::addSource(const std::shared_ptr<TelemetrySource>& source) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     sources.append(source);
// }




// /*
//  * Removes the telemetry source feeding the given channel, if any.
//  */
// void TelemetryRegistry::removeSource(int channelId) {
//     std::lock_guard<std::mutex> lock(registryMutex);
//     for (int i = 0; i < sources.size(); ++i) {
//         if (sources[i]->channelId() == channelId) {
//             sources.removeAt(i);
//             return;
//         }
//     }
// }




/*
 * Lets every registered source pull its data from the live MuJoCo state.
 * Called from the physics thread while physicsMutex is held.
 */
void TelemetryRegistry::captureAll(double time) {
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto& source : sources) {
        source->capture(time);
    }
}




/*
 * Clears all registered channels and resets the registry state.
 * Expected to be called when a project is unloaded or reset.
 */
void TelemetryRegistry::clearAll() {
    std::lock_guard<std::mutex> lock(registryMutex);
    sources.clear();
}



void TelemetryRegistry::clearInactive() {
    std::lock_guard<std::mutex> lock(registryMutex);
    for(auto& source: sources){
        if(source->isActive()) continue;

        sources.remove(source->getKey());
    }
}















shared_ptr<TelemetrySource> TelemetryRegistry::getOrCreateSource(const QString& key,
                                                                 const function<shared_ptr<TelemetrySource>()>& factory) {
    lock_guard<mutex> lock(registryMutex);

    auto it = sources.find(key);
    if (it != sources.end()) return it.value();

    auto created = factory();
    created->setKey(key);
    sources.insert(key, created);
    return created;
}



shared_ptr<TelemetrySource> TelemetryRegistry::getSource(const QString& key) const {
    auto it = sources.find(key);
    return it != sources.end() ? it.value() : nullptr;
}



QList<QString> TelemetryRegistry::getKeysOfType(DataType type) const {
    QList<QString> keys;
    for (const auto& source : sources) {
        if (source->getDataType() == type) {
            keys.append(source->getKey());
        }
    }
    return keys;
}
