#include "LibraryManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <qdir.h>
#include <qstandardpaths.h>
#include "Application/Application.h"
#include "Document/Project.h"
#include "Utils/Log.h"
#include "Document/Components/ComponentBlueprint.h"




const QString LibraryManager::CUSTOM_CATEGORY_ID   = "custom";
const QString LibraryManager::CUSTOM_CATEGORY_NAME = "Custom Components";


static QString computeCustomBasePath(const QString& dirPath, const QString& id) {
    QString customDir = dirPath + "/custom";
    return QDir(customDir).filePath(id);
}

QString LibraryManager::copyResourceFile(const QString& srcAbsPath, const QString& destDir, const QString& subfolder) {
    if (srcAbsPath.isEmpty()) return QString();

    QDir dir(destDir);
    dir.mkpath(subfolder);

    QString baseName = QFileInfo(srcAbsPath).fileName();
    QString destAbsPath = dir.filePath(subfolder + "/" + baseName);

    if (QFile::exists(destAbsPath)) {
        QFile::remove(destAbsPath); // overwrite -- known limitation: silently
                                     // replaces a same-named file even if it
                                     // came from a different source.
    }
    QFile::copy(srcAbsPath, destAbsPath);

    return subfolder + "/" + baseName;
}




// JSON serialization for a component now lives entirely in
// ComponentData::toJson() -- nothing in this file duplicates that mapping
// anymore.

// void LibraryManager::appendCustomCatalogEntry(const QString& relativeItemPath, const QString& modelId) {
//     QString path = dir+"/Catalog.json";
//     QFile file(path);
//     if (!file.open(QFile::ReadWrite)) {
//         Log::error("Failed to open Catalog.json for update: " + path);
//         return;
//     }

//     QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
//     QJsonObject catalogObj = doc.object();
//     QJsonArray categoriesArr = catalogObj["Categories"].toArray();

//     int customIdx = -1;
//     for (int i = 0; i < categoriesArr.size(); ++i) {
//         if (categoriesArr[i].toObject()["id"].toString() == CUSTOM_CATEGORY_ID) {
//             customIdx = i;
//             break;
//         }
//     }

//     QJsonObject customCategoryObj;
//     if (customIdx == -1) {
//         customCategoryObj["id"] = CUSTOM_CATEGORY_ID;
//         customCategoryObj["name"] = CUSTOM_CATEGORY_NAME;
//         customCategoryObj["keys"] = QJsonArray{CUSTOM_CATEGORY_ID};
//         customCategoryObj["items"] = QJsonArray{relativeItemPath};
//         categoriesArr.append(customCategoryObj);

//         CategoryDef newCat;
//         newCat.id = CUSTOM_CATEGORY_ID;
//         newCat.name = CUSTOM_CATEGORY_NAME;
//         newCat.keys = {CUSTOM_CATEGORY_ID};
//         newCat.modelIds.append(modelId);
//         categories.append(newCat);
//     } else {
//         customCategoryObj = categoriesArr[customIdx].toObject();
//         QJsonArray itemsArr = customCategoryObj["items"].toArray();
//         if (!itemsArr.toVariantList().contains(relativeItemPath)) {
//             itemsArr.append(relativeItemPath);
//         }
//         customCategoryObj["items"] = itemsArr;
//         categoriesArr[customIdx] = customCategoryObj;

//         for (CategoryDef& cat : categories) {
//             if (cat.id == CUSTOM_CATEGORY_ID) {
//                 if (!cat.modelIds.contains(modelId)) cat.modelIds.append(modelId);
//                 break;
//             }
//         }
//     }

//     catalogObj["Categories"] = categoriesArr;

//     file.resize(0);
//     file.write(QJsonDocument(catalogObj).toJson(QJsonDocument::Indented));
//     file.close();
// }




// bool LibraryManager::saveComponent(ComponentData data, bool isNewCatalogEntry) {
//     if (data.modelId.trimmed().isEmpty()) {
//         Log::error("Cannot save component: id is empty.");
//         return false;
//     }

//     if (isNewCatalogEntry && blueprints.contains(data.modelId)) {
//         Log::error("Cannot save component: id '" + data.modelId + "' is already in use.");
//         return false;
//     }

//     if (isNewCatalogEntry) {
//         data.basePath = computeCustomBasePath(dir, data.modelId);
//     }
//     if (data.basePath.isEmpty()) {
//         Log::error("Cannot save component: basePath is not set.");
//         return false;
//     }

//     QDir().mkpath(data.basePath);
//     QString rsdefPath = QDir(data.basePath).filePath(data.modelId + ".rsdef");

//     for (const QString& key : data.meshResources.keys()) {
//         QString relPath = copyResourceFile(data.meshResources[key], data.basePath, "meshes");
//         if (!relPath.isEmpty()) data.meshResources[key] = QDir(data.basePath).filePath(relPath);
//     }
//     for (const QString& key : data.materialResources.keys()) {
//         QString relPath = copyResourceFile(data.materialResources[key], data.basePath, "textures");
//         if (!relPath.isEmpty()) data.materialResources[key] = QDir(data.basePath).filePath(relPath);
//     }

//     QFile outFile(rsdefPath);
//     if (!outFile.open(QFile::WriteOnly | QFile::Truncate)) {
//         Log::error("Failed to write component file: " + rsdefPath);
//         return false;
//     }
//     outFile.write(QJsonDocument(data.toJson()).toJson(QJsonDocument::Indented));
//     outFile.close();

//     if (isNewCatalogEntry) {
//         QString baseModelsDir = dir;
//         QString relativeItemPath = QDir(baseModelsDir).relativeFilePath(rsdefPath);
//         appendCustomCatalogEntry(relativeItemPath, data.modelId);
//     }

//     ComponentBlueprint* freshBlueprint = new ComponentBlueprint(rsdefPath);
//     blueprints.insert(data.modelId, freshBlueprint);

//     emit componentSaved(data.modelId);
//     return true;
// }


bool LibraryManager::saveLocalComponent(ComponentData data) {
    if (data.modelId.trimmed().isEmpty()) {
        Log::error("Cannot save component: id is empty.");
        return false;
    }
    if (hasBlueprint(data.modelId)) {
        Log::error("Cannot save component: id '" + data.modelId + "' is already in use.");
        return false;
    }

    QString projDir = Application::getInstance()->getProject()->getProjectDirectory();
    data.basePath = projDir+"/Components/"+data.modelId;

    QDir().mkpath(data.basePath);
    QString rsdefPath = QDir(data.basePath).filePath(data.modelId + ".rsdef");

    for (const QString& key : data.meshResources.keys()) {
        // skip if same path
        if(data.meshResources[key].startsWith(data.basePath))
            continue;
        QString relPath = copyResourceFile(data.meshResources[key], data.basePath, "meshes");
        if (!relPath.isEmpty())
            data.meshResources[key] = QDir(data.basePath).filePath(relPath);
    }
    for (const QString& key : data.materialResources.keys()) {
        if(data.materialResources[key].startsWith(data.basePath))
            continue;
        QString relPath = copyResourceFile(data.materialResources[key], data.basePath, "textures");
        if (!relPath.isEmpty())
            data.materialResources[key] = QDir(data.basePath).filePath(relPath);
    }

    QFile outFile(rsdefPath);
    if (!outFile.open(QFile::WriteOnly | QFile::Truncate)) {
        Log::error("Failed to write component file: " + rsdefPath);
        return false;
    }
    outFile.write(QJsonDocument(data.toJson()).toJson(QJsonDocument::Indented));
    outFile.close();


    // add to Library
    ComponentBlueprint* freshBlueprint = new ComponentBlueprint(rsdefPath);
    blueprints.insert(data.modelId, freshBlueprint);


    auto it = std::find_if(categories.begin(), categories.end(), [](const CategoryDef& cat) {
        return cat.id == "custom";
    });
    if (it != categories.end()) {
        it->modelIds.append(data.modelId);
    } else {
        CategoryDef category;
        category.id = "custom";
        category.name = "Local Components";
        category.modelIds.append(data.modelId);
        categories.append(category);
    }

    emit componentSaved(data.modelId);
    emit catalogLoaded();
    return true;
}








LibraryManager& LibraryManager::getInstance() {
    static LibraryManager instance;
    return instance;
}




QString LibraryManager::getModelsDir(){
    if(dir!="") {
        return dir;
    }else{
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir directory(dataPath);
        if(directory.exists("models")){
            return directory.absoluteFilePath("models");
        }
        directory.mkpath(directory.absoluteFilePath("models"));
        return directory.absoluteFilePath("models");
    }
}





ComponentBlueprint* LibraryManager::getBlueprint(const QString& modelId) {
    return blueprints.value(modelId, nullptr);
}



bool LibraryManager::hasBlueprint(const QString& model_id){
    return blueprints.contains(model_id);
}




// void LibraryManager::fetchOnline() {

// }





/*
 * Parses the catalog JSON, populates categorical data, and loads all component blueprints.
 * Dynamically resolves relative component paths based on the catalog's base directory.
 */
bool LibraryManager::load(const QString& models_dir) {
    if(models_dir == ""){
        dir = getModelsDir();
    }else{
        dir = models_dir;
    }
    Log::info(dir);
    if(!QDir(dir).exists()){
        Log::error("Failed to load component library: " + dir);
        return false;
    }

    QString catalogJsonPath = dir + "/Catalog.json";
    QFile file(catalogJsonPath);
    if (!file.open(QFile::ReadOnly)) {
        Log::error("Failed to open component library catalog: " + catalogJsonPath);
        return false;
    }

    Log::info("Loading component library: " + dir);
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
            QString relativeItemPath = itemPathVal.toString();
            QString absoluteItemPath = QDir(dir).absoluteFilePath(relativeItemPath);

            QFile itemFile(absoluteItemPath);
            if (!itemFile.open(QFile::ReadOnly)) {
                Log::error("Failed to open component rsdef file: " + absoluteItemPath);
                continue;
            }
            itemFile.close();

            ComponentBlueprint* blueprint = new ComponentBlueprint(absoluteItemPath);

            // Fixed in ComponentData now
            // if (!blueprint->meta.iconPath.isEmpty()) {
            //     QString rsdefDir = QFileInfo(absoluteItemPath).absolutePath();
            //     blueprint->meta.iconPath = QFileInfo(rsdefDir + "/" + blueprint->meta.iconPath).absoluteFilePath();
            // }

            QString model_id = blueprint->getModelId();

            blueprints.insert(model_id, blueprint);
            category.modelIds.append(model_id);
        }

        categories.append(category);
    }

    emit catalogLoaded();
    return true;
}





bool LibraryManager::loadLocalComponents(QString project_dir){
    QString projectDir = project_dir;
    QString componentsDir = projectDir + "/Components";
    QDir dir(componentsDir);

    if(dir.exists()){
        QStringList componentFolders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        CategoryDef category;
        category.id = "custom";
        category.name = "Local Components";

        for(QString componentFolder : componentFolders){
            QString componentPath = componentsDir + "/" + componentFolder;
            QString rsdefPath = componentPath + "/" + componentFolder + ".rsdef";
            if(QFile::exists(rsdefPath)){
                // [TODO] : check for duplicate model id

                ComponentBlueprint* blueprint = new ComponentBlueprint(rsdefPath);
                QString model_id = blueprint->getModelId();

                blueprints.insert(model_id, blueprint);
                category.modelIds.append(model_id);
            }
        }

        categories.append(category);
    }

    emit catalogLoaded();
    return true;
}
