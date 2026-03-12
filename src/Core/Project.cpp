#include "Project.h"
#include "Core/Log.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <queue>
#include <cmath> // Required for sin/cos in quaternion snap rotation
#include "Simulation/Components/ComponentBlueprint.h"
#include "Simulation/Components/LibraryManager.h"




Project::Project() : rootComponent(nullptr) {}




Project::~Project() {
    clear();
}




void Project::clear() {
    qDeleteAll(componentMap);
    componentMap.clear();
    
    qDeleteAll(constraintList);
    constraintList.clear();
    
    rootComponent = nullptr;
}




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

    parseAssembly();
    buildHierarchy();

    return true;
}




/*
 * Parses the grouped JSON schema into memory.
 * Extracts the connection rules, nested parameters, and explicit loop constraints.
 */
void Project::parseAssembly() {
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

        componentMap.insert(component->uid, component);
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
 * Prepares the components for the recursive MuJoCo generation pass.
 */
void Project::buildHierarchy() {
    for (ComponentInstance* c : componentMap) {
        if (c->parentUid == -1) {
            rootComponent = c;
        } else {
            if (componentMap.contains(c->parentUid)) {
                componentMap[c->parentUid]->children.append(c);
            }
        }
    }

    if (!rootComponent) return;

    rootComponent->transform = Transform(); // Identity transform for root

    std::queue<ComponentInstance*> q;
    q.push(rootComponent);

    while (!q.empty()) {
        ComponentInstance* current = q.front();
        q.pop();

        if (current->parentUid != -1 && componentMap.contains(current->parentUid)) {
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




ComponentInstance* Project::getComponentByUid(int uid) {
    return componentMap.value(uid, nullptr);
}




ComponentInstance* Project::getRootComponent() {
    return rootComponent;
}



/*
 * Compiles the entire project into a single valid MuJoCo XML string.
 * Calls the blueprint algorithms to dynamically generate nested physics blocks.
 */
QString Project::generateMujocoXML() {
    QString worldbody, constraints, contacts, assets, actuators, sensors;
    QSet<QString> processedModels;
    QSet<int> visitedComps; 

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
        "  <asset>\n%2  </asset>\n\n"
        "  <worldbody>\n%3  </worldbody>\n\n"
        "  <equality>\n%4  </equality>\n\n"
        "  <contact>\n%5  </contact>\n\n"
        "  <actuator>\n%6  </actuator>\n\n"
        "  <sensor>\n%7  </sensor>\n"
        "</mujoco>"
    ).arg(
        projectData["meta"].toObject()["name"].toString(), 
        assets, 
        worldbody, 
        constraints,
        contacts,
        actuators,
        sensors
    );

    Log::info("Generated Mujoco XML successfully : \n" + xml);
    return xml;
}




// Update your generateMujocoXML() call to initialize the set:
// QSet<int> visitedComps;
// worldbody = writeWorldBodyXML(rootComponent, visitedComps);

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




/*
 * Recursively asks the blueprint to generate data-driven actuator blocks.
 */
void Project::writeActuatorsXML(ComponentInstance* comp, QString& actuatorsOut) {
    if (comp->blueprint) {
        actuatorsOut += comp->blueprint->generateActuatorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeActuatorsXML(child, actuatorsOut);
    }
}




/*
 * Recursively asks the blueprint to generate data-driven sensor blocks.
 */
void Project::writeSensorsXML(ComponentInstance* comp, QString& sensorsOut) {
    if (comp->blueprint) {
        sensorsOut += comp->blueprint->generateSensorXML(comp->uid);
    }

    for (ComponentInstance* child : comp->children) {
        writeSensorsXML(child, sensorsOut);
    }
}




/*
 * Generates <equality> tags ONLY for explicit closed-loop constraints.
 * Retains collision exclusion tags to prevent overlapping mesh explosions.
 */
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