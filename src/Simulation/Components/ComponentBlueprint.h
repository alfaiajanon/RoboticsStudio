#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include "Utils/Spatial.h"
#include "Utils/Graph.h"




struct MechanicDef {
    QList<float> snapAngles; 
};




struct MetaDef {
    QString id;
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
    QPair<float, float> range;  
    
    QString targetJoint;   
    QString actuatorType;
    QString sensorType;
    
    double kp = 0.0;
    double kv = 0.0;
    QList<double> forceRange;
    
    QList<QString> pinsRequired;
};




struct EmulatorDef {
    QString type;
    QMap<QString, QVariant> parameters;
};




class ComponentBlueprint {
private:
    QString modelId;
    QString basePath;
    QString assetXML;
    
    KinematicGraph kinematics;

    void parseResources(const QJsonObject& resourcesObj);
    void parseKinematics(const QJsonObject& kinematicsObj);
    void parseConnectors(const QJsonArray& connectorsArr);
    void parseIO(const QJsonObject& ioObj);

    void traverseGraph(QString& outXML, const QString& currentNodeId, const QString& parentNodeId, const Transform& relTransform, QSet<QString>& visited, const int uid) const;

public:
    ComponentBlueprint(const QString& rsdefFile);

    MetaDef meta;
    EmulatorDef emulator;

    QMap<QString, PinDef> pins; 
    QMap<QString, ConnectorDef> connectors;
    QMap<QString, IODef> inputDefs;
    QMap<QString, IODef> outputDefs;
    QMap<QString, QVariant> specs;

    QString getModelId() const { return modelId; }
    QString getAssetXML() const;

    QString generateTreeXML(const int uid, const QString& rootConnectorId, const Transform& globalTransform) const;
    QString generateActuatorXML(const int uid) const;
    QString generateSensorXML(const int uid) const;
    Transform getConnectorRelativeTransform(const QString& connId) const;
};