#include "ComponentData.h"
#include "Utils/Graph.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <cmath>




/*
 * First-error-wins load failure. Parsers take one of these, check has() after
 * each step and return early; fromJson turns it into isValid/errorString.
 */
struct LoadError {
    QString msg;
    bool has() const { return !msg.isEmpty(); }
    void set(const QString& m) { if (msg.isEmpty()) msg = m; }
};

static QString typeName(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null: return "null";
        case QJsonValue::Bool: return "a bool";
        case QJsonValue::Double: return "a number";
        case QJsonValue::String: return "a string";
        case QJsonValue::Array: return "an array";
        case QJsonValue::Object: return "an object";
        default: return "undefined";
    }
}

static QString readString(const QJsonObject& o, const QString& key, const QString& path, LoadError& err, bool required, const QString& def = QString()) {
    if (err.has()) return def;
    if (!o.contains(key)) {
        if (required) err.set(QString("%1: missing '%2'").arg(path, key));
        return def;
    }
    if (!o[key].isString()) {
        err.set(QString("%1: '%2' must be a string, got %3").arg(path, key, typeName(o[key])));
        return def;
    }
    QString v = o[key].toString();
    if (required && v.isEmpty()) err.set(QString("%1: '%2' must not be empty").arg(path, key));
    return v;
}

static double readNumber(const QJsonObject& o, const QString& key, const QString& path, LoadError& err, bool required, double def = 0.0) {
    if (err.has()) return def;
    if (!o.contains(key)) {
        if (required) err.set(QString("%1: missing '%2'").arg(path, key));
        return def;
    }
    if (!o[key].isDouble()) {
        err.set(QString("%1: '%2' must be a number, got %3").arg(path, key, typeName(o[key])));
        return def;
    }
    return o[key].toDouble();
}

static bool readBool(const QJsonObject& o, const QString& key, const QString& path, LoadError& err, bool required, bool def) {
    if (err.has()) return def;
    if (!o.contains(key)) {
        if (required) err.set(QString("%1: missing '%2'").arg(path, key));
        return def;
    }
    if (!o[key].isBool()) {
        err.set(QString("%1: '%2' must be a bool, got %3").arg(path, key, typeName(o[key])));
        return def;
    }
    return o[key].toBool();
}

// Optional array of exactly `size` numbers. Empty list if absent.
static QList<double> readNumberList(const QJsonObject& o, const QString& key, int size, const QString& path, LoadError& err) {
    QList<double> out;
    if (err.has() || !o.contains(key)) return out;
    if (!o[key].isArray() || o[key].toArray().size() != size) {
        err.set(QString("%1: '%2' must be an array of %3 numbers").arg(path, key).arg(size));
        return out;
    }
    for (const auto& v : o[key].toArray()) {
        if (!v.isDouble()) {
            err.set(QString("%1: '%2' must contain only numbers").arg(path, key));
            return QList<double>();
        }
        out.append(v.toDouble());
    }
    return out;
}

static QPair<int, int> readResolution(const QJsonObject& o, const QString& path, LoadError& err) {
    QList<double> r = readNumberList(o, "resolution", 2, path, err);
    if (err.has()) return qMakePair(0, 0);
    if (r.isEmpty()) {
        err.set(QString("%1: missing 'resolution'").arg(path));
        return qMakePair(0, 0);
    }
    if (r[0] < 1 || r[1] < 1 || r[0] != std::floor(r[0]) || r[1] != std::floor(r[1])) {
        err.set(QString("%1: 'resolution' must be two positive integers").arg(path));
        return qMakePair(0, 0);
    }
    return qMakePair(static_cast<int>(r[0]), static_cast<int>(r[1]));
}

static void rejectKeys(const QJsonObject& o, const QStringList& keys, const QString& path, const QString& hint, LoadError& err) {
    if (err.has()) return;
    for (const QString& k : keys) {
        if (o.contains(k)) {
            err.set(QString("%1: '%2' is not allowed in schema 2 (%3)").arg(path, k, hint));
            return;
        }
    }
}

static QJsonArray readArray(const QJsonObject& o, const QString& key, const QString& path, LoadError& err) {
    if (err.has() || !o.contains(key)) return QJsonArray();
    if (!o[key].isArray()) {
        err.set(QString("%1: '%2' must be an array, got %3").arg(path, key, typeName(o[key])));
        return QJsonArray();
    }
    return o[key].toArray();
}

static QJsonObject readObject(const QJsonValue& v, const QString& path, LoadError& err) {
    if (!v.isObject()) {
        err.set(QString("%1: must be an object, got %2").arg(path, typeName(v)));
        return QJsonObject();
    }
    return v.toObject();
}

static void claimId(QSet<QString>& seen, const QString& id, const QString& what, LoadError& err) {
    if (err.has()) return;
    if (seen.contains(id)) err.set(QString("duplicate %1 '%2'").arg(what, id));
    seen.insert(id);
}




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
static QMap<QString, Node> parseBodies(const QJsonArray& bodiesArr, QSet<QString>& geomIds, QSet<QString>& siteIds, LoadError& err) {
    QMap<QString, Node> parsedBodies;
    QSet<QString> bodyIds;
    for (int bi = 0; bi < bodiesArr.size() && !err.has(); ++bi) {
        QString bPath = QString("kinematics.bodies[%1]").arg(bi);
        QJsonObject bObj = readObject(bodiesArr[bi], bPath, err);
        if (err.has()) break;

        Node node;
        node.id = readString(bObj, "id", bPath, err, true);
        claimId(bodyIds, node.id, "body id", err);
        if (err.has()) break;
        bPath = QString("body '%1'").arg(node.id);

        node.overrideGeom = readBool(bObj, "overrideGeom", bPath, err, false, false);
        if (node.overrideGeom) {
            node.mass = readNumber(bObj, "mass", bPath, err, true);
            node.inertia = readNumber(bObj, "inertia", bPath, err, true);
        } else {
            rejectKeys(bObj, {"mass", "inertia"}, bPath, "only allowed when overrideGeom is true; put the mass on the geoms", err);
        }
        node.localTransform = parseTransform(bObj);

        QJsonArray geomsArr = readArray(bObj, "geoms", bPath, err);
        for (int gi = 0; gi < geomsArr.size() && !err.has(); ++gi) {
            QString gPath = QString("%1 geoms[%2]").arg(bPath).arg(gi);
            QJsonObject gObj = readObject(geomsArr[gi], gPath, err);
            if (err.has()) break;

            Geom geom;
            geom.id = readString(gObj, "id", gPath, err, true);
            claimId(geomIds, geom.id, "geom id", err);
            if (err.has()) break;
            gPath = QString("geom '%1'").arg(geom.id);

            if (node.overrideGeom) {
                rejectKeys(gObj, {"mass"}, gPath, "the body has overrideGeom true, so its mass is authoritative", err);
            } else {
                geom.mass = readNumber(gObj, "mass", gPath, err, false, 0.0);
            }

            geom.type = readString(gObj, "type", gPath, err, true);
            if (gObj.contains("mesh")) geom.mesh = readString(gObj, "mesh", gPath, err, false);

            if (gObj.contains("material")) geom.material = readString(gObj, "material", gPath, err, false);
            if (gObj.contains("color") && geom.material.isEmpty()) {
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

        QJsonArray sitesArr = readArray(bObj, "sites", bPath, err);
        for (int si = 0; si < sitesArr.size() && !err.has(); ++si) {
            QString sPath = QString("%1 sites[%2]").arg(bPath).arg(si);
            QJsonObject sObj = readObject(sitesArr[si], sPath, err);
            if (err.has()) break;

            Site site;
            site.id = readString(sObj, "id", sPath, err, true);
            claimId(siteIds, site.id, "site id", err);
            rejectKeys(sObj, {"sensor"}, QString("site '%1'").arg(site.id), "sensors live under devices.sensors with target {kind: site}", err);
            site.localTransform = parseTransform(sObj);
            node.sites.append(site);
        }

        parsedBodies[node.id] = node;
    }
    return parsedBodies;
}

static QJsonObject geomToJson(const Geom& g, bool includeMass) {
    QJsonObject obj;
    obj["id"] = g.id;
    obj["type"] = g.type;
    if (!g.mesh.isEmpty()) obj["mesh"] = g.mesh;         // raw resource key -- see parseBodies note
    if (!g.material.isEmpty()) obj["material"] = g.material; // raw resource key
    if (!g.size.isEmpty()) obj["size"] = doubleListToJson(g.size);
    if (includeMass) obj["mass"] = g.mass;
    obj["pos"] = QJsonArray{g.pos.x, g.pos.y, g.pos.z};
    obj["quat"] = QJsonArray{g.rot.w, g.rot.x, g.rot.y, g.rot.z};
    if (g.material.isEmpty() && !g.color.isEmpty()) obj["color"] = doubleListToJson(g.color);
    return obj;
}

static QJsonObject siteToJson(const Site& s) {
    QJsonObject obj;
    obj["id"] = s.id;
    obj["transform"] = transformToJson(s.localTransform);
    return obj;
}

static QJsonObject nodeToJson(const Node& n) {
    QJsonObject obj;
    obj["id"] = n.id;
    obj["overrideGeom"] = n.overrideGeom;
    if (n.overrideGeom) {
        obj["mass"] = n.mass;
        obj["inertia"] = n.inertia;
    }
    obj["transform"] = transformToJson(n.localTransform);

    QJsonArray geomsArr;
    for (const Geom& g : n.geoms) geomsArr.append(geomToJson(g, !n.overrideGeom));
    obj["geoms"] = geomsArr;

    if (!n.sites.isEmpty()) {
        QJsonArray sitesArr;
        for (const Site& s : n.sites) sitesArr.append(siteToJson(s));
        obj["sites"] = sitesArr;
    }
    return obj;
}




static QList<Edge> parseJoints(const QJsonArray& jointsArr, QSet<QString>& jointIds, LoadError& err) {
    QList<Edge> parsedJoints;
    for (int ji = 0; ji < jointsArr.size() && !err.has(); ++ji) {
        QString path = QString("kinematics.joints[%1]").arg(ji);
        QJsonObject jObj = readObject(jointsArr[ji], path, err);
        if (err.has()) break;

        Edge edge;
        edge.id = readString(jObj, "id", path, err, true);
        claimId(jointIds, edge.id, "joint id", err);
        if (err.has()) break;
        path = QString("joint '%1'").arg(edge.id);

        edge.type = readString(jObj, "type", path, err, true);
        edge.bodyA = readString(jObj, "body_a", path, err, true);
        edge.bodyB = readString(jObj, "body_b", path, err, true);
        edge.damping = readNumber(jObj, "damping", path, err, false, 0.0);
        edge.armature = readNumber(jObj, "armature", path, err, false, 0.0);
        edge.collision = readBool(jObj, "collision", path, err, false, true);
        edge.frictionloss = readNumber(jObj, "frictionloss", path, err, false, 0.0);
        edge.localTransform = parseTransform(jObj);
        edge.range = readNumberList(jObj, "range", 2, path, err);
        rejectKeys(jObj, {"actuator", "sensor"}, path, "actuators and sensors live under devices with target {kind: joint}", err);

        parsedJoints.append(edge);
    }
    return parsedJoints;
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
    return obj;
}




static QList<TendonDef> parseTendons(const QJsonArray& arr, LoadError& err) {
    QList<TendonDef> out;
    QSet<QString> ids;
    for (int i = 0; i < arr.size() && !err.has(); ++i) {
        QString path = QString("kinematics.tendons[%1]").arg(i);
        QJsonObject o = readObject(arr[i], path, err);
        if (err.has()) break;

        TendonDef t;
        t.id = readString(o, "id", path, err, true);
        claimId(ids, t.id, "tendon id", err);
        if (err.has()) break;
        path = QString("tendon '%1'").arg(t.id);

        t.type = readString(o, "type", path, err, true);
        if (!err.has() && t.type != "fixed") err.set(QString("%1: unsupported type '%2' (only 'fixed')").arg(path, t.type));

        QJsonArray terms = readArray(o, "terms", path, err);
        if (!err.has() && terms.isEmpty()) err.set(QString("%1: 'terms' must not be empty").arg(path));
        for (int k = 0; k < terms.size() && !err.has(); ++k) {
            QString tPath = QString("%1 terms[%2]").arg(path).arg(k);
            QJsonObject to = readObject(terms[k], tPath, err);
            if (err.has()) break;
            TendonTerm term;
            term.joint = readString(to, "joint", tPath, err, true);
            term.coef = readNumber(to, "coef", tPath, err, true);
            t.terms.append(term);
        }
        out.append(t);
    }
    return out;
}

static QJsonObject tendonToJson(const TendonDef& t) {
    QJsonObject obj;
    obj["id"] = t.id;
    obj["type"] = t.type;
    QJsonArray terms;
    for (const TendonTerm& term : t.terms) {
        QJsonObject to;
        to["joint"] = term.joint;
        to["coef"] = term.coef;
        terms.append(to);
    }
    obj["terms"] = terms;
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




static QJsonObject targetToJson(const TargetRef& t) {
    QJsonObject obj;
    obj["kind"] = t.kind;
    if (!t.id.isEmpty()) obj["id"] = t.id;
    return obj;
}

// "target": {kind, id}. The kind must be one of allowedKinds; id is required
// unless the kind is "emulator", where it must be absent.
static TargetRef parseTarget(const QJsonObject& owner, const QString& path, const QStringList& allowedKinds, LoadError& err) {
    TargetRef t;
    if (err.has()) return t;
    if (!owner.contains("target")) {
        err.set(QString("%1: missing 'target'").arg(path));
        return t;
    }
    QJsonObject o = readObject(owner["target"], path + " target", err);
    t.kind = readString(o, "kind", path + " target", err, true);
    if (err.has()) return t;
    if (!allowedKinds.contains(t.kind)) {
        err.set(QString("%1: target kind '%2' not allowed here (allowed: %3)").arg(path, t.kind, allowedKinds.join(", ")));
        return t;
    }
    if (t.kind == "emulator") {
        if (o.contains("id")) err.set(QString("%1: target kind 'emulator' takes no 'id'").arg(path));
    } else {
        t.id = readString(o, "id", path + " target", err, true);
    }
    return t;
}

static bool targetResolves(const TargetRef& t, const QMap<QString, QSet<QString>>& ids) {
    return ids.value(t.kind).contains(t.id);
}

static void requireResolved(const TargetRef& t, const QMap<QString, QSet<QString>>& ids, const QString& path, LoadError& err) {
    if (err.has()) return;
    if (!targetResolves(t, ids))
        err.set(QString("%1: target %2 '%3' does not exist").arg(path, t.kind, t.id));
}

// Common part of every device entry: id (unique across all families), type
// (must be in the DeviceTypes table) and target (kind allowed by the type row).
static const DeviceTypeInfo* parseDeviceHeader(const QJsonObject& o, DeviceFamily family, const QString& path, QSet<QString>& deviceIds,
                                               QString& id, QString& type, TargetRef& target, LoadError& err) {
    id = readString(o, "id", path, err, true);
    if (err.has()) return nullptr;
    claimId(deviceIds, id, "device id", err);
    QString p = QString("%1 '%2'").arg(path, id);

    const DeviceTypeInfo* row = nullptr;
    if (family == DeviceFamily::Display) {
        if (o.contains("type")) err.set(QString("%1: displays take no 'type'").arg(p));
        row = DeviceTypes::find(family, "");
    } else {
        type = readString(o, "type", p, err, true);
        if (err.has()) return nullptr;
        row = DeviceTypes::find(family, type);
        if (!row) {
            err.set(QString("%1: unknown %2 type '%3' (supported: %4)").arg(p, DeviceTypes::familyKey(family), type, DeviceTypes::typeNames(family).join(", ")));
        }
    }
    if (err.has() || !row) return nullptr;

    target = parseTarget(o, p, row->allowedTargets, err);
    return row;
}

static void parseDevices(const QJsonObject& devicesObj, ComponentData& data, LoadError& err) {
    static const QList<DeviceFamily> families = {DeviceFamily::Actuator, DeviceFamily::Sensor, DeviceFamily::Camera, DeviceFamily::Display};
    QStringList known;
    for (DeviceFamily f : families) known << DeviceTypes::familyKey(f);
    for (const QString& key : devicesObj.keys()) {
        if (!known.contains(key)) {
            err.set(QString("devices: unknown family '%1' (expected: %2)").arg(key, known.join(", ")));
            return;
        }
    }

    QSet<QString> deviceIds;
    for (DeviceFamily family : families) {
        QString famKey = DeviceTypes::familyKey(family);
        QJsonArray arr = readArray(devicesObj, famKey, "devices", err);
        for (int i = 0; i < arr.size() && !err.has(); ++i) {
            QString path = QString("devices.%1[%2]").arg(famKey).arg(i);
            QJsonObject o = readObject(arr[i], path, err);
            if (err.has()) break;

            QString id, type;
            TargetRef target;
            if (!parseDeviceHeader(o, family, path, deviceIds, id, type, target, err)) break;
            QString p = QString("%1 '%2'").arg(path, id);

            if (family == DeviceFamily::Actuator) {
                ActuatorDef a;
                a.id = id; a.type = type; a.target = target;
                a.ctrlrange = readNumberList(o, "ctrlrange", 2, p, err);
                a.forceRange = readNumberList(o, "forcerange", 2, p, err);
                a.kp = readNumber(o, "kp", p, err, false, 0.0);
                a.kv = readNumber(o, "kv", p, err, false, 0.0);
                data.actuators.append(a);
            } else if (family == DeviceFamily::Sensor) {
                SensorDef s;
                s.id = id; s.type = type; s.target = target;
                data.sensors.append(s);
            } else if (family == DeviceFamily::Camera) {
                CameraDef c;
                c.id = id; c.type = type; c.target = target;
                c.resolution = readResolution(o, p, err);
                c.fovy = readNumber(o, "fovy", p, err, true);
                data.cameras.append(c);
            } else {
                DisplayDef d;
                d.id = id; d.target = target;
                d.resolution = readResolution(o, p, err);
                data.displays.append(d);
            }
        }
    }
}

static QJsonObject actuatorToJson(const ActuatorDef& a) {
    QJsonObject obj;
    obj["id"] = a.id;
    obj["type"] = a.type;
    obj["target"] = targetToJson(a.target);
    if (!a.ctrlrange.isEmpty()) obj["ctrlrange"] = doubleListToJson(a.ctrlrange);
    if (!a.forceRange.isEmpty()) obj["forcerange"] = doubleListToJson(a.forceRange);
    // kp/kv are written when the type uses them or they were authored non-zero
    if (a.type == "position" || a.kp != 0.0) obj["kp"] = a.kp;
    if (a.type == "position" || a.type == "velocity" || a.kv != 0.0) obj["kv"] = a.kv;
    return obj;
}

static QJsonObject devicesToJson(const ComponentData& d) {
    QJsonObject devices;

    auto put = [&devices](DeviceFamily f, const QJsonArray& arr) {
        if (!arr.isEmpty()) devices[DeviceTypes::familyKey(f)] = arr;
    };

    QJsonArray acts;
    for (const ActuatorDef& a : d.actuators) acts.append(actuatorToJson(a));
    put(DeviceFamily::Actuator, acts);

    QJsonArray sens;
    for (const SensorDef& s : d.sensors) {
        QJsonObject o;
        o["id"] = s.id;
        o["type"] = s.type;
        o["target"] = targetToJson(s.target);
        sens.append(o);
    }
    put(DeviceFamily::Sensor, sens);

    QJsonArray cams;
    for (const CameraDef& c : d.cameras) {
        QJsonObject o;
        o["id"] = c.id;
        o["type"] = c.type;
        o["target"] = targetToJson(c.target);
        o["resolution"] = QJsonArray{c.resolution.first, c.resolution.second};
        o["fovy"] = c.fovy;
        cams.append(o);
    }
    put(DeviceFamily::Camera, cams);

    QJsonArray disps;
    for (const DisplayDef& x : d.displays) {
        QJsonObject o;
        o["id"] = x.id;
        o["target"] = targetToJson(x.target);
        o["resolution"] = QJsonArray{x.resolution.first, x.resolution.second};
        disps.append(o);
    }
    put(DeviceFamily::Display, disps);

    return devices;
}




/*
 * Domain registry: type -> required parameter names. A domain type or
 * parameter that is not listed here is a load error. Only "ranged" and
 * "unbounded" exist; the loader deliberately has no other types.
 */
static const QMap<QString, QStringList>& domainRegistry() {
    static const QMap<QString, QStringList> reg = {
        {"ranged", {"min", "max"}},
        {"unbounded", {}},
    };
    return reg;
}

static DomainDef parseDomain(const QJsonObject& sigObj, const QString& path, LoadError& err) {
    DomainDef d;
    if (err.has() || !sigObj.contains("domain")) return d;

    QString p = path + " domain";
    QJsonObject o = readObject(sigObj["domain"], p, err);
    d.type = readString(o, "type", p, err, true);
    if (err.has()) return DomainDef();
    if (!domainRegistry().contains(d.type)) {
        err.set(QString("%1: unknown domain type '%2' (supported: %3)").arg(p, d.type, QStringList(domainRegistry().keys()).join(", ")));
        return DomainDef();
    }
    const QStringList& allowed = domainRegistry()[d.type];

    QJsonObject params;
    if (o.contains("parameters")) params = readObject(o["parameters"], p + " parameters", err);
    if (err.has()) return DomainDef();
    for (const QString& key : params.keys()) {
        if (!allowed.contains(key)) {
            err.set(QString("%1: unknown parameter '%2' for domain type '%3'").arg(p, key, d.type));
            return DomainDef();
        }
    }
    for (const QString& key : allowed) {
        d.parameters[key] = readNumber(params, key, p + " parameters", err, true);
    }
    if (err.has()) return DomainDef();
    return d;
}

static QJsonObject domainToJson(const DomainDef& d) {
    QJsonObject obj;
    obj["type"] = d.type;
    if (!d.parameters.isEmpty()) {
        QJsonObject params;
        for (auto it = d.parameters.constBegin(); it != d.parameters.constEnd(); ++it) params[it.key()] = it.value();
        obj["parameters"] = params;
    }
    return obj;
}

static QMap<QString, InterfaceDef> parseInterface(const QJsonArray& arr, bool isInput, QSet<QString>& names, LoadError& err) {
    QMap<QString, InterfaceDef> out;
    const QString dir = isInput ? "inputs" : "outputs";
    const QStringList allowedKinds = isInput ? QStringList{"joint", "display", "actuator", "emulator"}
                                             : QStringList{"sensor", "camera", "emulator"};

    for (int i = 0; i < arr.size() && !err.has(); ++i) {
        QString path = QString("interface.%1[%2]").arg(dir).arg(i);
        QJsonObject o = readObject(arr[i], path, err);
        if (err.has()) break;

        InterfaceDef def;
        def.name = readString(o, "name", path, err, true);
        claimId(names, def.name, "signal name", err);
        if (err.has()) break;
        path = QString("%1 signal '%2'").arg(dir, def.name);

        rejectKeys(o, {"pin_required"}, path, "wiring will be a separate future object", err);
        rejectKeys(o, {"data_type"}, path, "removed; values are always float", err);
        rejectKeys(o, {"range"}, path, "replaced by 'domain'", err);
        rejectKeys(o, {"target_joint", "target_site", "camera_name"}, path, "replaced by 'target'", err);

        def.unit = readString(o, "unit", path, err, false);
        def.physical = readBool(o, "physical", path, err, true, true);
        def.domain = parseDomain(o, path, err);
        def.target = parseTarget(o, path, allowedKinds, err);
        if (o.contains("componentLabels")) {
            for (const auto& lv : readArray(o, "componentLabels", path, err)) def.componentLabels.append(lv.toString());
        }

        if (!err.has()) {
            if (def.target.kind == "emulator") {
                def.channelType = readString(o, "channel_type", path, err, true);
                if (!err.has() && def.channelType != "scalar" && def.channelType != "vector")
                    err.set(QString("%1: channel_type must be 'scalar' or 'vector'").arg(path));
                double dim = readNumber(o, "dim", path, err, true);
                if (!err.has() && (dim < 1 || dim != std::floor(dim)))
                    err.set(QString("%1: dim must be a positive integer").arg(path));
                if (!err.has() && def.channelType == "scalar" && dim != 1)
                    err.set(QString("%1: a scalar signal must have dim 1").arg(path));
                def.dim = static_cast<int>(dim);
            } else {
                rejectKeys(o, {"channel_type", "dim"}, path, "derived from the target device; only emulator-kind signals state them", err);
            }
        }

        out[def.name] = def;
    }
    return out;
}

static QJsonArray interfaceToJson(const QMap<QString, InterfaceDef>& defs) {
    QJsonArray arr;
    for (const InterfaceDef& def : defs) {
        QJsonObject o;
        o["name"] = def.name;
        if (!def.unit.isEmpty()) o["unit"] = def.unit;
        if (def.domain.type != "unbounded" || !def.domain.parameters.isEmpty()) o["domain"] = domainToJson(def.domain);
        o["physical"] = def.physical;
        o["target"] = targetToJson(def.target);
        if (def.target.kind == "emulator") {
            o["channel_type"] = def.channelType;
            o["dim"] = def.dim;
        }
        if (!def.componentLabels.isEmpty()) {
            QJsonArray labels;
            for (const QString& l : def.componentLabels) labels.append(l);
            o["componentLabels"] = labels;
        }
        arr.append(o);
    }
    return arr;
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




static ComponentData loadFailure(const QString& modelId, const QString& msg) {
    ComponentData bad;
    bad.isValid = false;
    bad.modelId = modelId;
    bad.errorString = modelId.isEmpty() ? msg : QString("rsdef '%1': %2").arg(modelId, msg);
    return bad;
}


ComponentData ComponentData::fromJson(const QJsonObject& mainJson, const QString& basePath) {
    LoadError err;

    if (!mainJson.contains("schema")) {
        return loadFailure(QString(), QString("missing 'schema'; only schema %1 is supported (this looks like a schema 1 file, or not an rsdef)").arg(kSchemaVersion));
    }
    if (!mainJson["schema"].isDouble() || mainJson["schema"].toDouble() != kSchemaVersion) {
        QString got = mainJson["schema"].isDouble() ? QString::number(mainJson["schema"].toDouble()) : typeName(mainJson["schema"]);
        return loadFailure(QString(), QString("unsupported schema %1; only schema %2 is supported").arg(got).arg(kSchemaVersion));
    }

    ComponentData data;
    data.basePath = basePath;
    data.modelId = readString(mainJson, "id", "top level", err, true);
    if (err.has()) return loadFailure(QString(), err.msg);

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

    // ---- kinematics ----
    QJsonObject kinematicsObj = readObject(mainJson["kinematics"], "kinematics", err);
    if (err.has()) return loadFailure(data.modelId, err.msg);
    data.defaultBodyId = readString(kinematicsObj, "default_body", "kinematics", err, true);

    QSet<QString> geomIds, siteIds, jointIds, tendonIds;
    data.bodies = parseBodies(readArray(kinematicsObj, "bodies", "kinematics", err), geomIds, siteIds, err); // no longer takes modelId -- see parseBodies note
    data.joints = parseJoints(readArray(kinematicsObj, "joints", "kinematics", err), jointIds, err);
    data.tendons = parseTendons(readArray(kinematicsObj, "tendons", "kinematics", err), err);
    if (err.has()) return loadFailure(data.modelId, err.msg);

    if (!data.bodies.contains(data.defaultBodyId))
        return loadFailure(data.modelId, QString("kinematics: default_body '%1' is not a body").arg(data.defaultBodyId));
    for (const Edge& e : data.joints) {
        for (const QString& b : {e.bodyA, e.bodyB}) {
            if (!data.bodies.contains(b))
                return loadFailure(data.modelId, QString("joint '%1': body '%2' does not exist").arg(e.id, b));
        }
    }
    for (const TendonDef& t : data.tendons) {
        tendonIds.insert(t.id);
        for (const TendonTerm& term : t.terms) {
            if (!jointIds.contains(term.joint))
                return loadFailure(data.modelId, QString("tendon '%1': joint '%2' does not exist").arg(t.id, term.joint));
        }
    }

    QJsonArray connectorsArr = mainJson["connectors"].toArray();
    data.connectors = parseConnectors(connectorsArr);
    for (const ConnectorDef& c : data.connectors) {
        if (!data.bodies.contains(c.body))
            return loadFailure(data.modelId, QString("connector '%1': body '%2' does not exist").arg(c.id, c.body));
    }

    // ---- devices ----
    if (mainJson.contains("devices")) {
        parseDevices(readObject(mainJson["devices"], "devices", err), data, err);
        if (err.has()) return loadFailure(data.modelId, err.msg);
    }

    QMap<QString, QSet<QString>> ids;
    ids["joint"] = jointIds;
    ids["tendon"] = tendonIds;
    ids["site"] = siteIds;
    ids["geom"] = geomIds;
    for (const ActuatorDef& a : data.actuators) ids["actuator"].insert(a.id);
    for (const SensorDef& s : data.sensors) ids["sensor"].insert(s.id);
    for (const CameraDef& c : data.cameras) ids["camera"].insert(c.id);
    for (const DisplayDef& d : data.displays) ids["display"].insert(d.id);

    for (const ActuatorDef& a : data.actuators) requireResolved(a.target, ids, QString("actuator '%1'").arg(a.id), err);
    for (const SensorDef& s : data.sensors) requireResolved(s.target, ids, QString("sensor '%1'").arg(s.id), err);
    for (const CameraDef& c : data.cameras) requireResolved(c.target, ids, QString("camera '%1'").arg(c.id), err);
    for (const DisplayDef& d : data.displays) requireResolved(d.target, ids, QString("display '%1'").arg(d.id), err);
    if (err.has()) return loadFailure(data.modelId, err.msg);

    // ---- interface ----
    QJsonObject interfaceObj;
    if (mainJson.contains("interface")) interfaceObj = readObject(mainJson["interface"], "interface", err);
    rejectKeys(mainJson, {"io"}, "top level", "renamed to 'interface'", err);
    QSet<QString> signalNames;
    data.interfaceInputs = parseInterface(readArray(interfaceObj, "inputs", "interface", err), true, signalNames, err);
    data.interfaceOutputs = parseInterface(readArray(interfaceObj, "outputs", "interface", err), false, signalNames, err);
    if (err.has()) return loadFailure(data.modelId, err.msg);

    for (const InterfaceDef& d : data.interfaceInputs) {
        if (d.target.kind != "emulator") requireResolved(d.target, ids, QString("input signal '%1'").arg(d.name), err);
    }
    for (const InterfaceDef& d : data.interfaceOutputs) {
        if (d.target.kind != "emulator") requireResolved(d.target, ids, QString("output signal '%1'").arg(d.name), err);
    }
    if (err.has()) return loadFailure(data.modelId, err.msg);

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




int ComponentData::interfaceDim(const InterfaceDef& def) const {
    const QString& kind = def.target.kind;
    if (kind == "emulator") return def.dim;
    if (kind == "joint") return 1;

    const DeviceTypeInfo* row = nullptr;
    if (kind == "actuator") {
        for (const ActuatorDef& a : actuators) if (a.id == def.target.id) row = DeviceTypes::find(DeviceFamily::Actuator, a.type);
    } else if (kind == "sensor") {
        for (const SensorDef& s : sensors) if (s.id == def.target.id) row = DeviceTypes::find(DeviceFamily::Sensor, s.type);
    }
    return row ? row->dim : 0;   // display / camera (images) and unresolved targets
}


SignalShape ComponentData::interfaceShape(const InterfaceDef& def) const {
    const QString& kind = def.target.kind;
    if (kind == "emulator") return def.channelType == "vector" ? SignalShape::Vector : SignalShape::Scalar;
    if (kind == "joint") return SignalShape::Scalar;
    if (kind == "display" || kind == "camera") return SignalShape::Image;

    if (kind == "actuator") {
        for (const ActuatorDef& a : actuators)
            if (a.id == def.target.id) if (auto* row = DeviceTypes::find(DeviceFamily::Actuator, a.type)) return row->shape;
    } else if (kind == "sensor") {
        for (const SensorDef& s : sensors)
            if (s.id == def.target.id) if (auto* row = DeviceTypes::find(DeviceFamily::Sensor, s.type)) return row->shape;
    }
    return SignalShape::Scalar;
}




QJsonObject ComponentData::toJson() const {
    QJsonObject root;
    root["schema"] = kSchemaVersion;
    root["id"] = modelId;

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

    if (!tendons.isEmpty()) {
        QJsonArray tendonsArr;
        for (const TendonDef& t : tendons) tendonsArr.append(tendonToJson(t));
        kinematicsObj["tendons"] = tendonsArr;
    }

    root["kinematics"] = kinematicsObj;

    QJsonArray connectorsArr;
    for (const ConnectorDef& c : connectors) connectorsArr.append(connectorToJson(c));
    root["connectors"] = connectorsArr;

    QJsonObject devicesObj = devicesToJson(*this);
    if (!devicesObj.isEmpty()) root["devices"] = devicesObj;

    QJsonObject interfaceObj;
    interfaceObj["inputs"] = interfaceToJson(interfaceInputs);
    interfaceObj["outputs"] = interfaceToJson(interfaceOutputs);
    root["interface"] = interfaceObj;

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
