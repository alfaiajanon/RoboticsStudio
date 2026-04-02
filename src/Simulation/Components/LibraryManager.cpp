#include "LibraryManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include "Core/Log.h"
#include "Simulation/Components/ComponentBlueprint.h"




LibraryManager& LibraryManager::getInstance() {
    static LibraryManager instance;
    return instance;
}





ComponentBlueprint* LibraryManager::getBlueprint(const QString& modelId) {
    return blueprints.value(modelId, nullptr);
}





/*
 * Parses the catalog JSON, populates categorical data, and loads all component blueprints.
 * Emits a signal once the entire library is successfully initialized.
 */

void LibraryManager::load(const QString& catalogJsonPath) {
    path = QFileInfo(catalogJsonPath).absoluteFilePath();
    
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open component library catalog: " + catalogJsonPath);
        return;
    }   

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject catalogObj = doc.object();
    QJsonArray componentsArray = catalogObj["Categories"].toArray();

    categories.clear();

    for (const QJsonValue& categoryVal : componentsArray) {
        QJsonObject categoryObj = categoryVal.toObject();
        
        CategoryDef category;
        category.id = categoryObj["id"].toString();
        category.name = categoryObj["name"].toString();
        
        for (const QJsonValue& keyVal : categoryObj["keys"].toArray()) {
            category.keys.append(keyVal.toString().toLower());
        }

        QJsonArray itemsPathArray = categoryObj["items"].toArray();

        for (const QJsonValue& itemPathVal : itemsPathArray) {
            QString itemPath = itemPathVal.toString();
            
            QFile itemFile(itemPath);
            if (!itemFile.open(QFile::ReadOnly)) {
                Log::error("Failed to open component rsdef file: " + itemPath);
                continue;
            }
            itemFile.close();

            ComponentBlueprint* blueprint = new ComponentBlueprint(itemPath);
            
            if (!blueprint->meta.iconPath.isEmpty()) {
                QString rsdefDir = QFileInfo(itemPath).absolutePath();
                blueprint->meta.iconPath = QFileInfo(rsdefDir + "/" + blueprint->meta.iconPath).absoluteFilePath();
            }

            QString model_id = blueprint->getModelId();
            
            blueprints.insert(model_id, blueprint);
            category.modelIds.append(model_id);
        }
        
        categories.append(category);
    }

    emit catalogLoaded(path);
}   