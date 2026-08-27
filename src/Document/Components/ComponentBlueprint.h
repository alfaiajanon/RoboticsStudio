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
#include "ComponentData.h"




class ComponentBlueprint : public ComponentData {
    private:
        QString assetXML;



        void parseKinematics();
        void traverseGraph(QString& outXML, const QString& currentNodeId, const QString& parentNodeId, const Transform& relTransform, QSet<QString>& visited, const int uid) const;

    public:
        KinematicGraph kinematics;
        ComponentBlueprint(const QString& rsdefFile);

        QString getModelId() const { return modelId; }
        QString getAssetXML() const;

        void generateAssetXML();
        QString generateTreeXML(const int uid, const QString& rootConnectorId, const Transform& globalTransform) const;
        QString generateContactsXML(const int uid) const;
        QString generateActuatorXML(const int uid) const;
        QString generateSensorXML(const int uid) const;
        Transform getConnectorRelativeTransform(const QString& connId) const;
};
