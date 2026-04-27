#include "Project.h"
#include "Core/Log.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <queue>
#include <cmath> 
#include "Core/Application.h"
#include "Simulation/Components/ComponentBlueprint.h"
#include "Simulation/Components/LibraryManager.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/ErrorSystem/EmulatorFactory.h"




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


QMap<int, ComponentInstance*>& Project::getComponentMap() {
    return componentMap;
}


QJsonObject Project::getProjectData() const {
    return projectData;
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





 
void Project::setScript(QString path){
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open script file: " + path);
        return;
    }   

    currentScriptPath = path;

    currentScript = QString::fromUtf8(file.readAll());
    file.close();
}




void Project::reloadScript(){
    QFile file(currentScriptPath);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open script file: " + currentScriptPath);
        return;
    }   

    currentScript = QString::fromUtf8(file.readAll());
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
    currentScriptPath = projectData["script"].toObject()["path"].toString();
    
    parseAssembly();
    buildHierarchy();
    applyDefaults();
    
    // setScript("/home/anon/Documents/Code Projects/Mixed Projects/RoboticsStudio/demo/demoScript.js");
    // microcontroller.compile(getScript(), getRootComponent());
    
    Application::getInstance()->saveLastProject(path);
    return true;
}



void Project::unloadProject() {
    clear();
    projectData = QJsonObject();
    projectPath = "";
    directoryPath = "";
    currentScript = "";
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
        compObj["type"] = comp->type;
        compObj["model"] = comp->model;

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

    projectData["assembly"] = assemblyObj;

    projectData["script"] = QJsonObject{
        {"path", currentScriptPath}
    };

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
        if (comp->blueprint) {
            for (const QString& key : comp->blueprint->inputDefs.keys()) {
                QString jkey = comp->blueprint->inputDefs[key].targetJoint;
                
                if (!jkey.isEmpty()) {
                    // Fetch the current state from the component's memory
                    double currentValue = comp->getJointTarget(jkey); 
                    
                    // Overwrite the old parameter with the new live value
                    comp->parameters.insert(jkey, currentValue);
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
    // load script path from script->path
    QJsonObject scriptObj = projectData["script"].toObject();
    QString scriptPath = scriptObj["path"].toString();
    setScript(scriptPath);


    QJsonObject assemblyObj = projectData["assembly"].toObject();
    QJsonArray componentsArray = assemblyObj["components"].toArray();
    QJsonArray constraintsArray = assemblyObj["constraints"].toArray();

    for (const auto val : componentsArray) {
        QJsonObject obj = val.toObject();
        ComponentInstance* component = new ComponentInstance();
        
        component->uid = obj["uid"].toInt();
        component->name = obj["name"].toString();
        component->type = obj["type"].toString();
        component->model = obj["model"].toString();
        
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

        QString model_id = component->type + "_" + component->model;
        component->blueprint = LibraryManager::getInstance().getBlueprint(model_id);
        component->initializeIO();
        
        
        QString emulatorType = component->blueprint->emulatorDef.type;
        if(!emulatorType.isEmpty()){
            component->emulator = EmulatorFactory::create(emulatorType, component);
        }else{
            component->emulator = EmulatorFactory::create("default_emulator", component);
        }
        

        componentMap.insert(component->uid, component);

        if (component->uid >= nextComponentUid) {
            nextComponentUid = component->uid + 1;
        }
    }

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
            Transform pConnRelTrans; 
            Rotation mateRot(0, 1, 0, 0); 
            double halfAngle = current->snapAngle * 0.5 * (M_PI / 180.0);
            Rotation snapRot(std::cos(halfAngle), 0, 0, std::sin(halfAngle)); 
            Transform alignTransform(Position(0,0,0), mateRot * snapRot);
            current->transform = pConnRelTrans * alignTransform; 

        } else if (componentMap.contains(current->parentUid)) {
            ComponentInstance* parent = componentMap[current->parentUid];
            
            if (parent->blueprint && current->blueprint) {
                if (!parent->blueprint->connectors.contains(current->parentConnector)) {
                    Log::error(QString("Parent connector '%1' missing on UID %2").arg(current->parentConnector).arg(parent->uid));
                } else if (!current->blueprint->connectors.contains(current->selfConnector)) {
                    Log::error(QString("Self connector '%1' missing on UID %2").arg(current->selfConnector).arg(current->uid));
                } else {
                    Transform pConnRelTrans = parent->blueprint->getConnectorRelativeTransform(current->parentConnector);
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
        if (comp->blueprint) {
            for (const QString& key : comp->blueprint->inputDefs.keys()) {
                QString jkey=comp->blueprint->inputDefs[key].targetJoint;
                if (comp->parameters.contains(jkey)) {
                    comp->setJointTarget(jkey, comp->parameters.value(jkey).toDouble());
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
        actualSelfConnector = blueprint->connectors.firstKey();
    }

    ComponentInstance* newComp = new ComponentInstance();
    newComp->uid = nextComponentUid++;
    newComp->name = blueprint->meta.name + " (" + QString::number(newComp->uid) + ")";
    newComp->type = modelId.section('_', 0, 0); 
    newComp->model = modelId.section('_', 1); 
    newComp->blueprint = blueprint;
    newComp->selfConnector = actualSelfConnector;
    newComp->snapAngle = snapAngle;
    newComp->initializeIO();

    QString emulatorType = blueprint->emulatorDef.type;
    if(!emulatorType.isEmpty()){
        newComp->emulator = EmulatorFactory::create(emulatorType, newComp);
    }

    if (componentMap.contains(parentUid)) {
        ComponentInstance* parent = componentMap[parentUid];
        newComp->parentUid = parentUid;
        newComp->parentConnector = parentConnector;
        
        Transform pConnRelTrans = parent->blueprint->getConnectorRelativeTransform(newComp->parentConnector);
        Rotation mateRot(0, 1, 0, 0); 
        double halfAngle = newComp->snapAngle * 0.5 * (M_PI / 180.0);
        Rotation snapRot(std::cos(halfAngle), 0, 0, std::sin(halfAngle)); 
        Transform alignTransform(Position(0,0,0), mateRot * snapRot);
        newComp->transform = pConnRelTrans * alignTransform; 

        parent->children.append(newComp);
        
    } else {
        newComp->parentUid = -1; 
    }

    componentMap.insert(newComp->uid, newComp);
    return newComp;
}











#pragma region mujoco stuff


/*
 * Compiles the entire project into a single valid MuJoCo XML string.
 * Injects procedural skybox, a solid color floor, and dynamically scaled coordinate axes.
 */

QString Project::generateMujocoXML() {
    QString worldbody, constraints, contacts, assets, actuators, sensors;
    QSet<QString> processedModels;
    QSet<int> visitedComps; 

    double axisScale = 0.5; 
    double halfLen = axisScale / 2.0;
    double radius = axisScale * 0.025;

    QString baseAssets = 
    "    <texture type=\"skybox\" builtin=\"gradient\" rgb1=\"0.07 0.08 0.10\" rgb2=\"0.05 0.04 0.045\" width=\"512\" height=\"512\"/>\n";
    // "";

    QString baseWorldBody = QString(
        "    <light directional=\"true\" diffuse=\"0.6 0.6 0.6\" specular=\"0.2 0.2 0.2\" pos=\"-0.5 -.5 .5\" dir=\"1 1 -1\"/>\n"
    );

    if (rootComponent) {
        worldbody = writeWorldBodyXML(rootComponent, visitedComps);
        writeAssetsXML(rootComponent, assets, processedModels);
        writeActuatorsXML(rootComponent, actuators);
        writeSensorsXML(rootComponent, sensors);
    }

    for (Constraint* constraint : constraintList) {
        ComponentInstance* compA = getComponentByUid(constraint->componentAUid);
        ComponentInstance* compB = getComponentByUid(constraint->componentBUid);
        
        if (compA && compB) {
            writeConstraintXML(compA, constraint->connectorA, compB, constraint->connectorB, constraints, contacts);
        }
    }

    QString xml = QString(
        "<mujoco model=\"%1\">\n"
        "  <asset>\n%2%3  </asset>\n\n"
        "  <worldbody>\n%4%5  </worldbody>\n\n"
        "  <equality>\n%6  </equality>\n\n"
        "  <contact>\n%7  </contact>\n\n"
        "  <actuator>\n%8  </actuator>\n\n"
        "  <sensor>\n%9  </sensor>\n"
        "</mujoco>"
    ).arg(
        projectData["meta"].toObject()["name"].toString(), 
        baseAssets,
        assets, 
        baseWorldBody,
        worldbody, 
        constraints,
        contacts,
        actuators,
        sensors
    );

    Log::info("Generated internal Mujoco XML successfully");
    // Log::info("Generated Mujoco XML successfully : \n" + xml);
    return xml;
}






QString Project::writeWorldBodyXML(ComponentInstance* comp, QSet<int>& visitedComponents) {
    if (!comp || !comp->blueprint) return "";
    
    if (visitedComponents.contains(comp->uid)) {
        Log::error("Macro-graph cycle detected! UID " + QString::number(comp->uid) + " is looping.");
        return "";
    }
    visitedComponents.insert(comp->uid);
    
    QString xml = comp->blueprint->generateTreeXML(comp->uid, comp->selfConnector, comp->transform);
    
    for (ComponentInstance* child : comp->children) {
        QString childXML = writeWorldBodyXML(child, visitedComponents);
        
        if (comp->blueprint->connectors.contains(child->parentConnector)) {
            QString targetBody = comp->blueprint->connectors.value(child->parentConnector).body;
            QString injectMarker = "<!" + QString("-- INJECT_comp_%1_%2 --").arg(comp->uid).arg(targetBody) + ">";
            
            if (!injectMarker.isEmpty() && xml.contains(injectMarker)) {
                xml.replace(injectMarker, childXML + "\n" + injectMarker);
            }
        }
    }
    
    return xml;
}







void Project::writeAssetsXML(ComponentInstance* comp, QString& assetsOut, QSet<QString>& processedModels) {
    if (!comp->blueprint) return;

    QString model_id = comp->type + "_" + comp->model;

    if (!processedModels.contains(model_id)) {
        processedModels.insert(model_id);
        assetsOut += comp->blueprint->getAssetXML();
    }
    
    for (ComponentInstance* child : comp->children) {
        writeAssetsXML(child, assetsOut, processedModels);
    }
}





void Project::writeActuatorsXML(ComponentInstance* comp, QString& actuatorsOut) {
    if (comp->blueprint) {
        actuatorsOut += comp->blueprint->generateActuatorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeActuatorsXML(child, actuatorsOut);
    }
}





void Project::writeSensorsXML(ComponentInstance* comp, QString& sensorsOut) {
    if (comp->blueprint) {
        sensorsOut += comp->blueprint->generateSensorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeSensorsXML(child, sensorsOut);
    }
}





void Project::writeConstraintXML(ComponentInstance* compA, const QString& connA, ComponentInstance* compB, const QString& connB, QString& constraintsOut, QString& contactsOut) {
    if (!compA || !compB || !compA->blueprint || !compB->blueprint) return;
    if (!compA->blueprint->connectors.contains(connA) || !compB->blueprint->connectors.contains(connB)) return;

    QString siteA = QString("comp_%1_conn_%2").arg(compA->uid).arg(connA);
    QString siteB = QString("comp_%1_conn_%2").arg(compB->uid).arg(connB);

    constraintsOut += QString("    <weld site1=\"%1\" site2=\"%2\"/>\n")
                        .arg(siteA)
                        .arg(siteB);

    QString bodyA = compA->blueprint->connectors.value(connA).body;
    QString bodyB = compB->blueprint->connectors.value(connB).body;
    
    if (bodyA == "base") bodyA = "root";
    if (bodyB == "base") bodyB = "root";

    QString mBodyA = QString("comp_%1_%2").arg(compA->uid).arg(bodyA);
    QString mBodyB = QString("comp_%1_%2").arg(compB->uid).arg(bodyB);

    contactsOut += QString("    <exclude body1=\"%1\" body2=\"%2\" />\n").arg(mBodyA).arg(mBodyB);
    
    if (bodyA != "root" || bodyB != "root") {
        contactsOut += QString("    <exclude body1=\"comp_%1_root\" body2=\"comp_%2_root\" />\n").arg(compA->uid).arg(compB->uid);
    }
}