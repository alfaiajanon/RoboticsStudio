#include "Stage.h"

#include <algorithm>
#include <cmath>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include "Utils/Log.h"



#pragma region PropHandle / RobotPose

PropHandle::Command PropHandle::takeCommand() {
    std::lock_guard<std::mutex> lock(mutex);
    Command c = command;
    command.teleport = false;
    return c;
}

void PropHandle::publish(const Vec3& p, const Vec3& v, double yawDeg) {
    std::lock_guard<std::mutex> lock(mutex);
    pos = p;
    vel = v;
    yaw = yawDeg;
}

void PropHandle::resetRuntime() {
    std::lock_guard<std::mutex> lock(mutex);
    command = Command();
    pos = basePos;
    vel = {0, 0, 0};
    yaw = baseYaw;
}

void PropHandle::setVelocity(double vx, double vy) {
    std::lock_guard<std::mutex> lock(mutex);
    command.vx = vx;
    command.vy = vy;
    command.local = false;
}

void PropHandle::move(double forward, double strafe) {
    std::lock_guard<std::mutex> lock(mutex);
    command.vx = forward;
    command.vy = strafe;
    command.local = true;
}

void PropHandle::setYawRate(double degPerSec) {
    std::lock_guard<std::mutex> lock(mutex);
    command.yawRate = degPerSec;
}

void PropHandle::stop() {
    std::lock_guard<std::mutex> lock(mutex);
    command.vx = command.vy = command.yawRate = 0;
}

void PropHandle::teleport(double x, double y, double z, double yawDeg) {
    std::lock_guard<std::mutex> lock(mutex);
    command.teleport = true;
    command.tp[0] = x; command.tp[1] = y; command.tp[2] = z; command.tp[3] = yawDeg;
}

static QVariantMap xyz(const Vec3& v) {
    return QVariantMap{{"x", v[0]}, {"y", v[1]}, {"z", v[2]}};
}

QVariantMap PropHandle::getPosition() const { std::lock_guard<std::mutex> lock(mutex); return xyz(pos); }
QVariantMap PropHandle::getVelocity() const { std::lock_guard<std::mutex> lock(mutex); return xyz(vel); }
double PropHandle::getYaw() const           { std::lock_guard<std::mutex> lock(mutex); return yaw; }


void RobotPose::publish(const Vec3& p, const Vec3& rpyDeg) {
    std::lock_guard<std::mutex> lock(mutex);
    pos = p;
    rpy = rpyDeg;
}

QVariantMap RobotPose::getPosition() const { std::lock_guard<std::mutex> lock(mutex); return xyz(pos); }

QVariantMap RobotPose::getRotation() const {
    std::lock_guard<std::mutex> lock(mutex);
    return QVariantMap{{"roll", rpy[0]}, {"pitch", rpy[1]}, {"yaw", rpy[2]}};
}







#pragma region Stage

Stage::Stage() {}
Stage::~Stage() {}


void Stage::setData(const Data& d) {
    data = d;
    rebuildHandles();
}


// Keeps the handle of a prop that is still controlled (the stage script and
// physics thread may hold it), creates new ones, drops the rest.
void Stage::rebuildHandles() {
    std::vector<std::unique_ptr<PropHandle>> old = std::move(handles);
    handles.clear();
    for (const Prop& p : data.props) {
        if (p.motion != "controlled") continue;
        auto it = std::find_if(old.begin(), old.end(), [&](const auto& h) { return h && h->id == p.id; });
        std::unique_ptr<PropHandle> h = it != old.end() ? std::move(*it) : std::make_unique<PropHandle>(p.id);
        h->basePos = p.pos;
        h->baseYaw = p.euler[2];
        h->resetRuntime();
        handles.push_back(std::move(h));
    }
}


QString Stage::absPath(const QString& rel) const {
    return QDir(projectDir).absoluteFilePath(rel);
}







#pragma region json

template <size_t N>
static std::array<double, N> readVec(const QJsonValue& v, const std::array<double, N>& def) {
    QJsonArray arr = v.toArray();
    if (arr.size() != static_cast<int>(N)) return def;
    std::array<double, N> out;
    for (size_t i = 0; i < N; ++i) out[i] = arr[static_cast<int>(i)].toDouble(def[i]);
    return out;
}

template <size_t N>
static QJsonArray writeVec(const std::array<double, N>& v) {
    QJsonArray arr;
    for (double x : v) arr.append(x);
    return arr;
}


Stage::Data Stage::fromJson(const QJsonObject& obj) {
    Data d;   // defaults reproduce the pre-stage look

    QJsonObject e = obj["environment"].toObject();
    Environment& env = d.env;
    env.skyTop       = readVec(e["skyTop"], env.skyTop);
    env.skyBottom    = readVec(e["skyBottom"], env.skyBottom);
    env.ambient      = readVec(e["ambient"], env.ambient);
    env.headlight    = readVec(e["headlight"], env.headlight);
    QJsonObject light = e["light"].toObject();
    env.lightDir     = readVec(light["dir"], env.lightDir);
    env.lightDiffuse = readVec(light["diffuse"], env.lightDiffuse);
    env.castShadow   = light["castShadow"].toBool(env.castShadow);
    env.shadowSize   = e["shadowSize"].toInt(env.shadowSize);
    QJsonObject fog  = e["fog"].toObject();
    env.fog          = fog["enabled"].toBool(env.fog);
    env.fogStart     = fog["start"].toDouble(env.fogStart);
    env.fogEnd       = fog["end"].toDouble(env.fogEnd);
    env.fogColor     = readVec(fog["color"], env.fogColor);
    env.gravity      = readVec(e["gravity"], env.gravity);
    env.timestep     = e["timestep"].toDouble(env.timestep);

    QJsonObject f = obj["floor"].toObject();
    Floor& fl = d.floor;
    fl.type      = f["type"].toString(fl.type);
    fl.pattern   = f["pattern"].toString(fl.pattern);
    fl.color1    = readVec(f["color1"], fl.color1);
    fl.color2    = readVec(f["color2"], fl.color2);
    fl.texRepeat = f["texRepeat"].toDouble(fl.texRepeat);
    fl.file      = f["file"].toString();
    fl.scale     = readVec(f["scale"], fl.scale);
    fl.pos       = readVec(f["pos"], fl.pos);
    fl.euler     = readVec(f["euler"], fl.euler);
    fl.friction  = readVec(f["friction"], fl.friction);

    for (const QJsonValue& v : obj["props"].toArray()) {
        QJsonObject o = v.toObject();
        Prop p;
        p.id         = o["id"].toString();
        p.shape      = o["shape"].toString(p.shape);
        p.size       = readVec(o["size"], p.size);
        p.mesh       = o["mesh"].toString();
        p.pos        = readVec(o["pos"], p.pos);
        p.euler      = readVec(o["euler"], p.euler);
        p.rgba       = readVec(o["rgba"], p.rgba);
        p.mass       = o["mass"].toDouble(p.mass);
        p.friction   = readVec(o["friction"], p.friction);
        p.motion     = o["motion"].toString(p.motion);
        QJsonObject c = o["control"].toObject();
        p.maxSpeed   = c["maxSpeed"].toDouble(p.maxSpeed);
        p.maxYawRate = c["maxYawRate"].toDouble(p.maxYawRate);
        p.response   = c["response"].toDouble(p.response);
        d.props.append(p);
    }

    // {"current", "paths"}, like the top-level MCU "script"
    QJsonObject s = obj["script"].toObject();
    for (const QJsonValue& v : s["paths"].toArray()) d.scripts.append(v.toString());
    d.currentScript = s["current"].toInt(d.scripts.isEmpty() ? -1 : 0);
    if (d.currentScript >= d.scripts.size()) d.currentScript = -1;
    d.robotSpawn = readVec(obj["robotSpawn"], d.robotSpawn);

    QString err = validate(d);
    if (!err.isEmpty()) Log::error("Stage: " + err);
    return d;
}


QJsonObject Stage::toJson(const Data& d) {
    const Environment& env = d.env;
    QJsonObject e{
        {"skyTop", writeVec(env.skyTop)},
        {"skyBottom", writeVec(env.skyBottom)},
        {"ambient", writeVec(env.ambient)},
        {"headlight", writeVec(env.headlight)},
        {"light", QJsonObject{{"dir", writeVec(env.lightDir)},
                              {"diffuse", writeVec(env.lightDiffuse)},
                              {"castShadow", env.castShadow}}},
        {"shadowSize", env.shadowSize},
        {"fog", QJsonObject{{"enabled", env.fog}, {"start", env.fogStart},
                            {"end", env.fogEnd}, {"color", writeVec(env.fogColor)}}},
        {"gravity", writeVec(env.gravity)},
        {"timestep", env.timestep},
    };

    const Floor& fl = d.floor;
    QJsonObject f{
        {"type", fl.type}, {"pattern", fl.pattern},
        {"color1", writeVec(fl.color1)}, {"color2", writeVec(fl.color2)},
        {"texRepeat", fl.texRepeat}, {"file", fl.file},
        {"scale", writeVec(fl.scale)}, {"pos", writeVec(fl.pos)},
        {"euler", writeVec(fl.euler)}, {"friction", writeVec(fl.friction)},
    };

    QJsonArray props;
    for (const Prop& p : d.props) {
        QJsonObject o{
            {"id", p.id}, {"shape", p.shape}, {"size", writeVec(p.size)},
            {"pos", writeVec(p.pos)}, {"euler", writeVec(p.euler)}, {"rgba", writeVec(p.rgba)},
            {"mass", p.mass}, {"friction", writeVec(p.friction)}, {"motion", p.motion},
        };
        if (p.shape == "mesh") o["mesh"] = p.mesh;
        if (p.motion == "controlled") {
            o["control"] = QJsonObject{{"maxSpeed", p.maxSpeed}, {"maxYawRate", p.maxYawRate},
                                       {"response", p.response}};
        }
        props.append(o);
    }

    return QJsonObject{
        {"environment", e}, {"floor", f}, {"props", props},
        {"script", QJsonObject{{"current", d.currentScript},
                               {"paths", QJsonArray::fromStringList(d.scripts)}}},
        {"robotSpawn", writeVec(d.robotSpawn)},
    };
}


QString Stage::validate(const Data& d) {
    static const QRegularExpression idRe("^[A-Za-z0-9_]+$");
    static const QSet<QString> shapes{"box", "sphere", "cylinder", "capsule", "mesh"};
    static const QSet<QString> motions{"static", "dynamic", "controlled"};
    static const QSet<QString> floors{"plane", "mesh", "heightfield", "none"};

    if (!floors.contains(d.floor.type)) return "unknown floor type '" + d.floor.type + "'";
    QSet<QString> ids;
    for (const Prop& p : d.props) {
        if (!idRe.match(p.id).hasMatch()) return "prop id '" + p.id + "' must be letters, digits or '_'";
        if (ids.contains(p.id)) return "duplicate prop id '" + p.id + "'";
        ids.insert(p.id);
        if (!shapes.contains(p.shape)) return "prop '" + p.id + "': unknown shape '" + p.shape + "'";
        if (!motions.contains(p.motion)) return "prop '" + p.id + "': unknown motion '" + p.motion + "'";
        if (p.mass <= 0) return "prop '" + p.id + "': mass must be positive";
    }
    return {};
}







#pragma region mjcf

static QString v3(const Vec3& v) {
    return QString("%1 %2 %3").arg(v[0]).arg(v[1]).arg(v[2]);
}

static QString v4(const Vec4& v) {
    return QString("%1 %2 %3 %4").arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]);
}

static QString attrPath(const QString& path) {
    return path.toHtmlEscaped();
}

// Mesh props/floors only make it into the model when their file exists:
// a missing file would fail the whole MuJoCo load.
static bool fileUsable(const QString& abs, const QString& what) {
    if (QFileInfo::exists(abs)) return true;
    Log::warning("Stage: " + what + " file not found, skipped: " + abs);
    return false;
}


QString Stage::assetsXML() const {
    const Environment& env = data.env;
    const Floor& fl = data.floor;

    QString xml = QString("    <texture type=\"skybox\" builtin=\"gradient\" rgb1=\"%1\" rgb2=\"%2\" width=\"512\" height=\"512\"/>\n")
                    .arg(v3(env.skyTop), v3(env.skyBottom));

    if (fl.type == "plane") {
        if (fl.pattern == "checker") {
            xml += QString("    <texture name=\"stage_floor_tex\" type=\"2d\" builtin=\"checker\" rgb1=\"%1\" rgb2=\"%2\" width=\"512\" height=\"512\" mark=\"cross\" markrgb=\"0.8 0.8 0.8\"/>\n")
                    .arg(v3(fl.color1), v3(fl.color2));
            xml += QString("    <material name=\"stage_floor_mat\" reflectance=\"0.0\" texture=\"stage_floor_tex\" texrepeat=\"%1 %1\" texuniform=\"true\"/>\n")
                    .arg(fl.texRepeat);
        } else {
            xml += QString("    <material name=\"stage_floor_mat\" reflectance=\"0.0\" rgba=\"%1 1\"/>\n").arg(v3(fl.color1));
        }
    } else if (fl.type == "mesh" && fileUsable(absPath(fl.file), "floor mesh")) {
        xml += QString("    <mesh name=\"stage_floor_mesh\" file=\"%1\" scale=\"%2\"/>\n").arg(attrPath(absPath(fl.file)), v3(fl.scale));
    } else if (fl.type == "heightfield" && fileUsable(absPath(fl.file), "floor heightfield")) {
        xml += QString("    <hfield name=\"stage_floor_hfield\" file=\"%1\" size=\"%2 0.01\"/>\n").arg(attrPath(absPath(fl.file)), v3(fl.scale));
    }

    for (const Prop& p : data.props) {
        if (p.shape != "mesh" || !fileUsable(absPath(p.mesh), "prop '" + p.id + "' mesh")) continue;
        xml += QString("    <mesh name=\"prop_%1_mesh\" file=\"%2\" scale=\"%3\"/>\n").arg(p.id, attrPath(absPath(p.mesh)), v3(p.size));
    }
    return xml;
}


// The geom of a prop: a static one sits in the worldbody at the prop's pos
// and has no mass; a moving one is at its body's origin and carries the mass.
static QString propGeomXML(const Stage::Prop& p, bool isStatic, const QString& indent) {
    QString shape = p.shape == "mesh" ? QString("type=\"mesh\" mesh=\"prop_%1_mesh\"").arg(p.id)
                                      : QString("type=\"%1\" size=\"%2\"").arg(p.shape, v3(p.size));
    QString xml = indent + QString("<geom name=\"prop_%1\" %2 euler=\"%3\" rgba=\"%4\" friction=\"%5\" condim=\"3\"")
                    .arg(p.id, shape, v3(p.euler), v4(p.rgba), v3(p.friction));
    if (isStatic) xml += QString(" pos=\"%1\"").arg(v3(p.pos));
    else          xml += QString(" mass=\"%1\"").arg(p.mass);
    // A controlled prop's own friction decides its contacts, so a low value
    // lets it glide at the commanded speed (floor drag costs ~mu*g/response).
    if (p.motion == "controlled") xml += " priority=\"1\"";
    return xml + "/>\n";
}


QString Stage::worldBodyXML(bool isSimulation) const {
    const Environment& env = data.env;
    const Floor& fl = data.floor;

    QString xml = QString("    <light name=\"stage_light\" directional=\"true\" diffuse=\"%1\" specular=\"0.2 0.2 0.2\" pos=\"0 0 5\" dir=\"%2\" castshadow=\"%3\"/>\n")
                    .arg(v3(env.lightDiffuse), v3(env.lightDir), env.castShadow ? "true" : "false");

    QString floorAttrs = QString("pos=\"%1\" euler=\"%2\" friction=\"%3\" condim=\"3\"").arg(v3(fl.pos), v3(fl.euler), v3(fl.friction));
    if (fl.type == "plane") {
        xml += "    <geom name=\"stage_floor\" type=\"plane\" size=\"0 0 1\" material=\"stage_floor_mat\" " + floorAttrs + "/>\n";
    } else if (fl.type == "mesh" && QFileInfo::exists(absPath(fl.file))) {
        xml += QString("    <geom name=\"stage_floor\" type=\"mesh\" mesh=\"stage_floor_mesh\" rgba=\"%1 1\" ").arg(v3(fl.color1)) + floorAttrs + "/>\n";
    } else if (fl.type == "heightfield" && QFileInfo::exists(absPath(fl.file))) {
        xml += QString("    <geom name=\"stage_floor\" type=\"hfield\" hfield=\"stage_floor_hfield\" rgba=\"%1 1\" ").arg(v3(fl.color1)) + floorAttrs + "/>\n";
    }

    for (const Prop& p : data.props) {
        if (p.shape == "mesh" && !QFileInfo::exists(absPath(p.mesh))) continue;

        if (p.motion == "static") {
            xml += propGeomXML(p, true, "    ");
            continue;
        }

        // Edit mode never steps: moving props get no joints and stay where authored.
        xml += QString("    <body name=\"prop_%1\" pos=\"%2\">\n").arg(p.id, v3(p.pos));
        if (isSimulation && p.motion == "dynamic") {
            xml += QString("      <freejoint name=\"prop_%1_free\"/>\n").arg(p.id);
        } else if (isSimulation) {
            // Upright character body: translate freely, turn only about z.
            // Slides come first so their axes stay world-aligned.
            xml += QString("      <joint name=\"prop_%1_x\" type=\"slide\" axis=\"1 0 0\"/>\n").arg(p.id);
            xml += QString("      <joint name=\"prop_%1_y\" type=\"slide\" axis=\"0 1 0\"/>\n").arg(p.id);
            xml += QString("      <joint name=\"prop_%1_z\" type=\"slide\" axis=\"0 0 1\"/>\n").arg(p.id);
            xml += QString("      <joint name=\"prop_%1_yaw\" type=\"hinge\" axis=\"0 0 1\"/>\n").arg(p.id);
        }
        xml += propGeomXML(p, false, "      ");
        xml += "    </body>\n";
    }
    return xml;
}


/*
 * Velocity servos for controlled props. Gains scale with the prop's mass
 * (and footprint, for yaw) so `response` (1/s) means the same for a pebble
 * and a crate, and stays inside explicit-Euler stability (response*dt < 2).
 */
QString Stage::actuatorsXML(bool isSimulation) const {
    if (!isSimulation) return {};

    QString xml;
    for (const Prop& p : data.props) {
        if (p.motion != "controlled") continue;
        if (p.shape == "mesh" && !QFileInfo::exists(absPath(p.mesh))) continue;

        double kv = p.response * p.mass;
        double r = p.shape == "mesh" ? 0.05 : std::max(p.size[0], p.shape == "box" ? p.size[1] : p.size[0]);
        double kvYaw = kv * r * r;
        double yawMax = p.maxYawRate * M_PI / 180.0;

        xml += QString("    <velocity name=\"prop_%1_vx\" joint=\"prop_%1_x\" kv=\"%2\" ctrllimited=\"true\" ctrlrange=\"%3 %4\"/>\n")
                .arg(p.id).arg(kv).arg(-p.maxSpeed).arg(p.maxSpeed);
        xml += QString("    <velocity name=\"prop_%1_vy\" joint=\"prop_%1_y\" kv=\"%2\" ctrllimited=\"true\" ctrlrange=\"%3 %4\"/>\n")
                .arg(p.id).arg(kv).arg(-p.maxSpeed).arg(p.maxSpeed);
        xml += QString("    <velocity name=\"prop_%1_vyaw\" joint=\"prop_%1_yaw\" kv=\"%2\" ctrllimited=\"true\" ctrlrange=\"%3 %4\"/>\n")
                .arg(p.id).arg(kvYaw).arg(-yawMax).arg(yawMax);
    }
    return xml;
}


QString Stage::visualXML() const {
    const Environment& env = data.env;
    QString xml;
    xml += QString("    <headlight ambient=\"%1\" diffuse=\"%2\"/>\n").arg(v3(env.ambient), v3(env.headlight));
    xml += QString("    <quality shadowsize=\"%1\"/>\n").arg(env.shadowSize);
    xml += QString("    <map fogstart=\"%1\" fogend=\"%2\"/>\n").arg(env.fogStart).arg(env.fogEnd);
    xml += QString("    <rgba fog=\"%1 1\"/>\n").arg(v3(env.fogColor));
    return xml;
}


QString Stage::optionXML() const {
    return QString("  <option timestep=\"%1\" gravity=\"%2\"/>\n\n").arg(data.env.timestep).arg(v3(data.env.gravity));
}
