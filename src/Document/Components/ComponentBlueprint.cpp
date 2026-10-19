#include "ComponentBlueprint.h"
#include "Units.h"
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
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
        outXML += QString("  <geom type=\"%1\" pos=\"%2 %3 %4\" quat=\"%5 %6 %7 %8\"")
                            .arg(geom.type)
                            .arg(geom.pos.x).arg(geom.pos.y).arg(geom.pos.z)
                            .arg(geom.rot.w).arg(geom.rot.x).arg(geom.rot.y).arg(geom.rot.z);

        // overrideGeom: the <inertial> above is authoritative, geoms carry no mass.
        // Otherwise every geom carries its own mass.
        if (!currentNode.overrideGeom) {
            outXML += QString(" mass=\"%1\"").arg(geom.mass);
        }

        if (geom.type == "mesh" && !geom.mesh.isEmpty()) {
            outXML += QString(" mesh=\"%1_%2\"")
                                .arg(modelId)
                                .arg(geom.mesh);
        } else if ((geom.type == "box" || geom.type == "cylinder" || geom.type == "capsule") && geom.size.size() > 0) {
            outXML += " size=\"";
            for (int j = 0; j < geom.size.size(); ++j) {
                outXML += QString::number(geom.size[j]) + (j < geom.size.size() - 1 ? " " : "");
            }
            outXML += "\"";
        }

        // Appearance, any geom type: a texture material wins over a flat color.
        if(!geom.material.isEmpty() && geom.material != ("mat_" + modelId + "_")) {
            outXML += QString(" material=\"mat_%1_%2\"")
                                .arg(modelId)
                                .arg(geom.material);
        }else if(!geom.color.isEmpty()){
            outXML += QString(" rgba=\"%1 %2 %3 %4\"")
                                .arg(geom.color[0])
                                .arg(geom.color.size() > 1 ? geom.color[1] : geom.color[0])
                                .arg(geom.color.size() > 2 ? geom.color[2] : geom.color[0])
                                .arg(geom.color.size() > 3 ? geom.color[3] : 1.0);
        }
        outXML += "/>\n";
    }

    // Display screens: a visual-only quad on the display's surface, in the
    // body that owns the target geom (see generateDisplayAssetXML).
    for (const DisplayDef& disp : displays) {
        if (disp.target.kind != "geom") continue;
        bool inThisBody = false;
        for (const Geom& g : currentNode.geoms) if (g.id == disp.target.id) { inThisBody = true; break; }
        if (!inThisBody) continue;
        outXML += indent + QString("  <geom name=\"%1%2_screen\" type=\"mesh\" mesh=\"%1%2_quad\" material=\"%1%2_mat\" "
                                   "pos=\"%3 %4 %5\" quat=\"%6 %7 %8 %9\" contype=\"0\" conaffinity=\"0\" density=\"0\"/>\n")
                        .arg(prefix, disp.id)
                        .arg(disp.surface.position.x).arg(disp.surface.position.y).arg(disp.surface.position.z)
                        .arg(disp.surface.rotation.w).arg(disp.surface.rotation.x).arg(disp.surface.rotation.y).arg(disp.surface.rotation.z);
    }

    for (const Site& site : currentNode.sites) {
        outXML += indent + QString("<site name=\"%1%2\" pos=\"%3 %4 %5\" quat=\"%6 %7 %8 %9\"/>\n")
                        .arg(prefix)
                        .arg(site.id)
                        .arg(site.localTransform.position.x)
                        .arg(site.localTransform.position.y)
                        .arg(site.localTransform.position.z)
                        .arg(site.localTransform.rotation.w)
                        .arg(site.localTransform.rotation.x)
                        .arg(site.localTransform.rotation.y)
                        .arg(site.localTransform.rotation.z);

        // Cameras sit exactly on their site: a MuJoCo camera looks along the
        // site's -Z axis with +Y up.
        for (const CameraDef& cam : cameras) {
            if (cam.target.kind != "site" || cam.target.id != site.id) continue;
            outXML += indent + QString("<camera name=\"%1%2\" pos=\"%3 %4 %5\" quat=\"%6 %7 %8 %9\"")
                            .arg(prefix)
                            .arg(cam.id)
                            .arg(site.localTransform.position.x)
                            .arg(site.localTransform.position.y)
                            .arg(site.localTransform.position.z)
                            .arg(site.localTransform.rotation.w)
                            .arg(site.localTransform.rotation.x)
                            .arg(site.localTransform.rotation.y)
                            .arg(site.localTransform.rotation.z);
            if (cam.fovy > 0.0) outXML += QString(" fovy=\"%1\"").arg(cam.fovy);
            if (cam.resolution.first > 0 && cam.resolution.second > 0) {
                outXML += QString(" resolution=\"%1 %2\"").arg(cam.resolution.first).arg(cam.resolution.second);
            }
            outXML += "/>\n";
        }
    }

    // Every joint between the parent and this body is emitted (e.g. a servo's
    // servo_joint + backlash_joint stacked on the horn). MuJoCo chains a body's
    // joints in order, so a reversed walk (body_a below body_b) emits them in
    // reverse to keep the composition parent -> child.
    if (!parentNodeId.isEmpty()) {
        QList<Edge> edges;
        for (const Edge& e : kinematics.getEdgesForNode(currentNodeId))
            if (e.bodyA == parentNodeId || e.bodyB == parentNodeId) edges << e;
        if (!edges.isEmpty() && edges.first().bodyA == currentNodeId) std::reverse(edges.begin(), edges.end());
        for (const Edge& edge : edges) {
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
 * Assets of each display, named comp_<uid>_<displayId>_{tex,mat,quad}:
 *   texture  resolution-sized, dark (a powered-off panel); DisplayTextures
 *            uploads the live frames straight into its GL texture.
 *   material emissive, so the screen glows regardless of scene lighting.
 *   quad     two triangles spanning the surface (half-extents), facing +Z,
 *            with explicit texcoords: pixel row 0 at +Y, column 0 at -X.
 *            Shell inertia: a flat mesh has no volume.
 */
QString ComponentBlueprint::generateDisplayAssetXML(const int uid) const {
    QString xml;
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const DisplayDef& disp : displays) {
        const QString name = prefix + disp.id;
        const double hx = disp.surfaceSize.first;
        const double hy = disp.surfaceSize.second;
        xml += QString("<texture name=\"%1_tex\" type=\"2d\" builtin=\"flat\" rgb1=\"0.02 0.02 0.03\" width=\"%2\" height=\"%3\"/>\n")
                    .arg(name).arg(disp.resolution.first).arg(disp.resolution.second);
        xml += QString("<material name=\"%1_mat\" texture=\"%1_tex\" emission=\"1\" specular=\"0\" shininess=\"0\"/>\n").arg(name);
        xml += QString("<mesh name=\"%1_quad\" vertex=\"%2 %3 0  %4 %3 0  %4 %5 0  %2 %5 0\" "
                       "face=\"0 1 2  0 2 3\" texcoord=\"0 1  1 1  1 0  0 0\" inertia=\"shell\"/>\n")
                    .arg(name).arg(-hx).arg(-hy).arg(hx).arg(hy);
    }
    return xml;
}




QString ComponentBlueprint::actuatorForInput(const InterfaceDef& def) const {
    if (def.target.kind == "actuator") return def.target.id;
    if (def.target.kind != "joint") return QString();
    for (const ActuatorDef& a : actuators) {
        if (a.target.kind == "joint" && a.target.id == def.target.id && a.type != "motor") return a.id;
    }
    return QString();
}


/*
 * Unit of the interface input that drives an actuator (actuatorForInput, the
 * same resolution SimulationManager::cacheMujocoIds uses). Empty if no signal
 * drives it (values are then taken as MuJoCo units).
 */
QString ComponentBlueprint::drivingSignalUnit(const ActuatorDef& act) const {
    for (const InterfaceDef& def : interfaceInputs) {
        if (actuatorForInput(def) == act.id) return def.unit;
    }
    return QString();
}


/*
 * Constructs the <actuator> tags from devices.actuators. Each actuator is
 * named comp_<uid>_<deviceId> and drives its target joint or tendon.
 */
QString ComponentBlueprint::generateActuatorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const ActuatorDef& act : actuators) {
        const DeviceTypeInfo* row = DeviceTypes::find(DeviceFamily::Actuator, act.type);
        if (!row) continue;

        QString actuatorOut = QString("<%1 name=\"%2%3\" %4=\"%2%5\"")
                                .arg(row->mjcfElement)
                                .arg(prefix)
                                .arg(act.id)
                                .arg(act.target.kind)
                                .arg(act.target.id);

        if (act.ctrlrange.size() == 2) {
            // Authored in the driving signal's unit (degrees for a servo);
            // MuJoCo wants radians.
            const double f = Units::toMujoco(drivingSignalUnit(act));
            actuatorOut += QString(" ctrlrange=\"%1 %2\" ctrllimited=\"true\"")
                            .arg(act.ctrlrange[0] * f)
                            .arg(act.ctrlrange[1] * f);
        }

        if (act.forceRange.size() == 2) {
            actuatorOut += QString(" forcerange=\"%1 %2\"")
                            .arg(act.forceRange[0])
                            .arg(act.forceRange[1]);
        }

        if (act.type == "position") {
            actuatorOut += QString(" kp=\"%1\" kv=\"%2\"").arg(act.kp).arg(act.kv);
        }
        else if (act.type == "velocity") {
            actuatorOut += QString(" kv=\"%1\"").arg(act.kv);
        }
        else if (act.type == "motor") {
            actuatorOut += QString(" gear=\"1\"");
        }

        actuatorOut += "/>\n";
        xml += actuatorOut;
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
    QSet<QString> seen;
    for (const Node& node : kinematics.getNodes()) {
        if (!node.geoms.isEmpty()) continue;                 // only geom-less helper bodies
        QStringList nbrs;
        for (const Edge& e : kinematics.getEdgesForNode(node.id))
            nbrs << (e.bodyA == node.id ? e.bodyB : e.bodyA);
        for (int i = 0; i < nbrs.size(); ++i)
            for (int j = i + 1; j < nbrs.size(); ++j) {
                QString key = nbrs[i] < nbrs[j] ? nbrs[i] + "|" + nbrs[j] : nbrs[j] + "|" + nbrs[i];
                if (seen.contains(key)) continue;            // never emit a pair twice
                seen.insert(key);
                exclude_xml += QString("<exclude body1=\"%1%2\" body2=\"%1%3\"/>\n").arg(prefix, nbrs[i], nbrs[j]);
            }
    }
    return exclude_xml;
}



/*
 * Constructs the <sensor> tags from devices.sensors, named
 * comp_<uid>_<deviceId>. The target attribute (joint=/site=/tendon=) follows
 * the target kind.
 */
QString ComponentBlueprint::generateBasicSensorXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const SensorDef& sensor : sensors) {
        const DeviceTypeInfo* row = DeviceTypes::find(DeviceFamily::Sensor, sensor.type);
        if (!row) continue;

        xml += QString("<%1 name=\"%2%3\" %4=\"%2%5\"/>\n")
                .arg(row->mjcfElement)
                .arg(prefix)
                .arg(sensor.id)
                .arg(sensor.target.kind)
                .arg(sensor.target.id);
    }

    return xml;
}




/*
 * Fixed tendons (measurement only). A fixed tendon's length is
 * sum(coef * joint qpos). traverseGraph negates a joint's axis when it walks
 * body_b -> body_a, and that negation is exactly what makes qpos equal the
 * joint's logical value (rotation of body_b relative to body_a about the
 * joint axis) whatever the walk direction. Coefficients written in the logical
 * direction are therefore emitted unchanged; no per-instance orientation
 * handling is needed.
 */
QString ComponentBlueprint::generateTendonXML(const int uid) const {
    QString xml = "";
    QString prefix = "comp_" + QString::number(uid) + "_";

    for (const TendonDef& tendon : tendons) {
        xml += QString("<fixed name=\"%1%2\">\n").arg(prefix, tendon.id);
        for (const TendonTerm& term : tendon.terms) {
            xml += QString("    <joint joint=\"%1%2\" coef=\"%3\"/>\n").arg(prefix, term.joint).arg(term.coef);
        }
        xml += "</fixed>\n";
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
