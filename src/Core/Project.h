#pragma once

#include <QSet>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include "Simulation/Components/ComponentInstance.h"




/*
 * Manages the active workspace and holds the loaded robot assembly.
 * Handles parsing the project JSON, building the component memory tree,
 * and compiling the final MuJoCo XML string for the physics engine.
 */
class Project {
    private:
        QString projectPath;
        QString directoryPath;
        QJsonObject projectData;
        
        ComponentInstance* rootComponent = nullptr;
        QMap<int, ComponentInstance*> componentMap;
        QList<Constraint*> constraintList;

        void clear();
        void parseAssembly();
        void buildHierarchy(); 

        QString writeWorldBodyXML(ComponentInstance* comp, QSet<int>& visitedComponents);
        void writeAssetsXML(ComponentInstance* comp, QString& assetsOut, QSet<QString>& processedModels);
        void writeActuatorsXML(ComponentInstance* comp, QString& actuatorsOut);
        void writeSensorsXML(ComponentInstance* comp, QString& sensorsOut);
        
        void writeConstraintXML(ComponentInstance* compA, const QString& connA, 
                                ComponentInstance* compB, const QString& connB, 
                                QString& constraintsOut, QString& contactsOut);
            
    public:
        Project();
        ~Project();

        bool loadProject(const QString& path);
        void unloadProject();

        ComponentInstance* getComponentByUid(int uid);
        ComponentInstance* getRootComponent();

        QString generateMujocoXML();
};