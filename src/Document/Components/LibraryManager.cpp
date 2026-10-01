#include "LibraryManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <qcontainerfwd.h>
#include <qdir.h>
#include <qlist.h>
#include <qobject.h>
#include <qstandardpaths.h>
#include "Application/Application.h"
#include "Document/Project.h"
#include "Utils/Log.h"
#include "Document/Components/ComponentBlueprint.h"



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




bool LibraryManager::saveLocalComponent(ComponentData data) {
    if (data.modelId.trimmed().isEmpty()) {
        Log::error("Cannot save component: id is empty.");
        return false;
    }

    const bool overwrite = hasBlueprint(data.modelId);
    if (overwrite) {
        Log::warning("Overwriting existing component: id '" + data.modelId + "'");
    }

    QString projDir = Application::getInstance()->getProject()->getProjectDirectory();
    data.basePath = projDir + "/Components/" + data.modelId;

    QDir().mkpath(data.basePath);
    QString rsdefPath = QDir(data.basePath).filePath(data.modelId + ".rsdef");

    // Trailing slash matters: a bare startsWith(basePath) would also match a
    // sibling component like "servo_sg90" when saving "servo".
    const QString ownDir = data.basePath + "/";

    for (const QString& key : data.meshResources.keys()) {
        if (data.meshResources[key].startsWith(ownDir)) continue;   // already inside this component
        QString relPath = copyResourceFile(data.meshResources[key], data.basePath, "meshes");
        if (!relPath.isEmpty())
            data.meshResources[key] = QDir(data.basePath).filePath(relPath);
    }
    for (const QString& key : data.materialResources.keys()) {
        if (data.materialResources[key].startsWith(ownDir)) continue;
        QString relPath = copyResourceFile(data.materialResources[key], data.basePath, "textures");
        if (!relPath.isEmpty())
            data.materialResources[key] = QDir(data.basePath).filePath(relPath);
    }

    // QSaveFile writes to a temp file and renames on commit(). With
    // QFile + Truncate the existing definition is destroyed the moment the
    // file opens, so a failed write (disk full, crash) would leave an empty
    // .rsdef that breaks the next project load.
    QSaveFile outFile(rsdefPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        Log::error("Failed to write component file: " + rsdefPath);
        return false;
    }
    outFile.write(QJsonDocument(data.toJson()).toJson(QJsonDocument::Indented));
    if (!outFile.commit()) {
        Log::error("Failed to finalize component file: " + rsdefPath);
        return false;
    }

    // Overwrite: disk only. The registered blueprint, category list and every
    // placed instance are left untouched, so the new definition takes effect
    // when the project is reloaded.
    if (overwrite) {
        Log::info("Component '" + data.modelId + "' saved to disk. Reload the project to apply the changes.");
        emit componentSaved(data.modelId);
        return true;
    }

    // ---- new component only: register it ----
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





ComponentBlueprint* LibraryManager::getBlueprint(const QString modelId) {
    return blueprints.value(modelId, nullptr);
}



bool LibraryManager::hasBlueprint(const QString& model_id){
    return blueprints.contains(model_id);
}


QList<ComponentBlueprint*> LibraryManager::getBlueprints(QString categoryId){
    // int idx = categories.indexOf(categoryId);
    CategoryDef catDef;
    for(CategoryDef def:categories){
        if(def.id==categoryId){
            catDef=def;
            break;
        }
    }

    QStringList list=catDef.modelIds;
    QList<ComponentBlueprint*> result;
    for(QString id:list){
        result.append(blueprints.value(id));
    }
    return result;
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
            if (!blueprint->isValid) {
                Log::error("Skipping component " + absoluteItemPath + ": " + blueprint->errorString);
                delete blueprint;
                continue;
            }

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
                ComponentBlueprint* blueprint = new ComponentBlueprint(rsdefPath);
                if (!blueprint->isValid) {
                    Log::error("Skipping local component " + rsdefPath + ": " + blueprint->errorString);
                    delete blueprint;
                    continue;
                }
                QString model_id = blueprint->getModelId();

                // `categories` was reset by load() for this project open, so it only lists the
                // catalog and the local components registered so far -- `blueprints` itself
                // still holds entries from earlier opens and cannot be used for this check.
                bool duplicate = false;
                for (const CategoryDef& cat : categories) {
                    if (cat.modelIds.contains(model_id)) { duplicate = true; break; }
                }
                if (category.modelIds.contains(model_id)) duplicate = true;
                if (duplicate) {
                    Log::error("Skipping local component " + rsdefPath + ": model id '" + model_id + "' is already registered");
                    delete blueprint;
                    continue;
                }

                blueprints.insert(model_id, blueprint);
                category.modelIds.append(model_id);
            }
        }

        categories.append(category);
    }

    emit catalogLoaded();
    return true;
}
