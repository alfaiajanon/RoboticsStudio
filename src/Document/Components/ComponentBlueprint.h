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
        mutable bool unsupportedDevicesLogged = false;



        void parseKinematics();
        void traverseGraph(QString& outXML, const QString& currentNodeId, const QString& parentNodeId, const Transform& relTransform, QSet<QString>& visited, const int uid) const;
        void logUnsupportedDevices() const;

    public:
        KinematicGraph kinematics;
        ComponentBlueprint(const QString& rsdefFile);
        // Seeds from in-memory data instead of a file -- used by the
        // component editor preview, which edits a ComponentData that has
        // no on-disk .rsdef (yet). Resource paths must already be absolute.
        ComponentBlueprint(const ComponentData& data);

        QString getModelId() const { return modelId; }
        QString getAssetXML() const;

        void generateAssetXML();
        QString generateTreeXML(const int uid, const QString& rootConnectorId, const Transform& globalTransform) const;
        QString generateContactsXML(const int uid) const;
        QString generateActuatorXML(const int uid) const;
        QString generateBasicSensorXML(const int uid) const;
        QString generateTendonXML(const int uid) const;
        Transform getConnectorRelativeTransform(const QString& connId) const;
};
