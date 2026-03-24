#include "ComponentInstance.h"
#include "Core/Log.h"



/*
 * Initializes the IO memory buffers based on the component's blueprint.
 * Pre-allocates streams and locks the mutex to prevent partial initialization reads.
 */
void ComponentInstance::initializeIO() {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (!blueprint) return;

    for (const QString& key : blueprint->inputDefs.keys()) {
        actuators[key] = std::make_shared<IOStream>();
    }
    for (const QString& key : blueprint->inputDefs.keys()) {
        QString targetJoint = this->blueprint->inputDefs[key].targetJoint;
        joints[targetJoint] = std::make_shared<IOStream>();
    }
    for (const QString& key : blueprint->outputDefs.keys()) {
        sensors[key] = std::make_shared<IOStream>();
    }
}




/*
 * Thread-safe setter for targets and present values.
 */
#pragma region IOSettersGetters

void ComponentInstance::setActuatorTarget(const QString& key, double value) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        actuators[key]->targetValue = value;
    }
}

double ComponentInstance::getActuatorTarget(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return actuators.contains(key) ? actuators.value(key)->targetValue : 0.0;
}



void ComponentInstance::setJointTarget(const QString& key, double value) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (joints.contains(key)) {
        joints[key]->targetValue = value;
    }
}

double ComponentInstance::getJointTarget(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return joints.contains(key) ? joints.value(key)->targetValue : 0.0;
}



void ComponentInstance::setSensorCurrent(const QString& key, double value) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        sensors[key]->currentValue = value;
    }
}

double ComponentInstance::getSensorCurrent(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return sensors.contains(key) ? sensors.value(key)->currentValue : 0.0;
}


RingBuffer<PlotPoint>* ComponentInstance::getSensorBuffer(const QString& key) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        return &sensors[key]->historyBuffer;
    }
    return nullptr;
}


RingBuffer<PlotPoint>* ComponentInstance::getActuatorBuffer(const QString& key) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        return &actuators[key]->historyBuffer;
    }
    return nullptr;
}







/*
 * Safely caches the pre-computed MuJoCo integer ID for an IO stream.
 * Binds the high-level UI component directly to the low-level C array index.
 */
#pragma region MujocoIDMapping

void ComponentInstance::setMujocoActuatorId(const QString& key, int id) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        actuators[key]->mujocoId = id;
    }
}

void ComponentInstance::setMujocoSensorId(const QString& key, int id) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        sensors[key]->mujocoId = id;
    }
}

void ComponentInstance::setMujocoJointId(const QString& key, int id) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (joints.contains(key)) {
        joints[key]->mujocoId = id;
    }
}


int ComponentInstance::getMujocoActuatorId(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) 
        return actuators.value(key)->mujocoId;
    return -1;
}

int ComponentInstance::getMujocoSensorId(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) 
    return sensors.value(key)->mujocoId;
    return -1;
}

int ComponentInstance::getMujocoJointId(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (joints.contains(key)) 
        return joints.value(key)->mujocoId;
    return -1;
}