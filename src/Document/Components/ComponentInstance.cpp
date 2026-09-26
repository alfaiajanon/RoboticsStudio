#include "ComponentInstance.h"

#include "Utils/Log.h"
#include "Document/Project.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "ComponentBlueprint.h"
#include <qobject.h>
#include <QString>





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




ComponentBlueprint* ComponentInstance::getBlueprint() const {
    return blueprint;
}


void ComponentInstance::setBlueprint(ComponentBlueprint *blueprint){
    this->blueprint = blueprint;

    for (const QString &key : blueprint->inputDefs.keys()) {
        auto data = std::make_shared<BasicIOValue>();
        const IODef &def = blueprint->inputDefs[key];
        if(def.channelType=="scalar") data->dim=1;
        if(def.channelType=="vector") data->dim=3;
        data->data.resize(data->dim);
        actuators[key] = data;

        if (!joints.contains(def.targetJoint)) {
            auto jdata=std::make_shared<BasicIOValue>();
            jdata->dim=1;
            jdata->data.resize(1);
            joints[def.targetJoint] = jdata;
        }
    }
    for (const QString& key : blueprint->outputDefs.keys()) {
        auto data = std::make_shared<BasicIOValue>();
        const IODef &def = blueprint->outputDefs[key];
        if(def.channelType=="scalar") data->dim=1;
        if(def.channelType=="vector") data->dim=3;
        data->data.resize(data->dim);

        sensors[key] = data;
    }
}





/*
 * Initializes the IO memory buffers based on the component's blueprint.
 * Registers active channels with the central TelemetryRegistry for tracking.
 */
// void ComponentInstance::initializeIO() {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (!blueprint) return;

//     auto& registry = TelemetryRegistry::getInstance();
//     QString prefix = "comp_" + QString::number(uid) + "_";

//     for (const QString& key : blueprint->inputDefs.keys()) {
//         auto stream = std::make_shared<IOStream>();
//         const IODef& def = blueprint->inputDefs[key];

//         ChannelMeta meta;
//         meta.name = prefix + def.name;
//         meta.source = SourceKind::INPUT;
//         meta.physical = def.physical;
//         meta.dim = def.dim;

//         if (def.channelType == "scalar") stream->channelId = registry.registerScalar(meta);
//         else if (def.channelType == "vector") stream->channelId = registry.registerVector(meta);
//         else if (def.channelType == "image") stream->channelId = registry.registerImage(meta);

//         actuators[key] = stream;

//         QString targetJoint = def.targetJoint;
//         if (!joints.contains(targetJoint)) {
//             joints[targetJoint] = std::make_shared<IOStream>();
//         }
//     }

//     for (const QString& key : blueprint->outputDefs.keys()) {
//         auto stream = std::make_shared<IOStream>();
//         const IODef& def = blueprint->outputDefs[key];

//         ChannelMeta meta;
//         meta.name = prefix + def.name;
//         meta.source = SourceKind::OUTPUT;
//         meta.physical = def.physical;
//         meta.dim = def.dim;

//         if (def.channelType == "scalar") stream->channelId = registry.registerScalar(meta);
//         else if (def.channelType == "vector") stream->channelId = registry.registerVector(meta);
//         else if (def.channelType == "image") stream->channelId = registry.registerImage(meta);

//         sensors[key] = stream;
//     }
// }





/*
 * Thread-safe getters setters for actuator, joint, and sensor data.
 */

void ComponentInstance::setActuatorValue(const QString& key, BasicIOValue& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (actuators.contains(key)) {
        actuators[key]->data = data.data;
        actuators[key]->mujocoId = data.mujocoId;
    }
}

void ComponentInstance::setJointValue(const QString& key, BasicIOValue& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (joints.contains(key)) {
        joints[key]->data=data.data;
        joints[key]->mujocoId=data.mujocoId;
        // Log::info(QString("Setting joint %1 to mjid %2").arg(key).arg(data.mujocoId));
    }
}

void ComponentInstance::setSensorValue(const QString& key, BasicIOValue& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (sensors.contains(key)) {
        sensors[key]->data=data.data;
        sensors[key]->mujocoId=data.mujocoId;
    }
}



BasicIOValue ComponentInstance::getActuatorValue(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    auto it = actuators.constFind(key);
    if (it != actuators.constEnd() && *it != nullptr) {
        return *(*it);
    }
    return BasicIOValue{0};
}

BasicIOValue ComponentInstance::getJointValue(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    auto it = joints.constFind(key);
    if (it != joints.constEnd() && *it != nullptr) {
        return *(*it);
    }
    return BasicIOValue{0};
}

BasicIOValue ComponentInstance::getSensorValue(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    auto it = sensors.constFind(key);
    if (it != sensors.constEnd() && *it != nullptr) {
        return *(*it);
    }
    return BasicIOValue{0};
}





/*
 * Retrieves the global TelemetryRegistry channel ID linked to a specific actuator.
 */
// int ComponentInstance::getActuatorChannelId(const QString& key) const {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (actuators.contains(key)) {
//         return actuators.value(key)->channelId;
//     }
//     return -1;
// }





// /*
//  * Retrieves the global TelemetryRegistry channel ID linked to a specific sensor.
//  */
// int ComponentInstance::getSensorChannelId(const QString& key) const {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (sensors.contains(key)) {
//         return sensors.value(key)->channelId;
//     }
//     return -1;
// }





/*
 * Safely caches the pre-computed MuJoCo integer ID for an IO stream.
 */
// void ComponentInstance::setMujocoActuatorId(const QString& key, int id) {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (actuators.contains(key)) {
//         actuators[key]->mujocoId = id;
//     }
// }





// void ComponentInstance::setMujocoSensorId(const QString& key, int id) {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (sensors.contains(key)) {
//         sensors[key]->mujocoId = id;
//     }
// }





// void ComponentInstance::setMujocoJointId(const QString& key, int id) {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (joints.contains(key)) {
//         joints[key]->mujocoId = id;
//     }
// }





// int ComponentInstance::getMujocoActuatorId(const QString& key) const {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (actuators.contains(key))
//         return actuators.value(key)->mujocoId;
//     return -1;
// }





// int ComponentInstance::getMujocoSensorId(const QString& key) const {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (sensors.contains(key))
//         return sensors.value(key)->mujocoId;
//     return -1;
// }





// int ComponentInstance::getMujocoJointId(const QString& key) const {
//     std::lock_guard<std::mutex> lock(ioMutex);
//     if (joints.contains(key))
//         return joints.value(key)->mujocoId;
//     return -1;
// }
