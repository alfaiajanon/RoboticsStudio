#pragma once

#include <QSet>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include "Simulation/Components/ComponentInstance.h"
#include "Simulation/MicroController/MicroController.h"



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
        
        int nextComponentUid = 1; 
        ComponentInstance* rootComponent = nullptr;
        QMap<int, ComponentInstance*> componentMap;
        QList<Constraint*> constraintList;

        Microcontroller microcontroller;
        QString currentScript;



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

        QJsonObject getProjectData() const;

        ComponentInstance* getRootComponent();
        ComponentInstance* getComponentByUid(int uid);
        QMap<int, ComponentInstance*>& getComponentMap();

        ComponentInstance* createComponentInstance( const int parentUid, 
                                                    const QString& parentConnector, 
                                                    const QString& modelId, 
                                                    const QString& selfConnector, 
                                                    const float snapAngle);
        
        Microcontroller* getMicrocontroller() { return &microcontroller; }
    
        void setScript(QString path);
        QString getScript() const { return currentScript; }

        
        QString generateMujocoXML();
};