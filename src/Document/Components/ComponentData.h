#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>
#include "Utils/Graph.h"




struct MechanicDef {
    QList<float> snapAngles;
};

struct MetaDef {
    // QString id;
    QString name;
    QString version;
    QString author;
    QString iconPath;
};

struct PinDef {
    QString id;
    QString description;
    QPair<float, float> voltageRange;
};

struct ConnectorDef {
    QString id;
    QString body;
    QString description;
    Transform transform;
    MechanicDef mechanics;
};

struct IODef {
    QString name;
    QString unit;
    QString dataType;
    QString channelType;
    bool physical = true;

    QPair<float, float> range;
    QString targetJoint;
    QString targetSite;

    int dim = 1;
    QList<QString> componentLabels;
    QString cameraName;
    QPair<int, int> resolution;

    QList<QString> pinsRequired;
};

struct EmulatorDef {
    QString type;
    QJsonObject source;
    QMap<QString, QVariant> parameters;
};





struct ComponentData {
    QString basePath;

    QString modelId;
    MetaDef meta;
    QJsonObject specs;
    QJsonObject pins;

    QMap<QString, QString> meshResources;
    QMap<QString, QString> materialResources;

    QMap<QString, Node> bodies;
    QList<Edge> joints;
    QString defaultBodyId;

    QMap<QString, ConnectorDef> connectors;
    QMap<QString, IODef> inputDefs;
    QMap<QString, IODef> outputDefs;
    EmulatorDef emulatorDef;

    QJsonObject toJson() const;
    static ComponentData fromJson(const QJsonObject& obj, const QString& basePath);
};
