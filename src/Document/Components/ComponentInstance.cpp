#include "ComponentInstance.h"

#include "Utils/Log.h"
#include "Document/Project.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"





QMap<QString, QPair<int, QString>> ComponentInstance::getActiveConnections() const {
    QMap<QString, QPair<int, QString>> activeConnections;
    for (ComponentInstance* child : children) {
        activeConnections.insert(child->parentConnector, qMakePair(child->uid, child->selfConnector));
    }
    if (parentUid != -1) {
        activeConnections.insert(selfConnector, qMakePair(parentUid, parentConnector));
    }
    return activeConnections;
}





QList<QString> ComponentInstance::getFreeConnections() const {
    QList<QString> freeConnections;
    QSet<QString> occupiedConnectors;
    for (ComponentInstance* child : children) {
        occupiedConnectors.insert(child->parentConnector);
    }
    occupiedConnectors.insert(selfConnector);
    for (const QString& connId : blueprint->connectors.keys()) {
        if (!occupiedConnectors.contains(connId)) {
            freeConnections.append(connId);
        }
    }
    return freeConnections;
}





/*
 * Initializes the IO memory buffers based on the component's blueprint.
 * Registers active channels with the central TelemetryRegistry for tracking.
 */
void ComponentInstance::initializeIO() {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (!blueprint) return;

    auto& registry = TelemetryRegistry::getInstance();
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const QString& key : blueprint->inputDefs.keys()) {
        auto stream = std::make_shared<IOStream>();
        const IODef& def = blueprint->inputDefs[key];
        
        ChannelMeta meta;
        meta.name = prefix + def.name;
        meta.source = SourceKind::INPUT;
        meta.physical = def.physical;
        meta.dim = def.dim;

        if (def.channelType == "scalar") stream->channelId = registry.registerScalar(meta);
        else if (def.channelType == "vector") stream->channelId = registry.registerVector(meta);
        else if (def.channelType == "image") stream->channelId = registry.registerImage(meta);

        actuators[key] = stream;

        QString targetJoint = def.targetJoint;
        if (!joints.contains(targetJoint)) {
            joints[targetJoint] = std::make_shared<IOStream>();
        }
    }

    for (const QString& key : blueprint->outputDefs.keys()) {
        auto stream = std::make_shared<IOStream>();
        const IODef& def = blueprint->outputDefs[key];
        
        ChannelMeta meta;
        meta.name = prefix + def.name;
        meta.source = SourceKind::OUTPUT;
        meta.physical = def.physical;
        meta.dim = def.dim;

        if (def.channelType == "scalar") stream->channelId = registry.registerScalar(meta);
        else if (def.channelType == "vector") stream->channelId = registry.registerVector(meta);
        else if (def.channelType == "image") stream->channelId = registry.registerImage(meta);

        sensors[key] = stream;
    }
}





/*
 * Thread-safe getters setters for actuator, joint, and sensor data.
 */

void ComponentInstance::setActuatorTarget(const QString& key, IOData& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        actuators[key]->targetData = data;
    }
}


IOData ComponentInstance::getActuatorTarget(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return actuators.contains(key) ? actuators.value(key)->targetData : IOData{0.0};
}



void ComponentInstance::setJointTarget(const QString& key, IOData& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (joints.contains(key)) {
        joints[key]->targetData = data;
    }
}


IOData ComponentInstance::getJointTarget(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return joints.contains(key) ? joints.value(key)->targetData : IOData{0.0};
}



void ComponentInstance::setSensorCurrent(const QString& key, IOData& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        sensors[key]->currentData = data;
    }
}



IOData ComponentInstance::getSensorCurrent(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return sensors.contains(key) ? sensors.value(key)->currentData : IOData{0.0};
}





/*
 * Retrieves the global TelemetryRegistry channel ID linked to a specific actuator.
 */
int ComponentInstance::getActuatorChannelId(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        return actuators.value(key)->channelId;
    }
    return -1;
}





/*
 * Retrieves the global TelemetryRegistry channel ID linked to a specific sensor.
 */
int ComponentInstance::getSensorChannelId(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        return sensors.value(key)->channelId;
    }
    return -1;
}





/*
 * Safely caches the pre-computed MuJoCo integer ID for an IO stream.
 */
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