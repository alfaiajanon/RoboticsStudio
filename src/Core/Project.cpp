#include "Project.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include "Core/Log.h"
#include "Simulation/Components/ComponentBlueprint.h"
#include "Simulation/Components/LibraryManager.h"
#include "Simulation/MujocoContext.h"

Project::Project():rootComponent(nullptr) {
}

Project::~Project() {
    clear();
}

void Project::clear(){
    qDeleteAll(componentMap);
    componentMap.clear();
    rootComponent = nullptr;
    constraintList.clear();
}

bool Project::loadProject(const QString& path) {
    clear();
    projectPath = path;

    QFile file(path);
    if(!file.open(QFile::ReadOnly)){
        Log::error("Failed to open project file: " + path);
        return false;
    }   
    Log::info("project file found");

    projectData =QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QFileInfo fileInfo(path);
    directoryPath = fileInfo.absolutePath();

    parseAssembly();
    buildHierarchy();

    MujocoContext *mjctx = MujocoContext::getInstance(); 
    mjctx->loadModelFromString(generateMujocoXML().toStdString());

    

    return true;
}



void Project::parseAssembly(){
    QJsonObject assemblyObj = projectData["assembly"].toObject();
    QJsonArray componentsArray=assemblyObj["components"].toArray();
    QJsonArray constraintsArray=assemblyObj["constraints"].toArray();

    // Parsing components
    for(const auto val:componentsArray){

        QJsonObject obj=val.toObject();

        ComponentInstance* component=new ComponentInstance();
        component->uid=obj["uid"].toInt();
        component->name=obj["name"].toString();
        component->type=obj["type"].toString();
        component->model=obj["model"].toString();

        component->parentConnector=obj["parent_connector"].toString();
        component->selfConnector=obj["self_connector"].toString();

        if(obj["parent"].isNull()){
            component->parentUid=-1;
        }else{
            component->parentUid=obj["parent"].toInt();
        }

        componentMap.insert(component->uid, component);
    }

    // Now,parsing Constraints
    for(const auto val:constraintsArray){
        QJsonObject obj=val.toObject();

        Constraint* constraint=new Constraint();
        constraint->type=obj["type"].toString();
        constraint->componentAUid=obj["component_a"].toInt();
        constraint->componentBUid=obj["component_b"].toInt();
        constraint->componentAConnector=obj["component_a_connector"].toString();
        constraint->componentBConnector=obj["component_b_connector"].toString();

        constraintList.append(constraint);
    }
}



void Project::buildHierarchy(){
    for(ComponentInstance* c:componentMap){
        if(c->parentUid == -1){
            rootComponent=c;
            Log::info("Found root, uid: "+ QString::number(c->uid));
        }else{
            ComponentInstance *parent=componentMap[c->parentUid];
            parent->children.append(c);
        }
    }
}


ComponentInstance* Project::getComponentByUid(int uid){
    return componentMap.value(uid, nullptr);
}

ComponentInstance* Project::getRootComponent(){
    return rootComponent;
}

QString Project::generateMujocoXML(){
    QString xml;
    xml+= "<mujoco model=\""+projectData["meta"].toObject()["name"].toString()+"\">\n";
    
    QString worldbody, constraints, assets;
    if(rootComponent){
        writeComponentXML(rootComponent,assets, worldbody, constraints, 2);
    }


    xml+= "  <asset>\n";
    xml+= assets;
    xml += "  </asset>\n";



    xml+= "  <worldbody>\n";
    xml+= worldbody;
    xml+= "  </worldbody>\n";



    xml+= "  <equality>\n";
    xml+= constraints;
    // TODO: other constraints
    xml+= "  </equality>\n";




    xml+= "</mujoco>";
    return xml;
}


void Project::writeComponentXML(ComponentInstance* comp, QString& assetsOut, QString& worldbodyOut, QString& constraintsOut , int indentLevel){
    QString type=comp->type;
    QString model=comp->model;
    QString model_id = type + "_" + model;

    ComponentBlueprint *blueprint = LibraryManager::getInstance().getBlueprint(model_id);
    if(!blueprint){
        Log::error("Unknown component type/model: " + type + "/" + model);
        return;
    }
    QString a=blueprint->getAssetXML();
    QString b=blueprint->getBodyXML();

    // QString c=blueprint->getConstraintXML();
    assetsOut+=a;
    worldbodyOut+=b;
    // constraintsOut+=c;

    return;

    for(ComponentInstance* child:comp->children){
        writeComponentXML(child, assetsOut, worldbodyOut, constraintsOut, indentLevel);
    }
}