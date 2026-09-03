#pragma once

#include "Utils/Global.h"
#include "Utils/Spatial.h"
#include "Telemetry/PlotTarget.h"
#include "Simulation/MicroController/MicroController.h"
#include <qobject.h>


class ComponentInstance;
class Constraint;


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

        int nextComponentUid = 1; // for auto-assigning UIDs to new components

        ComponentInstance* rootComponent = nullptr;
        QMap<int, ComponentInstance*> componentMap;
        QList<Constraint*> constraintList;
        Rotation rootRotation;

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

        QString writeWorldBodyXML(ComponentInstance* comp, QSet<int>& visitedComponents, bool isSimulation = false);
        void writeContactXML(ComponentInstance* comp, QString& contacts, QSet<int>& visitedComponents);
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
        QString getProjectPath(){return projectPath;}
        bool saveProject();
        void unloadProject();
        void refresh();

        QJsonObject getProjectData();
        void setProjectData(QJsonObject data);

        QList<PlotTarget> getActivePlotsVal(){ return activePlots; }
        QList<PlotTarget>* getActivePlots(){ return &activePlots; }
        ComponentInstance* getRootComponent();
        ComponentInstance* getComponentByUid(int uid);
        Rotation getRootRotation() const { return rootRotation; }
        void setRootRotation(const Rotation& rot) { rootRotation = rot; }
        QMap<int, ComponentInstance*>& getComponentMap();
        MicroController* getMicroController() { return &microcontroller; }

        void setRootComponent(ComponentInstance* comp);
        void resetRootComponent();

        ComponentInstance* createComponentInstance( const int parentUid,
                                                    const QString& parentConnector,
                                                    const QString& modelId,
                                                    const QString& selfConnector,
                                                    const float snapAngle);

        // Undo/redo support for component creation (see Commands/AddComponentCommand.h).
        // takeComponent() unlinks an instance from map/parent/root WITHOUT deleting it
        // (caller takes ownership); adoptComponent() re-links a previously taken instance.
        ComponentInstance* takeComponent(int uid);
        void adoptComponent(ComponentInstance* comp);



        void reloadScript();
        void setScript(int idx);
        void setScriptPaths(QJsonArray newarr){ scriptPaths = newarr; };
        QJsonArray getScriptPaths() const { return scriptPaths; }
        QString getScriptPath() const {return scriptPaths[currentScriptIdx].toString(); }
        QString getScript() const { return currentScriptContent; }

        QString getProjectDirectory() const { return directoryPath; }


        QString generateMujocoXML(bool forSimulation = false);
};
