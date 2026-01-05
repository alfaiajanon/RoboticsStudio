#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>

struct PinDef{
    // ...
};

struct MetaDef{
    // ...
};

class ComponentBlueprint{
    QString modelId;

    QString basePath;
    QString assetXML;
    QString bodyXML;  

    QList<PinDef> pins;
    QMap<QString, QVariant> specs;


    // helper func
    void parseMujocoXML(const QString& xmlContent);

    public:
        ComponentBlueprint(const QString& rsdefFile);

        QString getModelId() const { return modelId; }
        QString getAssetXML() const;
        QString getBodyXML() const;
};