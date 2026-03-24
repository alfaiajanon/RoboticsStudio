#include "ComponentBlueprint.h"
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include "Core/Log.h"





/*
 * Helper function to safely extract a Transform object from JSON.
 * Expects 'pos' array of 3 floats and 'quat' array of 4 floats.
 */
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





/*
 * Parses the .rsdef JSON file to construct the component's definitions.
 * Delegates specific block parsing to private helper methods.
 */
ComponentBlueprint::ComponentBlueprint(const QString& rsdefFile) {
    QFile file(rsdefFile);
    if(!file.open(QIODevice::ReadOnly)){
        Log::error("ComponentBlueprint: Unable to open rsdef file: " + rsdefFile);
        return;
    }

    QJsonObject mainJson = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QFileInfo rsdefFileInfo(rsdefFile);
    basePath = rsdefFileInfo.absoluteDir().absolutePath();

    QJsonObject metaObj = mainJson["meta"].toObject();
    meta.id = metaObj["id"].toString();
    meta.name = metaObj["name"].toString();
    meta.version = metaObj["version"].toString();
    meta.author = metaObj["author"].toString();
    meta.iconPath = metaObj["icon_path"].toString();

    modelId = meta.id; 

    QJsonArray pinsArray = mainJson["pins"].toArray();
    for(const auto& val : pinsArray){
        QJsonObject pinObj = val.toObject();
        PinDef pinDef;
        pinDef.id = pinObj["id"].toString();
        pinDef.description = pinObj["description"].toString();
        
        if (pinObj.contains("voltage_range")) {
            QJsonArray vRange = pinObj["voltage_range"].toArray();
            if (vRange.size() == 2) {
                pinDef.voltageRange = qMakePair(vRange[0].toDouble(), vRange[1].toDouble());
            }
        }
        pins[pinDef.id] = pinDef;
    }

    if (mainJson.contains("resources")) parseResources(mainJson["resources"].toObject());
    if (mainJson.contains("kinematics")) parseKinematics(mainJson["kinematics"].toObject());
    if (mainJson.contains("connectors")) parseConnectors(mainJson["connectors"].toArray());
    if (mainJson.contains("io")) parseIO(mainJson["io"].toObject());

    Log::info("ComponentBlueprint: Loaded model ID: " + modelId);
}





/*
 * Generates MuJoCo <asset> tags for meshes and materials.
 * Resolves relative paths to absolute filesystem paths.
 */
void ComponentBlueprint::parseResources(const QJsonObject& resourcesObj) {
    assetXML = "";
    
    QJsonObject meshesObj = resourcesObj["meshes"].toObject();
    for (const QString& key : meshesObj.keys()) {
        QString relPath = meshesObj[key].toString();
        QString absPath = QDir(basePath).filePath(relPath);
        assetXML += QString("    <mesh name=\"%1_%2\" file=\"%3\" scale=\"1 1 1\"/>\n")
                        .arg(modelId).arg(key).arg(absPath);
    }
    
    QJsonObject materialsObj = resourcesObj["materials"].toObject();
    for (const QString& key : materialsObj.keys()) {
        QString relPath = materialsObj[key].toString();
        QString absPath = QDir(basePath).filePath(relPath);
        
        assetXML += QString("    <texture name=\"tex_%1_%2\" file=\"%3\" type=\"2d\"/>\n")
                        .arg(modelId).arg(key).arg(absPath);
        assetXML += QString("    <material name=\"mat_%1_%2\" texture=\"tex_%1_%2\" specular=\"0.3\"/>\n")
                        .arg(modelId).arg(key);
    }
}





/*
 * Parses the physical structure into the bidirectional KinematicGraph.
 * Extracts Nodes (bodies) and Edges (joints) using the unified Transform standard.
 */
void ComponentBlueprint::parseKinematics(const QJsonObject& kinematicsObj) {
    kinematics.clear();

    QJsonArray bodiesArr = kinematicsObj["bodies"].toArray();
    for (const auto& val : bodiesArr) {
        QJsonObject bObj = val.toObject();
        Node node;
        node.id = bObj["id"].toString();
        node.mass = bObj["mass"].toDouble(0.0);
        node.localTransform = parseTransform(bObj);
        
        QJsonArray geomsArr = bObj["geoms"].toArray();
        for (const auto& gVal : geomsArr) {
            QJsonObject gObj = gVal.toObject();
            Geom geom;
            geom.type = gObj["type"].toString();
            geom.pos = parseTransform(gObj).position;
            
            if (gObj.contains("mesh")) geom.mesh = modelId + "_" + gObj["mesh"].toString();
            if (gObj.contains("material")) geom.material = "mat_" + modelId + "_" + gObj["material"].toString();
            
            if (gObj.contains("size")) {
                QJsonArray sizeArr = gObj["size"].toArray();
                for (int i = 0; i < sizeArr.size(); ++i) geom.size.append(sizeArr[i].toDouble());
            }
            node.geoms.append(geom);
        }

        QJsonArray sitesArr = bObj["sites"].toArray();
        for (const auto& sVal : sitesArr) {
            QJsonObject sObj = sVal.toObject();
            Site site;
            site.id = sObj["id"].toString();
            site.localTransform = parseTransform(sObj);
            node.sites.append(site);
        }

        kinematics.addNode(node);
    }

    QJsonArray jointsArr = kinematicsObj["joints"].toArray();
    for (const auto& val : jointsArr) {
        QJsonObject jObj = val.toObject();
        Edge edge;
        edge.id = jObj["id"].toString();
        edge.bodyA = jObj["body_a"].toString();
        edge.bodyB = jObj["body_b"].toString();
        edge.type = jObj["type"].toString();
        edge.localTransform = parseTransform(jObj);
        edge.damping = jObj["damping"].toDouble(0.0);
        edge.armature = jObj["armature"].toDouble(0.0);
        edge.frictionloss = jObj["frictionloss"].toDouble(0.0);
        
        if (jObj.contains("range")) {
            QJsonArray rangeArr = jObj["range"].toArray();
            if (rangeArr.size() == 2) {
                edge.range.append(rangeArr[0].toDouble());
                edge.range.append(rangeArr[1].toDouble());
            }
        }
        kinematics.addEdge(edge);
    }
}





/*
 * Parses the structural attachment points.
 * Connectors represent non-physical snap-points in the kinematic graph.
 */
void ComponentBlueprint::parseConnectors(const QJsonArray& connectorsArr) {
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
        connectors[connDef.id] = connDef;
    }
}





/*
 * Parses the logical IO interfaces required to drive actuators or read sensors.
 * Extracts specific target joint IDs and control bounds.
 */
void ComponentBlueprint::parseIO(const QJsonObject& ioObj) {
    QJsonArray inputsArray = ioObj["inputs"].toArray();
    for (const auto& val : inputsArray) {
        QJsonObject inObj = val.toObject();
        IODef def;
        def.name = inObj["name"].toString();
        def.unit = inObj["unit"].toString();
        def.dataType = inObj["data_type"].toString();
        def.targetJoint = inObj["target_joint"].toString();
        def.actuatorType = inObj["actuator_type"].toString();
        def.kp = inObj["kp"].toDouble(0.0);
        def.kv = inObj["kv"].toDouble(0.0);
        
        if (inObj.contains("range")) {
            QJsonArray rangeArr = inObj["range"].toArray();
            if (rangeArr.size() == 2) {
                def.range = qMakePair(static_cast<float>(rangeArr[0].toDouble()), static_cast<float>(rangeArr[1].toDouble()));
            }
        }

        if (inObj.contains("forcerange")) {
            QJsonArray frArr = inObj["forcerange"].toArray();
            if (frArr.size() == 2) {
                def.forceRange.append(frArr[0].toDouble());
                def.forceRange.append(frArr[1].toDouble());
            }
        }

        inputDefs[def.name] = def;
    }

    QJsonArray outputsArray = ioObj["outputs"].toArray();
    for (const auto& val : outputsArray) {
        QJsonObject outObj = val.toObject();
        IODef def;
        def.name = outObj["name"].toString();
        def.unit = outObj["unit"].toString();
        def.dataType = outObj["data_type"].toString();
        def.targetJoint = outObj["target_joint"].toString();
        def.targetSite = outObj["target_site"].toString();
        def.sensorType = outObj["sensor_type"].toString();
        def.axis = outObj["axis"].toInt(0);
        
        outputDefs[def.name] = def;
    }
}





/*
 * Returns the generated XML string containing all asset definitions.
 * Called during the initial MuJoCo model compilation phase.
 */
QString ComponentBlueprint::getAssetXML() const {
    return assetXML;
}





/*
 * Entry point for generating the component's nested MuJoCo XML.
 * Triggers recursive graph traversal from the specified root body.
 */
QString ComponentBlueprint::generateTreeXML(const int uid, const QString& rootConnectorId, const Transform& globalTransform) const {
    QString xml = "";
    QString rootNodeId = ""; 
    Transform rootRelTransform = globalTransform;

    if (!rootConnectorId.isEmpty() && connectors.contains(rootConnectorId)) {
        ConnectorDef conn = connectors.value(rootConnectorId);
        rootNodeId = conn.body;
        
        Transform baseTransform = globalTransform * conn.transform.inverse();
        Transform nodeLocalTransform = kinematics.getNode(rootNodeId).localTransform;
        rootRelTransform = baseTransform * nodeLocalTransform;
    } else {
        // Fallback
        QList<QString> availableNodes = kinematics.getNodes().keys();
        if (!availableNodes.isEmpty()) {
            rootNodeId = availableNodes.first();
        } else {
            Log::error("Component blueprint has no bodies to render! UID: " + QString::number(uid));
            return xml;
        }
    }

    QSet<QString> visited;
    traverseGraph(xml, rootNodeId, "", rootRelTransform, visited, uid);
    
    return xml;
}





/*
 * Recursive Spanning Tree algorithm that walks the kinematic graph.
 * Dynamically nests bodies, assigns properties, and links joints.
 */
void ComponentBlueprint::traverseGraph(QString& outXML, const QString& currentNodeId, const QString& parentNodeId, const Transform& relTransform, QSet<QString>& visited, const int uid) const {
    visited.insert(currentNodeId);
    Node currentNode = kinematics.getNode(currentNodeId);
    
    QString prefix = "comp_" + QString::number(uid) + "_";
    QString indent = QString(visited.size() * 2, ' ');
    
    outXML += indent + "<body name=\"" + prefix + currentNodeId + "\" ";
    outXML += QString("pos=\"%1 %2 %3\" ").arg(relTransform.position.x).arg(relTransform.position.y).arg(relTransform.position.z);
    outXML += QString("quat=\"%1 %2 %3 %4\">\n").arg(relTransform.rotation.w).arg(relTransform.rotation.x).arg(relTransform.rotation.y).arg(relTransform.rotation.z);

    for (int i = 0; i < currentNode.geoms.size(); ++i) {
        const Geom& geom = currentNode.geoms[i];
        
        outXML += indent;
        outXML += QString("  <geom type=\"%1\" pos=\"%2 %3 %4\"").arg(geom.type).arg(geom.pos.x).arg(geom.pos.y).arg(geom.pos.z);
        
        if (i == 0 && currentNode.mass > 0.0) {
            outXML += QString(" mass=\"%1\"").arg(currentNode.mass);
        }
        
        if (geom.type == "mesh" && !geom.mesh.isEmpty() && geom.mesh != (modelId + "_")) {
            // outXML += QString(" mesh=\"%1\" material=\"%2\"").arg(geom.mesh).arg(geom.material);
            if(!geom.mesh.isEmpty() && geom.mesh != (modelId + "_")){
                outXML += QString(" mesh=\"%1\"").arg(geom.mesh);
            }
            if(!geom.material.isEmpty() && geom.material != ("mat_" + modelId + "_")) {
                outXML += QString(" material=\"%1\"").arg(geom.material);
            }
        } else if ((geom.type == "box" || geom.type == "cylinder" || geom.type == "capsule") && geom.size.size() > 0) {
            outXML += " size=\"";
            for (int j = 0; j < geom.size.size(); ++j) {
                outXML += QString::number(geom.size[j]) + (j < geom.size.size() - 1 ? " " : "");
            }
            outXML += "\"";
        }
        outXML += "/>\n";
    }

    for (const Site& site : currentNode.sites) {
        outXML += indent + QString("  <site name=\"%1%2\" pos=\"%3 %4 %5\"/>\n")
                        .arg(prefix).arg(site.id)
                        .arg(site.localTransform.position.x).arg(site.localTransform.position.y).arg(site.localTransform.position.z);
    }

    if (!parentNodeId.isEmpty()) {
        QList<Edge> edges = kinematics.getEdgesForNode(currentNodeId);
        for (const Edge& edge : edges) {
            if (edge.bodyA == parentNodeId || edge.bodyB == parentNodeId) {
                Transform jointRel = currentNode.localTransform.inverse() * edge.localTransform;
                Position zAxis(0, 0, 1);
                Position rotAxis = jointRel.rotation.rotate(zAxis);
                
                double rangeMin = edge.range.isEmpty() ? 0 : edge.range[0];
                double rangeMax = edge.range.isEmpty() ? 0 : edge.range[1];

                if (edge.bodyA == currentNodeId) {
                    rotAxis = -rotAxis;
                    double temp = rangeMin;
                    rangeMin = -rangeMax;
                    rangeMax = -temp;
                }

                outXML += indent + "  <joint name=\"" + prefix + edge.id + "\" type=\"" + edge.type + "\" ";
                outXML += QString("pos=\"%1 %2 %3\" ").arg(jointRel.position.x).arg(jointRel.position.y).arg(jointRel.position.z);
                outXML += QString("axis=\"%1 %2 %3\" ").arg(rotAxis.x).arg(rotAxis.y).arg(rotAxis.z);
                outXML += QString("range=\"%1 %2\" ").arg(rangeMin).arg(rangeMax);
                outXML += QString("damping=\"%1\"").arg(edge.damping);
                
                if (edge.armature > 0.0) outXML += QString(" armature=\"%1\"").arg(edge.armature);
                if (edge.frictionloss > 0.0) outXML += QString(" frictionloss=\"%1\"").arg(edge.frictionloss);
                
                outXML += "/>\n";
                break;
            }
        }
    }

    QList<Edge> connectedEdges = kinematics.getEdgesForNode(currentNodeId);
    for (const Edge& edge : connectedEdges) {
        QString neighborId = (edge.bodyA == currentNodeId) ? edge.bodyB : edge.bodyA;
        if (!visited.contains(neighborId)) {
            Node neighborNode = kinematics.getNode(neighborId);
            Transform childRelTransform = currentNode.localTransform.inverse() * neighborNode.localTransform;
            traverseGraph(outXML, neighborId, currentNodeId, childRelTransform, visited, uid);
        }
    }

    outXML += indent + "  <!" + "-- INJECT_" + prefix + currentNodeId + " --" + ">\n";
    outXML += indent + "</body>\n";
}





/*
 * Dynamically constructs the <actuator> tags based on the IODef specifications.
 * Identifies the specific joint the actuator controls and enforces force limits.
 */
QString ComponentBlueprint::generateActuatorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";
    
    for (const IODef& def : inputDefs) {
        if (!def.actuatorType.isEmpty() && !def.targetJoint.isEmpty()) {
            QString forceStr = "";
            if (def.forceRange.size() == 2) {
                forceStr = QString(" forcerange=\"%1 %2\"").arg(def.forceRange[0]).arg(def.forceRange[1]);
            }
            
            xml += QString("    <%1 name=\"%2%3\" joint=\"%2%4\" kp=\"%5\" kv=\"%6\"%7/>\n")
                    .arg(def.actuatorType)
                    .arg(prefix)
                    .arg(def.name)
                    .arg(def.targetJoint)
                    .arg(def.kp)
                    .arg(def.kv)
                    .arg(forceStr);
        }
    }
    return xml;
}





/*
 * Dynamically constructs the <sensor> tags based on the IODef specifications.
 * Links the telemetry output to the exact physical joint.
 */
// QString ComponentBlueprint::generateSensorXML(const int uid) const {
//     QString xml = "";
//     QString prefix = "comp_" + QString::number(uid) + "_";
    
//     for (const IODef& def : outputDefs) {
//         if (!def.sensorType.isEmpty() && !def.targetJoint.isEmpty()) {
//             xml += QString("    <%1 name=\"%2%3\" joint=\"%2%4\"/>\n")
//                     .arg(def.sensorType)
//                     .arg(prefix)
//                     .arg(def.name)
//                     .arg(def.targetJoint);
//         }
//     }
//     return xml;
// }
QString ComponentBlueprint::generateSensorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";
    
    for (const IODef& def : outputDefs) {
        if (def.sensorType.isEmpty()) continue;

        // If the sensor targets a JOINT (like a servo)
        if (!def.targetJoint.isEmpty()) {
            xml += QString("    <%1 name=\"%2%3\" joint=\"%2%4\"/>\n")
                    .arg(def.sensorType)
                    .arg(prefix)
                    .arg(def.name)
                    .arg(def.targetJoint);
        } 
        // If the sensor targets a SITE (like an IMU or Rangefinder)
        else if (!def.targetSite.isEmpty()) {
            xml += QString("    <%1 name=\"%2%3\" site=\"%2%4\"/>\n")
                    .arg(def.sensorType)
                    .arg(prefix)
                    .arg(def.name)
                    .arg(def.targetSite);
        }
    }
    return xml;
}





/*
 * Calculates the coordinate frame of a connector relative to the 
 * specific physical body it is attached to.
 */
Transform ComponentBlueprint::getConnectorRelativeTransform(const QString& connId) const {
    if (!connectors.contains(connId)) return Transform();
    
    ConnectorDef conn = connectors.value(connId);
    Node bodyNode = kinematics.getNode(conn.body);
    
    return bodyNode.localTransform.inverse() * conn.transform;
}