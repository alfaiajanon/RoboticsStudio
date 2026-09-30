#include "ComponentBlueprint.h"
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include "Utils/Log.h"





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




static QJsonObject loadJsonFile(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){
        Log::error("ComponentBlueprint: Unable to open rsdef file: " + path);
        return QJsonObject();
    }
    QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    return obj;
}





/*
 * Parses the .rsdef JSON file to construct the component's definitions.
 * Delegates specific block parsing to private helper methods.
 */
ComponentBlueprint::ComponentBlueprint(const QString& rsdefFile)
                    : ComponentData(ComponentData::fromJson(loadJsonFile(rsdefFile), QFileInfo(rsdefFile).absolutePath())){

    generateAssetXML();
    parseKinematics();
    Log::info("ComponentBlueprint: Loaded model ID: " + modelId);
}




ComponentBlueprint::ComponentBlueprint(const ComponentData& data)
                    : ComponentData(data){

    generateAssetXML();
    parseKinematics();
}





void ComponentBlueprint::generateAssetXML() {
    assetXML = "";

    for (const QString& key : meshResources.keys()) {
        QString relPath = meshResources[key];
        QString absPath = QDir(basePath).filePath(relPath);
        assetXML += QString("<mesh name=\"%1_%2\" file=\"%3\" scale=\"1 1 1\"/>\n")
                        .arg(modelId).arg(key).arg(absPath);
    }

    for (const QString& key : materialResources.keys()) {
        QString relPath = materialResources[key];
        QString absPath = QDir(basePath).filePath(relPath);

        assetXML += QString("<texture name=\"tex_%1_%2\" file=\"%3\" type=\"2d\"/>\n")
                        .arg(modelId).arg(key).arg(absPath);
        assetXML += QString("<material name=\"mat_%1_%2\" texture=\"tex_%1_%2\" specular=\"0.3\"/>\n")
                        .arg(modelId).arg(key);
    }
}





void ComponentBlueprint::parseKinematics() {
    kinematics.clear();

    kinematics.setDefaultNode(defaultBodyId);

    for(auto& key : bodies.keys()){
        kinematics.addNode(bodies[key]);
    }
    for (auto& val : joints) {
        kinematics.addEdge(val);
    }
}






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
        // Fallback (needed for root node)
        Node defaultNode = kinematics.getDefaultNode();
        rootNodeId = defaultNode.id;
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
    if (currentNode.overrideGeom) {
        outXML += indent + QString("  <inertial pos=\"0 0 0\" mass=\"%1\" diaginertia=\"%2 %2 %2\"/>\n")
                                    .arg(currentNode.mass)
                                    .arg(currentNode.inertia);
    }

    for (int i = 0; i < currentNode.geoms.size(); ++i) {
        const Geom& geom = currentNode.geoms[i];

        outXML += indent;
        outXML += QString("  <geom type=\"%1\" pos=\"%2 %3 %4\" quat=\"%5 %6 %7 %8\" mass=\"%9\"")
                            .arg(geom.type)
                            .arg(geom.pos.x).arg(geom.pos.y).arg(geom.pos.z)
                            .arg(geom.rot.w).arg(geom.rot.x).arg(geom.rot.y).arg(geom.rot.z)
                            .arg(geom.mass);

        if (i == 0 && currentNode.mass > 0.0) {
            outXML += QString(" mass=\"%1\"").arg(currentNode.mass);
        }

        if (geom.type == "mesh" && !geom.mesh.isEmpty()) {
            if(!geom.mesh.isEmpty()){
                outXML += QString(" mesh=\"%1_%2\"")
                                    .arg(modelId)
                                    .arg(geom.mesh);
            }
            if(!geom.material.isEmpty() && geom.material != ("mat_" + modelId + "_")) {
                outXML += QString(" material=\"mat_%1_%2\"")
                                    .arg(modelId)
                                    .arg(geom.material);
            }else if(!geom.color.isEmpty()){
                outXML += QString(" rgba=\"%1 %2 %3 %4\"")
                                    .arg(geom.color[0])
                                    .arg(geom.color[1])
                                    .arg(geom.color[2])
                                    .arg(geom.color.size() > 3 ? geom.color[3] : 1.0);
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
        outXML += indent + QString("<site name=\"%1%2\" pos=\"%3 %4 %5\"/>\n")
                        .arg(prefix)
                        .arg(site.id)
                        .arg(site.localTransform.position.x)
                        .arg(site.localTransform.position.y)
                        .arg(site.localTransform.position.z);
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

                outXML += indent + "<joint name=\"" + prefix + edge.id + "\" type=\"" + edge.type + "\" ";
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

    outXML += indent + "<!" + "-- INJECT_" + prefix + currentNodeId + " --" + ">\n";
    outXML += indent + "</body>\n";
}





/*
 * Dynamically constructs the <actuator> tags based on the IODef specifications.
 * Identifies the specific joint the actuator controls and enforces force limits.
 */
QString ComponentBlueprint::generateActuatorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const Edge& edge : kinematics.getEdges()) {
        if (!edge.actuator.type.isEmpty()) {

            QString actuatorOut = QString("<%1 name=\"%2%3_actuator\" joint=\"%2%3\"")
                                    .arg(edge.actuator.type)
                                    .arg(prefix)
                                    .arg(edge.id);

            if (edge.actuator.ctrlrange.size() == 2) {
                actuatorOut += QString(" ctrlrange=\"%1 %2\" ctrllimited=\"true\"")
                                .arg(edge.actuator.ctrlrange[0])
                                .arg(edge.actuator.ctrlrange[1]);
            }

            if (edge.actuator.forceRange.size() == 2) {
                actuatorOut += QString(" forcerange=\"%1 %2\"")
                                .arg(edge.actuator.forceRange[0])
                                .arg(edge.actuator.forceRange[1]);
            }

            if (edge.actuator.type == "position") {
                actuatorOut += QString(" kp=\"%1\" kv=\"%2\"").arg(edge.actuator.kp).arg(edge.actuator.kv);
            }
            else if (edge.actuator.type == "velocity") {
                actuatorOut += QString(" kv=\"%1\"").arg(edge.actuator.kv);
            }
            else if (edge.actuator.type == "motor") {
                actuatorOut += QString(" gear=\"1\"");
            }

            actuatorOut += "/>\n";
            xml += actuatorOut;
        }
    }
    return xml;
}




QString ComponentBlueprint::generateContactsXML(const int uid) const {
    QString exclude_xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    QList<Edge> edges = kinematics.getEdges();
    for (const Edge& edge : edges) {
        if(edge.collision == false){
            exclude_xml += QString("<exclude body1=\"%1%2\" body2=\"%1%3\"/>\n")
                            .arg(prefix)
                            .arg(edge.bodyA)
                            .arg(edge.bodyB);
        }
    }
    return exclude_xml;
}



QString ComponentBlueprint::generateSensorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const Edge& edge : kinematics.getEdges()) {
        if (!edge.sensor.type.isEmpty()) {
            xml += QString("<%1 name=\"%2%3_sensor\" joint=\"%2%3\"/>\n")
                    .arg(edge.sensor.type)
                    .arg(prefix)
                    .arg(edge.id);
        }
    }

    for (const Node& node : kinematics.getNodes()) {
        for (const Site& site : node.sites) {
            if (!site.sensor.type.isEmpty()) {
                xml += QString("<%1 name=\"%2%3_sensor\" site=\"%2%3\"/>\n")
                        .arg(site.sensor.type)
                        .arg(prefix)
                        .arg(site.id);
            }
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
