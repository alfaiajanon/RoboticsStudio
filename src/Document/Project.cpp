#include "Project.h"
#include "Utils/Global.h"
#include "Utils/Log.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/LibraryManager.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/ErrorSystem/EmulatorFactory.h"

#include <QDir>
#include <QFile>
#include <queue>
#include <cmath>
#include <functional>



Project::Project() : rootComponent(nullptr) {}


Project::~Project() {
    clear();
}


ComponentInstance* Project::getComponentByUid(int uid) {
    return componentMap.value(uid, nullptr);
}


ComponentInstance* Project::getRootComponent() {
    return rootComponent;
}

void Project::setRootComponent(ComponentInstance* comp)  {
    Log::info("Root component set: " + comp->name);
    rootComponent = comp;
    rootComponent->parentConnector = "root";
    rootComponent->parentUid = 0;
}


QMap<int, ComponentInstance*>& Project::getComponentMap() {
    return componentMap;
}


QJsonObject Project::getProjectData() {
    return projectData;
}


int Project::getSimulationSeed() const {
    return projectData["simulation"].toObject()["seed"].toInt(1);
}


void Project::setSimulationSeed(int seed) {
    QJsonObject sim = projectData["simulation"].toObject();
    sim["seed"] = seed;
    projectData["simulation"] = sim;
}



void Project::resetRootComponent() {
    if (rootComponent) {
        rootComponent->parentUid = -1;
        rootComponent->parentConnector = "";
    }
    rootComponent = nullptr;
}




void Project::setProjectData(QJsonObject data) {
    projectData = data;
}


void Project::setProjectPath(const QString& path) {
    projectPath = path;
}






void Project::clear() {
    qDeleteAll(componentMap);
    componentMap.clear();

    qDeleteAll(constraintList);
    constraintList.clear();

    rootComponent = nullptr;
}




void Project::setScript(int idx){
    if (idx < 0 && scriptPaths.isEmpty()) {   // last script deleted: no active script
        currentScriptIdx = -1;
        currentScriptContent.clear();
        return;
    }
    if(idx < 0 || idx >= scriptPaths.size()) {
        Log::error("Invalid script index: " + QString::number(idx));
        return;
    }
    QString path = scriptPaths.at(idx).toString();
    QString fullPath=directoryPath + "/" + path;
    QFile file(fullPath);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open script file: " + path);
        return;
    }

    currentScriptIdx = idx;

    currentScriptContent = QString::fromUtf8(file.readAll());
    file.close();

    Log::info("Loaded script: " + fullPath);
}




bool Project::usesFirmware() const {
    const QString path = activeSketchPath().toLower();
    return path.endsWith(".ino") || path.endsWith(".cpp");
}



bool Project::isBoard(const ComponentInstance* comp) {
    return comp && comp->getBlueprint() && comp->getBlueprint()->board.present;
}



QList<ComponentInstance*> Project::boards() const {
    QList<ComponentInstance*> out;
    for (ComponentInstance* comp : componentMap) if (isBoard(comp)) out.append(comp);   // QMap: by uid
    return out;
}



ComponentInstance* Project::activeBoard() const {
    const QList<ComponentInstance*> all = boards();
    for (ComponentInstance* b : all) if (!b->firmware.isEmpty()) return b;
    return all.isEmpty() ? nullptr : all.first();
}



RealBoard Project::realBoard() const {
    return RealBoard::build(activeBoard(), rootComponent, wiring);
}



QString Project::activeSketchPath() const {
    if (level == ProjectLevel::Real) {
        ComponentInstance* b = activeBoard();
        return b ? b->firmware : QString();
    }
    if (currentScriptIdx < 0 || currentScriptIdx >= scriptPaths.size()) return QString();
    return scriptPaths[currentScriptIdx].toString();
}



QString Project::readActiveSketch() const {
    const QString rel = activeSketchPath();
    if (rel.isEmpty()) return {};
    QFile file(directoryPath + "/" + rel);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open firmware sketch: " + rel);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}



QString Project::pinsHeader() {
    if (level == ProjectLevel::Real) {
        const RealBoard rb = realBoard();
        if (rb.isValid()) return rb.header(getName());
    }
    return idealBoard().header(getName());
}



bool Project::writeBoardHeader() {
    if (level != ProjectLevel::Real || directoryPath.isEmpty()) return true;
    const RealBoard rb = realBoard();
    if (!rb.isValid() || rb.def().headerFolder.isEmpty()) return true;
    QDir dir(directoryPath);
    if (!dir.mkpath(rb.def().headerFolder)) {
        Log::error("Can't create the board header folder: " + dir.filePath(rb.def().headerFolder));
        return false;
    }
    const QString path = dir.filePath(rb.def().headerFolder + "/robot_pins.h");
    const QByteArray text = rb.header(getName()).toUtf8();
    QFile existing(path);
    if (existing.open(QFile::ReadOnly) && existing.readAll() == text) return true;   // unchanged: keep the mtime
    existing.close();
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        Log::error("Can't write the board header: " + path);
        return false;
    }
    file.write(text);
    return true;
}



QString Project::readStageScript() const {
    const QString rel = stage.getData().activeScript();
    if (rel.isEmpty()) return {};
    QFile file(directoryPath + "/" + rel);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open stage script: " + rel);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}




void Project::reloadScript(){
    QString fullpath=directoryPath + "/" + getScriptPath();
    QFile file(fullpath);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open script file (reload): " + fullpath);
        return;
    }

    currentScriptContent = QString::fromUtf8(file.readAll());
    file.close();
}





#pragma region load/unload
/*
 * Loads the project from a JSON file, parsing components and explicit constraints.
 * Builds the macro-graph hierarchical tree in memory.
 */



bool Project::loadProject(const QString& path) {
    clear();
    projectPath = path;

    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open project file: " + path);
        return false;
    }

    projectData = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    directoryPath = QFileInfo(path).absolutePath();

    // Schema 3 adds the level and the wiring; a schema 2 file (no "schema")
    // is a Prototype project with no wires, saved as 3.
    const int schema = projectData["schema"].toInt(2);
    if (schema > kSchemaVersion)
        Log::warning(QString("Project schema %1 is newer than this app (%2): unknown fields are dropped on save").arg(schema).arg(kSchemaVersion));
    else if (schema < kSchemaVersion)
        Log::info(QString("Project is schema %1: loaded as a Prototype project with no wiring, saved as schema %2").arg(schema).arg(kSchemaVersion));
    level = projectData["level"].toString() == "real" ? ProjectLevel::Real : ProjectLevel::Prototype;
    const QJsonObject wiringObj = projectData["wiring"].toObject();
    wiring = Netlist::fromJson(wiringObj["wires"].toArray());
    schematicLayout.clear();
    const QJsonObject layoutObj = wiringObj["layout"].toObject();
    for (auto it = layoutObj.begin(); it != layoutObj.end(); ++it) {
        const QJsonArray xy = it.value().toArray();
        if (xy.size() == 2) schematicLayout.insert(it.key().toInt(), QPointF(xy[0].toDouble(), xy[1].toDouble()));
    }

    QJsonObject scriptObj = projectData["script"].toObject();
    scriptPaths   = scriptObj["paths"].toArray();
    int scriptIdx = scriptObj["current"].toInt();
    setScript(scriptIdx);

    plotting = projectData["plotting"].toObject();

    stage.setProjectDirectory(directoryPath);
    stage.setData(Stage::fromJson(projectData["stage"].toObject()));

    parseAssembly();
    buildHierarchy();
    applyDefaults();

    // Wires to parts or pins that no longer exist (a part file changed) are dropped.
    for (const Wire& w : QList<Wire>(wiring.wires())) {
        for (const PinRef& r : {w.a, w.b}) {
            ComponentInstance* comp = componentMap.value(r.uid, nullptr);
            bool found = false;
            if (comp && comp->getBlueprint())
                for (const PinDef& p : comp->getBlueprint()->pins) found = found || p.id == r.pin;
            if (!found) {
                Log::warning(QString("Wiring: pin %1.%2 doesn't exist; its wire was dropped").arg(r.uid).arg(r.pin));
                wiring.removeWire(w);
                break;
            }
        }
    }

    return true;
}



void Project::unloadProject() {
    clear();
    projectData = QJsonObject();
    plotting = QJsonObject();
    projectPath = "";
    directoryPath = "";
    currentScriptContent = "";
    scriptPaths = QJsonArray();
    level = ProjectLevel::Prototype;
    wiring.clear();
    schematicLayout.clear();
    stage.setProjectDirectory("");
    stage.setData(Stage::Data());
}








#pragma region save data

bool Project::saveProject() {
    if (projectPath.isEmpty()) {
        Log::error("Cannot save project: Project path is empty.");
        return false;
    }

    QJsonArray componentsArray;
    for (ComponentInstance* comp : componentMap.values()) {
        QJsonObject compObj;
        compObj["uid"] = comp->uid;
        compObj["name"] = comp->name;
        compObj["model_id"]=comp->modelId;
        if (isBoard(comp) && !comp->firmware.isEmpty()) compObj["firmware"] = comp->firmware;

        if (comp->parentUid == -1) {
            compObj["connection"] = QJsonValue::Null;
        } else {
            QJsonObject connObj;
            connObj["parent_uid"] = comp->parentUid;
            connObj["parent_connector"] = comp->parentConnector;
            connObj["self_connector"] = comp->selfConnector;
            connObj["snap_angle"] = static_cast<double>(comp->snapAngle);
            compObj["connection"] = connObj;
        }

        saveDefaults();
        QJsonObject paramsObj;
        for (auto it = comp->parameters.constBegin(); it != comp->parameters.constEnd(); ++it) {
            paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        compObj["parameters"] = paramsObj;

        componentsArray.append(compObj);
    }

    QJsonArray constraintsArray;
    for (Constraint* constraint : constraintList) {
        QJsonObject constraintObj;
        constraintObj["component_a"] = constraint->componentAUid;
        constraintObj["component_b"] = constraint->componentBUid;
        constraintObj["connector_a"] = constraint->connectorA;
        constraintObj["connector_b"] = constraint->connectorB;
        constraintObj["snap_angle"] = static_cast<double>(constraint->snapAngle);
        constraintsArray.append(constraintObj);
    }

    QJsonObject assemblyObj;
    assemblyObj["components"] = componentsArray;
    assemblyObj["constraints"] = constraintsArray;
    assemblyObj["transform"] = QJsonObject{
        {"rotation", QJsonArray{rootRotation.roll(), rootRotation.pitch(), rootRotation.yaw()}}
    };

    projectData["assembly"] = assemblyObj;

    projectData["script"] = QJsonObject{
        {"current", currentScriptIdx},
        {"paths", scriptPaths}
    };

    projectData["stage"] = Stage::toJson(stage.getData());

    projectData["schema"] = kSchemaVersion;
    projectData["level"] = levelName(level);
    QJsonObject layoutObj;
    for (auto it = schematicLayout.begin(); it != schematicLayout.end(); ++it)
        if (componentMap.contains(it.key())) layoutObj[QString::number(it.key())] = QJsonArray{it.value().x(), it.value().y()};
    projectData["wiring"] = QJsonObject{{"wires", wiring.toJson()}, {"layout", layoutObj}};
    writeBoardHeader();

    if (plottingProvider) plotting = plottingProvider();
    projectData["plotting"] = plotting;

    QFile file(projectPath);
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        Log::error("Failed to open project file for saving: " + projectPath);
        return false;
    }

    QJsonDocument doc(projectData);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    Log::info("Project saved successfully to " + projectPath);
    return true;
}





void Project::saveDefaults() {
    for (ComponentInstance* comp : componentMap) {
        if (comp->getBlueprint()) {
            for (const InterfaceDef& def : comp->getBlueprint()->interfaceInputs) {
                if (def.target.kind != "joint") continue;
                QString jkey = def.target.id;

                BasicIOValue currentValueIOData = comp->getJointValue(jkey);
                if (!jkey.isEmpty() && !currentValueIOData.data.empty()) {
                    comp->parameters.insert(jkey, currentValueIOData.data[0]);
                }
            }
        }
    }
}










#pragma region read & build data

/*
 * Parses the grouped JSON schema into memory.
 * Extracts the connection rules, nested parameters, and explicit loop constraints.
 */

void Project::parseAssembly() {
    QJsonObject assemblyObj = projectData["assembly"].toObject();
    QJsonArray componentsArray = assemblyObj["components"].toArray();
    QJsonArray constraintsArray = assemblyObj["constraints"].toArray();

    // components
    for (const auto val : componentsArray) {
        QJsonObject obj = val.toObject();
        ComponentInstance* component = new ComponentInstance();

        component->uid = obj["uid"].toInt();
        component->name = obj["name"].toString();
        component->modelId = obj["model_id"].toString();
        component->firmware = obj["firmware"].toString();

        QJsonValue connVal = obj["connection"];
        if (connVal.isNull()) {
            component->parentUid = -1;
            component->parentConnector = "";
            component->selfConnector = "";
            component->snapAngle = 0.0f;
        } else {
            QJsonObject connObj = connVal.toObject();
            component->parentUid = connObj["parent_uid"].toInt();
            component->parentConnector = connObj["parent_connector"].toString();
            component->selfConnector = connObj["self_connector"].toString();
            component->snapAngle = static_cast<float>(connObj["snap_angle"].toDouble(0.0));
        }

        QJsonObject paramsObj = obj["parameters"].toObject();
        for (const QString& key : paramsObj.keys()) {
            component->parameters.insert(key, paramsObj.value(key).toVariant());
        }

        QString model_id = component->modelId;
        component->setBlueprint(LibraryManager::getInstance().getBlueprint(model_id));
        // component->initializeIO();


        QString emulatorType = component->getBlueprint()->emulatorDef.type;
        if(!emulatorType.isEmpty()){
            component->emulator = EmulatorFactory::create(emulatorType, component);
        }else if (!component->getBlueprint()->board.present) {   // a board is run by its firmware
            component->emulator = EmulatorFactory::create("default_emulator", component);
        }


        componentMap.insert(component->uid, component);

        if (component->uid >= nextComponentUid) {
            nextComponentUid = component->uid + 1;
        }
    }

    // constraints
    for (const auto val : constraintsArray) {
        QJsonObject obj = val.toObject();
        Constraint* constraint = new Constraint();

        constraint->componentAUid = obj["component_a"].toInt();
        constraint->componentBUid = obj["component_b"].toInt();
        constraint->connectorA = obj["connector_a"].toString();
        constraint->connectorB = obj["connector_b"].toString();
        constraint->snapAngle = static_cast<float>(obj["snap_angle"].toDouble(0.0));

        constraintList.append(constraint);
    }

    // root transform
    QJsonObject rootTransformObj = assemblyObj["transform"].toObject();
    if (!rootTransformObj.isEmpty()) {
        QJsonArray rotArray = rootTransformObj["rotation"].toArray();
        if (rotArray.size() == 3) {
            rootRotation = Rotation(rotArray[0].toDouble(),
                                    rotArray[1].toDouble(),
                                    rotArray[2].toDouble());
        }
    }
}







/*
 * Constructs the macro-graph tree and calculates spatial alignments.
 * Applies mating rotations to all components, including the virtual root (uid 0).
 */

void Project::buildHierarchy() {
    for (ComponentInstance* c : componentMap) {
        if (c->parentUid == 0) {
            rootComponent = c;
        } else {
            if (componentMap.contains(c->parentUid)) {
                componentMap[c->parentUid]->children.append(c);
            }
        }
    }

    applyTransforms();
}






void Project::applyTransforms() {
    if (!rootComponent) return;

    std::queue<ComponentInstance*> q;
    q.push(rootComponent);

    while (!q.empty()) {
        ComponentInstance* current = q.front();
        q.pop();

        if (current->parentUid == 0) {
            Transform pConnRelTrans(Position(0, 0, 0), rootRotation);
            double halfAngle = current->snapAngle * 0.5 * (M_PI / 180.0);
            Rotation snapRot(std::cos(halfAngle), 0, 0, std::sin(halfAngle));
            Transform alignTransform(Position(0,0,0), snapRot);
            current->transform = pConnRelTrans * alignTransform;

        } else if (componentMap.contains(current->parentUid)) {
            ComponentInstance* parent = componentMap[current->parentUid];

            if (parent->getBlueprint() && current->getBlueprint()) {
                if (!parent->getBlueprint()->connectors.contains(current->parentConnector)) {
                    Log::error(QString("Parent connector '%1' missing on UID %2").arg(current->parentConnector).arg(parent->uid));
                } else if (!current->getBlueprint()->connectors.contains(current->selfConnector)) {
                    Log::error(QString("Self connector '%1' missing on UID %2").arg(current->selfConnector).arg(current->uid));
                } else {
                    Transform pConnRelTrans = parent->getBlueprint()->getConnectorRelativeTransform(current->parentConnector);
                    Rotation mateRot(0, 1, 0, 0);
                    double halfAngle = current->snapAngle * 0.5 * (M_PI / 180.0);
                    Rotation snapRot(std::cos(halfAngle), 0, 0, std::sin(halfAngle));

                    Transform alignTransform(Position(0,0,0), mateRot * snapRot);
                    current->transform = pConnRelTrans * alignTransform;
                }
            } else {
                Log::error("Missing blueprint for parent or child relation: " + QString::number(current->uid));
            }
        }

        for (ComponentInstance* child : current->children) {
            q.push(child);
        }
    }
}







void Project::applyDefaults() {
    // apply joint params
    for (ComponentInstance* comp : componentMap) {
        if (comp->getBlueprint()) {
            for (const InterfaceDef& def : comp->getBlueprint()->interfaceInputs) {
                if (def.target.kind != "joint") continue;
                QString jkey = def.target.id;
                if (comp->parameters.contains(jkey)) {
                    double val=comp->parameters.value(jkey).toDouble();
                    BasicIOValue data = comp->getJointValue(jkey);
                    if (data.data.empty()) continue;
                    data.data[0]=val;
                    comp->setJointValue(jkey, data);
                }
            }
        }
    }
}





void Project::refresh() {
    applyTransforms();
}











/*
 * Creates a new component instance and adds it to the macro-graph.
 * If parentUid is -1, keeps it as a standalone component. Otherwise, attaches to specified parent and connector.
 */
ComponentInstance* Project::createComponentInstance(const int parentUid,
                                                    const QString& parentConnector,
                                                    const QString& modelId,
                                                    const QString& selfConnector,
                                                    const float snapAngle) {
    ComponentBlueprint* blueprint = LibraryManager::getInstance().getBlueprint(modelId);
    if (!blueprint) {
        Log::error("Model ID not found in library: " + modelId);
        return nullptr;
    }

    QString actualSelfConnector = selfConnector;

    if(selfConnector.isEmpty()){
        if(blueprint->connectors.isEmpty()){
            Log::error("Blueprint has no connectors: " + modelId);
            return nullptr;
        }
        if(parentUid!=0) actualSelfConnector = blueprint->connectors.firstKey();
    }

    ComponentInstance* newComp = new ComponentInstance();
    newComp->uid = nextComponentUid++;
    newComp->name = blueprint->meta.name + " (" + QString::number(newComp->uid) + ")";
    // newComp->type = modelId.section('_', 0, 0);
    // newComp->model = modelId.section('_', 1);
    newComp->modelId = modelId;
    newComp->selfConnector = actualSelfConnector;
    newComp->snapAngle = snapAngle;
    newComp->setBlueprint(blueprint);
    // newComp->initializeIO();

    // The first board of a Real project takes over the Prototype sketch.
    if (blueprint->board.present && level == ProjectLevel::Real && boards().isEmpty() && hasScriptAt(currentScriptIdx))
        newComp->firmware = scriptPaths[currentScriptIdx].toString();

    QString emulatorType = blueprint->emulatorDef.type;
    if(!emulatorType.isEmpty()){
        newComp->emulator = EmulatorFactory::create(emulatorType, newComp);
    }

    if(parentUid == 0){
        rootComponent = newComp;
        newComp->parentUid = 0;
        newComp->parentConnector = "root";
    }else{
        if (componentMap.contains(parentUid)) {
            ComponentInstance* parent = componentMap[parentUid];
            newComp->parentUid = parentUid;
            newComp->parentConnector = parentConnector;
            parent->children.append(newComp);

        } else {
            newComp->parentUid = -1;
        }
    }
    applyTransforms();


    componentMap.insert(newComp->uid, newComp);
    return newComp;
}



/*
 * Unlinks a component from the assembly WITHOUT deleting it.
 * Inverse of createComponentInstance(); the caller takes ownership of the
 * returned pointer. Used by AddComponentCommand for undo.
 */
ComponentInstance* Project::takeComponent(int uid) {
    ComponentInstance* comp = componentMap.take(uid);
    if (!comp) {
        Log::warning("takeComponent: no component with uid " + QString::number(uid));
        return nullptr;
    }

    if (comp->parentUid == 0) {
        if (rootComponent == comp) {
            rootComponent = nullptr;
        }
    } else if (componentMap.contains(comp->parentUid)) {
        componentMap[comp->parentUid]->children.removeAll(comp);
    }

    comp->parentUid = -1;
    comp->parentConnector = "";
    return comp;
}



/*
 * Re-links a component previously unlinked with takeComponent(). The
 * caller must restore the instance's parentUid/parentConnector first
 * (takeComponent clears them). Used by AddComponentCommand for redo.
 */
void Project::adoptComponent(ComponentInstance* comp) {
    if (!comp) return;

    componentMap.insert(comp->uid, comp);

    if (comp->parentUid == 0) {
        setRootComponent(comp);
    } else if (componentMap.contains(comp->parentUid)) {
        componentMap[comp->parentUid]->children.append(comp);
    } else {
        Log::warning("adoptComponent: parent uid " + QString::number(comp->parentUid) + " not found, component stays unlinked.");
        comp->parentUid = -1;
        comp->parentConnector = "";
    }

    applyTransforms();
}



/*
 * Unlinks a component AND all its descendants from the assembly without
 * deleting anything (caller takes ownership of the returned subtree root).
 * Intra-subtree parent/children links are kept intact, so the whole subtree
 * can be re-linked later with adoptSubtree(). Used by RemoveComponentCommand.
 */
ComponentInstance* Project::takeSubtree(int uid) {
    ComponentInstance* root = getComponentByUid(uid);
    if (!root) {
        Log::warning("takeSubtree: no component with uid " + QString::number(uid));
        return nullptr;
    }

    // Collect descendants before unlinking the root (children lists stay intact).
    QList<ComponentInstance*> descendants;
    std::function<void(ComponentInstance*)> collect = [&](ComponentInstance* comp) {
        for (ComponentInstance* child : comp->children) {
            descendants.append(child);
            collect(child);
        }
    };
    collect(root);

    takeComponent(uid); // severs the root's external link (parent or rootComponent)
    for (ComponentInstance* desc : descendants) {
        componentMap.take(desc->uid);
    }
    return root;
}


/*
 * Re-links a subtree previously unlinked with takeSubtree(). The caller must
 * restore the root's parentUid/parentConnector first. Descendants are only
 * re-inserted into componentMap -- their parent/children links were never
 * cleared, so re-appending them would duplicate entries.
 */
void Project::adoptSubtree(ComponentInstance* root) {
    if (!root) return;

    adoptComponent(root); // links the root to its external parent + transforms

    std::function<void(ComponentInstance*)> reinsert = [&](ComponentInstance* comp) {
        for (ComponentInstance* child : comp->children) {
            componentMap.insert(child->uid, child);
            reinsert(child);
        }
    };
    reinsert(root);
}











#pragma region mujoco stuff


/*
 * Compiles the entire project into a single valid MuJoCo XML string: the
 * stage (sky, lights, floor, props, <visual>/<option>) around the robot tree.
 */

static void writeTendonsXML(ComponentInstance* comp, QString& tendonsOut);

// Display texture/material/quad assets: per instance, unlike the per-model
// assets of writeAssetsXML (each display shows its own pixels).
static void writeDisplayAssetsXML(ComponentInstance* comp, QString& assetsOut) {
    if (!comp) return;
    if (ComponentBlueprint* bp = comp->getBlueprint()) assetsOut += bp->generateDisplayAssetXML(comp->uid);
    for (ComponentInstance* child : comp->children) writeDisplayAssetsXML(child, assetsOut);
}

// Largest camera resolution in the tree (0x0 without cameras).
static void maxCameraResolution(ComponentInstance* comp, int& w, int& h) {
    if (!comp) return;
    if (ComponentBlueprint* bp = comp->getBlueprint()) {
        for (const CameraDef& cam : bp->cameras) {
            w = std::max(w, cam.resolution.first);
            h = std::max(h, cam.resolution.second);
        }
    }
    for (ComponentInstance* child : comp->children) maxCameraResolution(child, w, h);
}


QString Project::generateMujocoXML(bool isSimulation) {
    QString worldbody, constraints, contacts, assets, actuators, sensors, tendons;
    QSet<QString> processedModels;
    QSet<int> visitedComps;

    // The world around the robot (sky, lights, floor, props) comes from the stage.
    QString baseAssets = stage.assetsXML();
    QString baseWorldBody = stage.worldBodyXML(isSimulation);
    actuators = stage.actuatorsXML(isSimulation);

    if (rootComponent) {
        worldbody = writeWorldBodyXML(rootComponent, visitedComps, isSimulation);
        visitedComps.clear();
        writeContactXML(rootComponent, contacts, visitedComps);
        writeAssetsXML(rootComponent, assets, processedModels);
        writeDisplayAssetsXML(rootComponent, assets);
        writeActuatorsXML(rootComponent, actuators);
        writeSensorsXML(rootComponent, sensors);
        writeTendonsXML(rootComponent, tendons);
    }

    // Cameras render into MuJoCo's offscreen buffer, which must fit the
    // largest one. MuJoCo's default (640x480) is kept as the floor.
    QString visual = stage.visualXML();
    int camW = 0, camH = 0;
    maxCameraResolution(rootComponent, camW, camH);
    if (camW > 0 && camH > 0) {
        visual += QString("    <global offwidth=\"%1\" offheight=\"%2\"/>\n")
                    .arg(std::max(camW, 640)).arg(std::max(camH, 480));
    }
    visual = "  <visual>\n" + visual + "  </visual>\n\n" + stage.optionXML();

    QString xml = QString(
        "<mujoco model=\"%1\">\n"
        "%11"
        "  <asset>\n%2%3  </asset>\n\n"
        "  <worldbody>\n%4%5  </worldbody>\n\n"
        "  <equality>\n%6  </equality>\n\n"
        "  <contact>\n%7  </contact>\n\n"
        "  <tendon>\n%10  </tendon>\n\n"
        "  <actuator>\n%8  </actuator>\n\n"
        "  <sensor>\n%9  </sensor>\n"
        "</mujoco>"
    ).arg(
        projectData["meta"].toObject()["name"].toString(),
        baseAssets, assets,
        baseWorldBody, worldbody,
        constraints, contacts, actuators, sensors
    ).arg(tendons, visual);   // %10, %11: QString::arg(a1..a9) is the most a single call takes

    Log::info("Generated internal Mujoco XML successfully");
    // Log::info("Generated Mujoco XML successfully : \n" + xml);

    return xml;
}






// Shifts the first <body pos="..."> of a component tree by the stage's robot spawn.
static void offsetRootBody(QString& xml, const Vec3& offset) {
    if (offset == Vec3{0, 0, 0}) return;
    int tagEnd = xml.indexOf(">");
    int start = xml.indexOf("pos=\"");
    if (start < 0 || start > tagEnd) return;
    start += 5;
    int end = xml.indexOf('"', start);
    QStringList parts = xml.mid(start, end - start).split(' ', Qt::SkipEmptyParts);
    if (parts.size() != 3) return;
    QString moved = QString("%1 %2 %3").arg(parts[0].toDouble() + offset[0])
                                       .arg(parts[1].toDouble() + offset[1])
                                       .arg(parts[2].toDouble() + offset[2]);
    xml.replace(start, end - start, moved);
}


QString Project::writeWorldBodyXML(ComponentInstance* comp, QSet<int>& visitedComponents, bool isSimulation) {
    if (!comp || !comp->getBlueprint()) return "";

    if (visitedComponents.contains(comp->uid)) {
        Log::error("Macro-graph cycle detected! UID " + QString::number(comp->uid) + " is looping.");
        return "";
    }
    visitedComponents.insert(comp->uid);

    int uid=comp->uid;
    // QString selfConnector = (comp->parentUid==0) ? QString() : comp->selfConnector; // should be redundant now
    QString xml = comp->getBlueprint()->generateTreeXML(comp->uid, comp->selfConnector, comp->transform);

    if (comp->parentUid == 0) {
        offsetRootBody(xml, stage.getData().robotSpawn);
        if (isSimulation) {
            int insertPos = xml.indexOf(">"); // Finds the end of the first <body ...> tag
            if (insertPos != -1) {
                xml.insert(insertPos + 1, "\n      <freejoint name=\"robot_freejoint\"/>");
            }
        }
    }

    for (ComponentInstance* child : comp->children) {
        QString childXML = writeWorldBodyXML(child, visitedComponents);

        if (comp->getBlueprint()->connectors.contains(child->parentConnector)) {
            QString targetBody = comp->getBlueprint()->connectors.value(child->parentConnector).body;
            QString injectMarker = "<!" + QString("-- INJECT_comp_%1_%2 --").arg(comp->uid).arg(targetBody) + ">";

            if (!injectMarker.isEmpty() && xml.contains(injectMarker)) {
                xml.replace(injectMarker, childXML + "\n" + injectMarker);
            }
        }
    }

    return xml;
}





void Project::writeContactXML(ComponentInstance* comp, QString& contacts, QSet<int>& visitedComponents) {
    if (!comp || !comp->getBlueprint()) return;

    if (visitedComponents.contains(comp->uid)) {
        Log::error("Macro-graph cycle detected during contact generation! UID " + QString::number(comp->uid) + " is looping.");
        return;
    }
    visitedComponents.insert(comp->uid);

    contacts += comp->getBlueprint()->generateContactsXML(comp->uid);

    for (ComponentInstance* child : comp->children) {
        writeContactXML(child, contacts, visitedComponents);
    }
}




void Project::writeAssetsXML(ComponentInstance* comp, QString& assetsOut, QSet<QString>& processedModels) {
    if (!comp->getBlueprint()) return;

    QString model_id = comp->modelId;

    if (!processedModels.contains(model_id)) {
        processedModels.insert(model_id);
        assetsOut += comp->getBlueprint()->getAssetXML();
    }

    for (ComponentInstance* child : comp->children) {
        writeAssetsXML(child, assetsOut, processedModels);
    }
}





void Project::writeActuatorsXML(ComponentInstance* comp, QString& actuatorsOut) {
    if (comp->getBlueprint()) {
        actuatorsOut += comp->getBlueprint()->generateActuatorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeActuatorsXML(child, actuatorsOut);
    }
}





// Same recursion as the writers above.
static void writeTendonsXML(ComponentInstance* comp, QString& tendonsOut) {
    if (comp->getBlueprint()) {
        tendonsOut += comp->getBlueprint()->generateTendonXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeTendonsXML(child, tendonsOut);
    }
}





void Project::writeSensorsXML(ComponentInstance* comp, QString& sensorsOut) {
    if (comp->getBlueprint()) {
        sensorsOut += comp->getBlueprint()->generateBasicSensorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeSensorsXML(child, sensorsOut);
    }
}





void Project::writeConstraintXML(ComponentInstance* compA, const QString& connA, ComponentInstance* compB, const QString& connB, QString& constraintsOut, QString& contactsOut) {
    if (!compA || !compB || !compA->getBlueprint() || !compB->getBlueprint()) return;
    if (!compA->getBlueprint()->connectors.contains(connA) || !compB->getBlueprint()->connectors.contains(connB)) return;

    QString siteA = QString("comp_%1_conn_%2").arg(compA->uid).arg(connA);
    QString siteB = QString("comp_%1_conn_%2").arg(compB->uid).arg(connB);

    constraintsOut += QString("    <weld site1=\"%1\" site2=\"%2\"/>\n")
                        .arg(siteA)
                        .arg(siteB);

    QString bodyA = compA->getBlueprint()->connectors.value(connA).body;
    QString bodyB = compB->getBlueprint()->connectors.value(connB).body;

    if (bodyA == "base") bodyA = "root";
    if (bodyB == "base") bodyB = "root";

    QString mBodyA = QString("comp_%1_%2").arg(compA->uid).arg(bodyA);
    QString mBodyB = QString("comp_%1_%2").arg(compB->uid).arg(bodyB);

    contactsOut += QString("    <exclude body1=\"%1\" body2=\"%2\" />\n").arg(mBodyA).arg(mBodyB);

    if (bodyA != "root" || bodyB != "root") {
        contactsOut += QString("    <exclude body1=\"comp_%1_root\" body2=\"comp_%2_root\" />\n").arg(compA->uid).arg(compB->uid);
    }
}
