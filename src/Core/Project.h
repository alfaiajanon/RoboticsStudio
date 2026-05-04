#pragma once

#include <QSet>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QColor>
#include "Simulation/Components/ComponentInstance.h"
#include "Simulation/MicroController/MicroController.h"



enum class PlotTargetType {
    SENSOR,
    ACTUATOR
};

struct PlotTarget {
    int compUid;          // Which component?
    PlotTargetType type;  // Sensor or Actuator?
    QString ioKey;        // The dictionary key (e.g., "target_angle")
    QColor color;         // Line color on the graph
    bool isVisible = true;// UI Checkbox state
};



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

        MicroController microcontroller;
        int currentScriptIdx=0;
        QString currentScriptContent;
        QJsonArray scriptPaths;

        QList<PlotTarget> activePlots;



        void clear();
        void parseAssembly();
        void buildHierarchy(); 
        void applyTransforms();
        void applyDefaults();
        void saveDefaults();

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
        void setProjectPath(const QString& path);
        bool saveProject();
        void unloadProject();
        void refresh();

        QJsonObject getProjectData();
        void setProjectData(QJsonObject data);

        QList<PlotTarget> getActivePlotsVal(){ return activePlots; } 
        QList<PlotTarget>* getActivePlots(){ return &activePlots; }
        ComponentInstance* getRootComponent();
        ComponentInstance* getComponentByUid(int uid);
        QMap<int, ComponentInstance*>& getComponentMap();
        MicroController* getMicroController() { return &microcontroller; }

        void setRootComponent(ComponentInstance* comp);
        void resetRootComponent();

        ComponentInstance* createComponentInstance( const int parentUid, 
                                                    const QString& parentConnector, 
                                                    const QString& modelId, 
                                                    const QString& selfConnector,
                                                    const float snapAngle);
        
    
        void setScript(int idx);
        void reloadScript();
        QJsonArray getScriptPaths() const { return scriptPaths; }
        QString getScriptPath() const {return scriptPaths[currentScriptIdx].toString(); }
        QString getScript() const { return currentScriptContent; }

        QString getProjectDirectory() const { return directoryPath; }

        
        QString generateMujocoXML();
};