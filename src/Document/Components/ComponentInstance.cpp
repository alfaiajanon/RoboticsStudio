#include "ComponentInstance.h"

#include "Document/Project.h"
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


/*
 * Builds the runtime value maps from the blueprint's interface. Signal slots
 * are keyed by signal name, joint slots by joint id (D8). Sizes come from the
 * DeviceTypes table via ComponentData::interfaceDim. Image signals
 * (camera outputs, display inputs) get an ImageIOValue sized from their device.
 */
void ComponentInstance::setBlueprint(ComponentBlueprint *blueprint){
    this->blueprint = blueprint;

    auto makeValue = [](int dim) {
        auto data = std::make_shared<BasicIOValue>();
        data->dim = dim;
        data->data.resize(dim);
        return data;
    };

    auto makeImage = [blueprint](const TargetRef& target) {
        auto img = std::make_shared<ImageIOValue>();
        if (target.kind == "camera") {
            for (const CameraDef& c : blueprint->cameras) {
                if (c.id != target.id) continue;
                img->width = c.resolution.first;
                img->height = c.resolution.second;
                img->format = c.type == "depth" ? ImageFormat::Depth32F : ImageFormat::RGB888;
            }
        } else if (target.kind == "display") {
            for (const DisplayDef& d : blueprint->displays) {
                if (d.id != target.id) continue;
                img->width = d.resolution.first;
                img->height = d.resolution.second;
            }
        }
        return img;
    };

    for (auto it = blueprint->interfaceInputs.constBegin(); it != blueprint->interfaceInputs.constEnd(); ++it) {
        const InterfaceDef &def = it.value();
        int dim = blueprint->interfaceDim(def);
        if (dim > 0) actuators[it.key()] = makeValue(dim);
        else if (def.target.kind == "display") images[it.key()] = makeImage(def.target);

        if (def.target.kind == "joint" && !joints.contains(def.target.id)) {
            joints[def.target.id] = makeValue(1);
        }
    }
    for (auto it = blueprint->interfaceOutputs.constBegin(); it != blueprint->interfaceOutputs.constEnd(); ++it) {
        int dim = blueprint->interfaceDim(it.value());
        if (dim > 0) sensors[it.key()] = makeValue(dim);
        else if (it.value().target.kind == "camera") images[it.key()] = makeImage(it.value().target);
    }
}







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



void ComponentInstance::setImageValue(const QString& key, ImageIOValue& data) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (images.contains(key)) {
        *images[key] = data;
    }
}

ImageIOValue ComponentInstance::getImageValue(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    auto it = images.constFind(key);
    if (it != images.constEnd() && *it != nullptr) {
        return *(*it);
    }
    return ImageIOValue{};
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
