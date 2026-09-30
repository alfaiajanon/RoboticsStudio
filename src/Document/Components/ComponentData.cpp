#include "ComponentData.h"
#include "Utils/Graph.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>




static Transform parseTransform(const QJsonObject& obj) {
    Transform t;
    if (obj.contains("transform")) {
        QJsonObject tObj = obj["transform"].toObject();

        if (tObj.contains("pos")) {
            QJsonArray posArr = tObj["pos"].toArray();
            if (posArr.size() == 3) {
                t.position = Position(posArr[0].toDouble(), posArr[1].toDouble(), posArr[2].toDouble());
            }
        }

        if (tObj.contains("quat")) {
            QJsonArray quatArr = tObj["quat"].toArray();
            if (quatArr.size() == 4) {
                t.rotation = Rotation(quatArr[0].toDouble(), quatArr[1].toDouble(), quatArr[2].toDouble(), quatArr[3].toDouble());
            }
        }
    }
    return t;
}

static QJsonObject transformToJson(const Transform& t) {
    QJsonObject obj;
    obj["pos"] = QJsonArray{t.position.x, t.position.y, t.position.z};
    obj["quat"] = QJsonArray{t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z};
    return obj;
}

static QJsonArray doubleListToJson(const QList<double>& list) {
    QJsonArray arr;
    for (double v : list) arr.append(v);
    return arr;
}




/*
 * NOTE: previously baked the modelId_/mat_modelId_ MuJoCo naming convention
 * directly into geom.mesh/geom.material at load time -- that's an XML
 * generation concern, not raw data, and made this unwritable by toJson
 * (couldn't cleanly recover the original key). Now stores the raw
 * resource key exactly as authored; ComponentBlueprint applies the prefix
 * itself, only when actually generating MuJoCo XML.
 */
static QMap<QString, Node> parseBodies(const QJsonArray& bodiesArr) {
    QMap<QString, Node> parsedBodies;
    for (const auto& val : bodiesArr) {
        QJsonObject bObj = val.toObject();
        Node node;
        node.id = bObj["id"].toString();
        node.overrideGeom=bObj["overrideGeom"].toBool(false);
        if(node.overrideGeom){
            node.inertia=bObj["inertia"].toDouble();
            node.mass = bObj["mass"].toDouble(0.0);
        }
        node.localTransform = parseTransform(bObj);

        QJsonArray geomsArr = bObj["geoms"].toArray();
        for (const auto& gVal : geomsArr) {
            QJsonObject gObj = gVal.toObject();
            Geom geom;
            geom.mass = gObj["mass"].toDouble(0.0);

            geom.type = gObj["type"].toString();
            if (gObj.contains("mesh")) geom.mesh = gObj["mesh"].toString();

            if (gObj.contains("material")) geom.material = gObj["material"].toString();
            if (gObj.contains("color") && gObj["material"].toString().isEmpty()) {
                QJsonArray colorArr = gObj["color"].toArray();
                for (int i = 0; i < colorArr.size(); ++i) geom.color.append(colorArr[i].toDouble());
            }

            if (gObj.contains("size")) {
                QJsonArray sizeArr = gObj["size"].toArray();
                for (int i = 0; i < sizeArr.size(); ++i) geom.size.append(sizeArr[i].toDouble());
            }

            if (gObj.contains("pos")) {
                QJsonArray posArr = gObj["pos"].toArray();
                if (posArr.size() == 3) {
                    geom.pos = Position(posArr[0].toDouble(), posArr[1].toDouble(), posArr[2].toDouble());
                }
            }

            if (gObj.contains("quat")) {
                QJsonArray quatArr = gObj["quat"].toArray();
                if (quatArr.size() == 4) {
                    geom.rot = Rotation(quatArr[0].toDouble(), quatArr[1].toDouble(), quatArr[2].toDouble(), quatArr[3].toDouble());
                }
            }

            node.geoms.append(geom);
        }

        QJsonArray sitesArr = bObj["sites"].toArray();
        for (const auto& sVal : sitesArr) {
            QJsonObject sObj = sVal.toObject();
            Site site;
            site.id = sObj["id"].toString();
            site.localTransform = parseTransform(sObj);

            if (sObj.contains("sensor")) {
                QJsonObject sensObj = sObj["sensor"].toObject();
                site.sensor.type = sensObj["type"].toString();
            }
            node.sites.append(site);
        }

        parsedBodies[node.id] = node;
    }
    return parsedBodies;
}

static QJsonObject geomToJson(const Geom& g) {
    QJsonObject obj;
    obj["type"] = g.type;
    if (!g.mesh.isEmpty()) obj["mesh"] = g.mesh;         // raw resource key -- see parseBodies note
    if (!g.material.isEmpty()) obj["material"] = g.material; // raw resource key
    if (!g.size.isEmpty()) obj["size"] = doubleListToJson(g.size);
    obj["mass"] = g.mass;
    obj["pos"] = QJsonArray{g.pos.x, g.pos.y, g.pos.z};
    obj["quat"] = QJsonArray{g.rot.w, g.rot.x, g.rot.y, g.rot.z};
    if (g.material.isEmpty() && !g.color.isEmpty()) obj["color"] = doubleListToJson(g.color);
    return obj;
}

static QJsonObject sensorDefToJson(const SensorDef& s) {
    QJsonObject obj;
    obj["type"] = s.type;
    return obj;
}

static QJsonObject siteToJson(const Site& s) {
    QJsonObject obj;
    obj["id"] = s.id;
    obj["transform"] = transformToJson(s.localTransform);
    if (!s.sensor.type.isEmpty()) obj["sensor"] = sensorDefToJson(s.sensor);
    return obj;
}

static QJsonObject nodeToJson(const Node& n) {
    QJsonObject obj;
    obj["id"] = n.id;
    obj["overrideGeom"]=n.overrideGeom;
    obj["mass"] = n.mass;
    obj["inertia"]=n.inertia;
    obj["transform"] = transformToJson(n.localTransform);

    QJsonArray geomsArr;
    for (const Geom& g : n.geoms) geomsArr.append(geomToJson(g));
    obj["geoms"] = geomsArr;

    if (!n.sites.isEmpty()) {
        QJsonArray sitesArr;
        for (const Site& s : n.sites) sitesArr.append(siteToJson(s));
        obj["sites"] = sitesArr;
    }
    return obj;
}




static QList<Edge> parseJoints(const QJsonArray& jointsArr) {
    QList<Edge> parsedJoints;
    for (const auto& val : jointsArr) {
        QJsonObject jObj = val.toObject();
        Edge edge;
        edge.id = jObj["id"].toString();
        edge.type = jObj["type"].toString();
        edge.bodyA = jObj["body_a"].toString();
        edge.bodyB = jObj["body_b"].toString();
        edge.damping = jObj["damping"].toDouble(0.0);
        edge.armature = jObj["armature"].toDouble(0.0);
        edge.collision = jObj["collision"].toBool(true);
        edge.frictionloss = jObj["frictionloss"].toDouble(0.0);
        edge.localTransform = parseTransform(jObj);

        if (jObj.contains("range")) {
            QJsonArray rangeArr = jObj["range"].toArray();
            if (rangeArr.size() == 2) {
                edge.range.append(rangeArr[0].toDouble());
                edge.range.append(rangeArr[1].toDouble());
            }
        }

        if (jObj.contains("actuator")) {
            QJsonObject actObj = jObj["actuator"].toObject();
            edge.actuator.type = actObj["type"].toString();
            edge.actuator.kp = actObj["kp"].toDouble(0.0);
            edge.actuator.kv = actObj["kv"].toDouble(0.0);

            if (actObj.contains("ctrlrange")) {
                QJsonArray crArr = actObj["ctrlrange"].toArray();
                if (crArr.size() == 2) {
                    edge.actuator.ctrlrange.append(crArr[0].toDouble());
                    edge.actuator.ctrlrange.append(crArr[1].toDouble());
                }
            }
            if (actObj.contains("forcerange")) {
                QJsonArray frArr = actObj["forcerange"].toArray();
                if (frArr.size() == 2) {
                    edge.actuator.forceRange.append(frArr[0].toDouble());
                    edge.actuator.forceRange.append(frArr[1].toDouble());
                }
            }
        }

        if (jObj.contains("sensor")) {
            QJsonObject sensObj = jObj["sensor"].toObject();
            edge.sensor.type = sensObj["type"].toString();
        }

        parsedJoints.append(edge);
    }
    return parsedJoints;
}

static QJsonObject actuatorDefToJson(const ActuatorDef& a) {
    QJsonObject obj;
    obj["type"] = a.type;
    if (!a.ctrlrange.isEmpty()) obj["ctrlrange"] = doubleListToJson(a.ctrlrange);
    if (!a.forceRange.isEmpty()) obj["forcerange"] = doubleListToJson(a.forceRange);
    obj["kp"] = a.kp;
    obj["kv"] = a.kv;
    return obj;
}

static QJsonObject edgeToJson(const Edge& e) {
    QJsonObject obj;
    obj["id"] = e.id;
    obj["body_a"] = e.bodyA;
    obj["body_b"] = e.bodyB;
    obj["type"] = e.type;
    obj["transform"] = transformToJson(e.localTransform);
    if (!e.range.isEmpty()) obj["range"] = doubleListToJson(e.range);
    obj["damping"] = e.damping;
    obj["armature"] = e.armature;
    obj["frictionloss"] = e.frictionloss;
    obj["collision"] = e.collision;
    if (!e.actuator.type.isEmpty()) obj["actuator"] = actuatorDefToJson(e.actuator);
    if (!e.sensor.type.isEmpty()) obj["sensor"] = sensorDefToJson(e.sensor);
    return obj;
}




static QMap<QString, ConnectorDef> parseConnectors(const QJsonArray& connectorsArr) {
    QMap<QString, ConnectorDef> conns;
    for (const auto& val : connectorsArr) {
        QJsonObject connObj = val.toObject();
        ConnectorDef connDef;
        connDef.id = connObj["id"].toString();
        connDef.body = connObj["body"].toString();
        connDef.description = connObj["description"].toString();
        connDef.transform = parseTransform(connObj);

        QJsonObject mechObj = connObj["mechanics"].toObject();
        if (mechObj.contains("snap_angles")) {
            QJsonArray anglesArr = mechObj["snap_angles"].toArray();
            for (int i = 0; i < anglesArr.size(); ++i) {
                connDef.mechanics.snapAngles.append(static_cast<float>(anglesArr[i].toDouble()));
            }
        }
        conns[connDef.id] = connDef;
    }
    return conns;
}

static QJsonObject connectorToJson(const ConnectorDef& c) {
    QJsonObject obj;
    obj["id"] = c.id;
    obj["body"] = c.body;
    obj["description"] = c.description;
    obj["transform"] = transformToJson(c.transform);
    if (!c.mechanics.snapAngles.isEmpty()) {
        QJsonObject mech;
        QJsonArray angles;
        for (float a : c.mechanics.snapAngles) angles.append(a);
        mech["snap_angles"] = angles;
        obj["mechanics"] = mech;
    }
    return obj;
}




static QMap<QString, IODef> parseIO(const QJsonArray& ioArr) {
    QMap<QString, IODef> io;
    for (const auto& val : ioArr) {
        QJsonObject ioObj = val.toObject();
        IODef def;
        def.name = ioObj["name"].toString();
        def.unit = ioObj["unit"].toString();
        def.dataType = ioObj["data_type"].toString();
        def.channelType = ioObj["channel_type"].toString("scalar");
        def.physical = ioObj["physical"].toBool(true);
        def.targetJoint = ioObj["target_joint"].toString();
        def.targetSite = ioObj["target_site"].toString();
        def.dim = ioObj["dim"].toInt(1);
        if (ioObj.contains("componentLabels")) {
            for (const auto& lv : ioObj["componentLabels"].toArray())
                def.componentLabels.append(lv.toString());
        }

        if (ioObj.contains("range")) {
            QJsonArray rangeArr = ioObj["range"].toArray();
            if (rangeArr.size() == 2) {
                def.ranged=true;
                def.range = qMakePair(static_cast<float>(rangeArr[0].toDouble()), static_cast<float>(rangeArr[1].toDouble()));
            }
        }

        // Previously dropped on load despite being real IODef fields:
        if (ioObj.contains("pin_required")) {
            for (const auto& pv : ioObj["pin_required"].toArray())
                def.pinsRequired.append(pv.toString());
        }
        if (ioObj.contains("camera_name")) {
            def.cameraName = ioObj["camera_name"].toString();
        }
        if (ioObj.contains("resolution")) {
            QJsonArray resArr = ioObj["resolution"].toArray();
            if (resArr.size() == 2) {
                def.resolution = qMakePair(resArr[0].toInt(), resArr[1].toInt());
            }
        }

        io[def.name] = def;
    }
    return io;
}

static QJsonObject ioDefToJson(const IODef& def) {
    QJsonObject obj;
    obj["name"] = def.name;
    obj["unit"] = def.unit;
    obj["data_type"] = def.dataType;
    obj["channel_type"] = def.channelType;
    obj["physical"] = def.physical;
    if (!def.targetJoint.isEmpty()) obj["target_joint"] = def.targetJoint;
    if (!def.targetSite.isEmpty()) obj["target_site"] = def.targetSite;
    obj["dim"] = def.dim;
    if (!def.componentLabels.isEmpty()) {
        QJsonArray labelsArr;
        for (const QString& l : def.componentLabels) labelsArr.append(l);
        obj["componentLabels"] = labelsArr;
    }
    if (def.range.first != 0.0f || def.range.second != 0.0f) {
        obj["range"] = QJsonArray{def.range.first, def.range.second};
    }
    if (!def.pinsRequired.isEmpty()) {
        QJsonArray pinsArr;
        for (const QString& p : def.pinsRequired) pinsArr.append(p);
        obj["pin_required"] = pinsArr;
    }
    if (!def.cameraName.isEmpty()) obj["camera_name"] = def.cameraName;
    if (def.resolution.first != 0 || def.resolution.second != 0) {
        obj["resolution"] = QJsonArray{def.resolution.first, def.resolution.second};
    }
    return obj;
}




static QList<PinDef> parsePins(const QJsonArray& pinsArr) {
    QList<PinDef> pins;
    for (const auto& val : pinsArr) {
        QJsonObject pObj = val.toObject();
        PinDef pin;
        pin.id = pObj["id"].toString();
        pin.description = pObj["description"].toString();
        pin.voltageRange = qMakePair(0.0f, 0.0f);
        if (pObj.contains("voltage_range")) {
            QJsonArray rangeArr = pObj["voltage_range"].toArray();
            if (rangeArr.size() == 2) {
                pin.voltageRange = qMakePair(static_cast<float>(rangeArr[0].toDouble()),
                                             static_cast<float>(rangeArr[1].toDouble()));
            }
        }
        pins.append(pin);
    }
    return pins;
}

static QJsonArray pinsToJson(const QList<PinDef>& pins) {
    QJsonArray arr;
    for (const PinDef& pin : pins) {
        QJsonObject obj;
        obj["id"] = pin.id;
        if (!pin.description.isEmpty()) obj["description"] = pin.description;
        if (pin.voltageRange.first != 0.0f || pin.voltageRange.second != 0.0f) {
            obj["voltage_range"] = QJsonArray{pin.voltageRange.first, pin.voltageRange.second};
        }
        arr.append(obj);
    }
    return arr;
}




ComponentData ComponentData::fromJson(const QJsonObject& mainJson, const QString& basePath) {
    ComponentData data;

    data.basePath = basePath;
    data.modelId = mainJson["id"].toString();

    QJsonObject metaObj = mainJson["meta"].toObject();
    data.meta.name = metaObj["name"].toString();
    data.meta.version = metaObj["version"].toString();
    data.meta.author = metaObj["author"].toString();
    data.meta.iconPath = metaObj["icon_path"].toString().isEmpty()
                                ? QString()
                                : QDir(data.basePath).filePath(metaObj["icon_path"].toString());

    data.pins = parsePins(mainJson["pins"].toArray());
    data.specs = mainJson["specs"].toObject();

    QJsonObject resourcesObj = mainJson["resources"].toObject();
    QJsonObject meshesObj = resourcesObj["meshes"].toObject();
    for (const QString& key : meshesObj.keys()) {
        data.meshResources[key] = QDir(data.basePath).filePath(meshesObj[key].toString());
    }

    QJsonObject materialsObj = resourcesObj["materials"].toObject();
    for (const QString& key : materialsObj.keys()) {
        data.materialResources[key] = QDir(data.basePath).filePath(materialsObj[key].toString());
    }

    QJsonObject kinematicsObj = mainJson["kinematics"].toObject();
    data.defaultBodyId = kinematicsObj["default_body"].toString();

    QJsonArray bodiesArr = kinematicsObj["bodies"].toArray();
    data.bodies = parseBodies(bodiesArr); // no longer takes modelId -- see parseBodies note

    QJsonArray jointsArr = kinematicsObj["joints"].toArray();
    data.joints = parseJoints(jointsArr);

    QJsonArray connectorsArr = mainJson["connectors"].toArray();
    data.connectors = parseConnectors(connectorsArr);

    QJsonObject io = mainJson["io"].toObject();
    data.inputDefs = parseIO(io["inputs"].toArray());
    data.outputDefs = parseIO(io["outputs"].toArray());

    EmulatorDef emuDef;
    if (mainJson.contains("emulator")) {
        QJsonObject emuObj = mainJson["emulator"].toObject();
        emuDef.type = emuObj["type"].toString();
        emuDef.source = emuObj["source"].toObject();
        if (emuObj.contains("parameters")) {
            QJsonObject paramsObj = emuObj["parameters"].toObject();
            for (const QString& key : paramsObj.keys()) {
                emuDef.parameters[key] = paramsObj[key].toVariant();
            }
        }
    }
    data.emulatorDef = emuDef;

    return data;
}




QJsonObject ComponentData::toJson() const {
    QJsonObject root;
    root["id"] = modelId; // NOTE: mirrors fromJson's top-level "id" read -- same
                           // verification needed; move under "meta" if that's
                           // where real files actually keep it.

    QJsonObject metaObj;
    metaObj["name"] = meta.name;
    metaObj["version"] = meta.version;
    metaObj["author"] = meta.author;
    metaObj["icon_path"] = meta.iconPath.isEmpty()
                            ? QString()
                            : QDir(basePath).relativeFilePath(meta.iconPath);
    root["meta"] = metaObj;

    root["specs"] = specs;
    root["pins"] = pinsToJson(pins);

    // Resource paths are stored absolute in memory (resolved against
    // basePath on load) -- converted back to relative here so a saved
    // .rsdef doesn't bake in a path specific to whoever last saved it.
    QJsonObject resourcesObj;
    QJsonObject meshesObj;
    for (auto it = meshResources.constBegin(); it != meshResources.constEnd(); ++it) {
        meshesObj[it.key()] = QDir(basePath).relativeFilePath(it.value());
    }
    resourcesObj["meshes"] = meshesObj;

    QJsonObject materialsObj;
    for (auto it = materialResources.constBegin(); it != materialResources.constEnd(); ++it) {
        materialsObj[it.key()] = QDir(basePath).relativeFilePath(it.value());
    }
    resourcesObj["materials"] = materialsObj;
    root["resources"] = resourcesObj;

    QJsonObject kinematicsObj;
    kinematicsObj["default_body"] = defaultBodyId;

    QJsonArray bodiesArr;
    for (const Node& n : bodies) bodiesArr.append(nodeToJson(n));
    kinematicsObj["bodies"] = bodiesArr;

    QJsonArray jointsArr;
    for (const Edge& e : joints) jointsArr.append(edgeToJson(e));
    kinematicsObj["joints"] = jointsArr;

    root["kinematics"] = kinematicsObj;

    QJsonArray connectorsArr;
    for (const ConnectorDef& c : connectors) connectorsArr.append(connectorToJson(c));
    root["connectors"] = connectorsArr;

    QJsonObject ioObj;
    QJsonArray inputsArr;
    for (const IODef& d : inputDefs) inputsArr.append(ioDefToJson(d));
    ioObj["inputs"] = inputsArr;
    QJsonArray outputsArr;
    for (const IODef& d : outputDefs) outputsArr.append(ioDefToJson(d));
    ioObj["outputs"] = outputsArr;
    root["io"] = ioObj;

    QJsonObject emulatorObj;
    emulatorObj["type"] = emulatorDef.type;
    emulatorObj["source"] = emulatorDef.source;
    QJsonObject paramsObj;
    for (auto it = emulatorDef.parameters.constBegin(); it != emulatorDef.parameters.constEnd(); ++it) {
        paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    emulatorObj["parameters"] = paramsObj;
    root["emulator"] = emulatorObj;

    return root;
}
