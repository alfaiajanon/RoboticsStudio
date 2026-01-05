#include "LibraryManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "Core/Log.h"
#include "Simulation/Components/ComponentBlueprint.h"

LibraryManager& LibraryManager::getInstance(){
    static LibraryManager instance;
    return instance;
}

void LibraryManager::load(const QString& catalogJsonPath){
    QFile file(catalogJsonPath);
    if(!file.open(QFile::ReadOnly)){
        Log::error("Failed to open component library catalog: " + catalogJsonPath);
        return;
    }   

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject catalogObj = doc.object();
    QJsonArray componentsArray=catalogObj["Categories"].toArray();

    for(const QJsonValue& categoryVal:componentsArray){
        QJsonObject categoryObj=categoryVal.toObject();
        QJsonArray itemsPathArray=categoryObj["items"].toArray();

        for(const QJsonValue& itemPathVal:itemsPathArray){
            QString itemPath = itemPathVal.toString();
            QFile itemFile(itemPath);
            if(!itemFile.open(QFile::ReadOnly)){
                Log::error("Failed to open component rsdef file: " + itemPath);
                continue;
            }

            ComponentBlueprint* blueprint = new ComponentBlueprint(itemPath);
            QString model_id = blueprint->getModelId();
            blueprints.insert(model_id, blueprint);
            itemFile.close();
        }
    }
}   

ComponentBlueprint* LibraryManager::getBlueprint(const QString& modelId){
    return blueprints.value(modelId, nullptr);
}
