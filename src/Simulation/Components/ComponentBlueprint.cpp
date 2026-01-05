#include "ComponentBlueprint.h"
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QDomDocument>
#include "Core/Log.h"

ComponentBlueprint::ComponentBlueprint(const QString& rsdefFile) {
    QFile file(rsdefFile);
    if(!file.open(QIODevice::ReadOnly)){
        Log::error("ComponentBlueprint: Unable to open rsdef file: " + rsdefFile);
        return;
    }

    QJsonObject mainJson=QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QFileInfo rsdefFileInfo(rsdefFile);
    basePath = rsdefFileInfo.absoluteDir().absolutePath();

    modelId = mainJson["meta"].toObject()["id"].toString();
    
    QString relativexmlpath=mainJson["resources"].toObject()["mujoco_model_path"].toString();
    QString absoluteXmlPath=rsdefFileInfo.absoluteDir().absoluteFilePath(relativexmlpath);
    
    QFile xmlFile(absoluteXmlPath);
    if(!xmlFile.open(QIODevice::ReadOnly)){
        Log::error("ComponentBlueprint: Unable to open mujoco xml file: " + absoluteXmlPath);
        return;
    }
    QString xmlContent=xmlFile.readAll();
    xmlFile.close();
    parseMujocoXML(xmlContent);
    
    Log::info("ComponentBlueprint: Loaded model ID: " + modelId);
}






void ComponentBlueprint::parseMujocoXML(const QString& xmlContent) {
    QDomDocument doc;
    if(!doc.setContent(xmlContent)){
        Log::error("ComponentBlueprint: Failed to parse mujoco xml content.");
        return;
    }

    QDomElement root = doc.documentElement();

    QDomElement assetNode = root.firstChildElement("asset");
    if (!assetNode.isNull()) {
        QDomNode child = assetNode.firstChild();
        while (!child.isNull()) {
            QDomElement e = child.toElement();
            
            if (!e.isNull() && e.hasAttribute("file")) {
                QString originalPath = e.attribute("file");
                
                QFileInfo pathCheck(originalPath);
                if (pathCheck.isRelative()) {
                    QString absPath = basePath + "/" + originalPath;
                    e.setAttribute("file", absPath);
                }
            }
            
            QString nodeStr;
            QTextStream stream(&nodeStr);
            child.save(stream, 4);
            this->assetXML.append(nodeStr + "\n");

            child = child.nextSibling();
        }
    }


    QDomElement worldNode = root.firstChildElement("worldbody");
    if (!worldNode.isNull()) {
        QDomElement bodyNode = worldNode.firstChildElement("body");
        if (!bodyNode.isNull()) {
            QTextStream stream(&this->bodyXML);
            bodyNode.save(stream, 2);
        }
    }
}





QString ComponentBlueprint::getAssetXML() const {
    return assetXML;
}

QString ComponentBlueprint::getBodyXML() const {
    return bodyXML;
}