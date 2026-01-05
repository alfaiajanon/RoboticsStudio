#pragma once


#include <QString>
#include <QJsonObject>
#include "Simulation/Components/Component.h"



class Constraint{
    public:
        QString type;
        int componentAUid;
        int componentBUid;
        QString componentAConnector;
        QString componentBConnector;
};


class Project{
    private:
        QString projectPath;
        QString directoryPath;

        QJsonObject projectData;

        QMap <int, ComponentInstance*> componentMap;
        QList <Constraint*> constraintList;

        ComponentInstance* rootComponent = nullptr;

        // Internal Steps
        void clear();
        void parseAssembly();
        void buildHierarchy(); 
        
        void writeComponentXML(ComponentInstance* comp, QString& assetsOut, QString& worldbodyOut, QString& constraintsOut , int indentLevel);
        
        void getAssetTransform(const QString& modelName, const QString& connectorName, 
                            QVector3D& outPos, QQuaternion& outRot);
        
    public:
        Project();
        ~Project();

        bool loadProject(const QString& path);
        void unloadProject();

        ComponentInstance* getComponentByUid(int uid);
        ComponentInstance* getRootComponent();

        QString generateMujocoXML();
};