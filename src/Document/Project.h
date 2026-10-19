#pragma once

#include <functional>

#include "Utils/Global.h"
#include "Utils/Spatial.h"
// #include "Telemetry/PlotTarget.h"
#include "Simulation/MicroController/MicroController.h"
#include "Simulation/Firmware/FirmwareRuntime.h"
#include "Document/IdealBoard.h"
#include "Document/RealBoard.h"
#include "Document/Netlist.h"
#include <QPointF>
#include "Document/Stage.h"
#include <qobject.h>


class ComponentInstance;
class Constraint;


/*
 * Manages the active workspace and holds the loaded robot assembly.
 * Handles parsing the project JSON, building the component memory tree,
 * and compiling the final MuJoCo XML string for the physics engine.
 */

// Prototype: one implicit ideal board, every part auto-wired. Real: board
// components run their own firmware through the project's wiring.
enum class ProjectLevel { Prototype, Real };


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

        FirmwareRuntime firmware{"MCU"};   // the robot's firmware while it is C++ (.ino/.cpp)
        QByteArray firmwareBinary;         // compiled firmware for the next run (the compiler sets it)
        MicroController stageController{"STAGE"};   // runs the stage script; sees props + robot pose only
        Stage stage;

        ProjectLevel level = ProjectLevel::Prototype;
        Netlist wiring;                     // kept (unused) at the Prototype level
        QHash<int, QPointF> schematicLayout; // wiring page block positions, by uid

        int currentScriptIdx=0;
        QString currentScriptContent;
        QJsonArray scriptPaths;

        // Plot panel layout ("plotting" in the .rsproj). Authored by the view, so
        // Project only stores it: the provider is asked for a fresh snapshot on
        // every save, and loadProject() keeps the parsed object for the view to apply.
        QJsonObject plotting;
        std::function<QJsonObject()> plottingProvider;


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
        QString getName() const { return projectData["meta"].toObject()["name"].toString(); }
        // Prototype level: every part's signal pins on the ideal controller.
        IdealBoard idealBoard() { return IdealBoard::fromTree(rootComponent); }

        static constexpr int kSchemaVersion = 3;   // "schema" in the .rsproj; files without it are 2
        ProjectLevel getLevel() const { return level; }
        void setLevel(ProjectLevel l) { level = l; }
        static QString levelName(ProjectLevel l) { return l == ProjectLevel::Real ? "real" : "prototype"; }

        Netlist& netlist() { return wiring; }
        QHash<int, QPointF>& layout() { return schematicLayout; }

        // Board components (a "board" block in their .rsdef), by uid.
        QList<ComponentInstance*> boards() const;
        static bool isBoard(const ComponentInstance* comp);
        // The board that runs at the Real level: the first (by uid) with a
        // sketch in its slot, else the first board; null without boards.
        // One board runs until boards can talk to each other (UART).
        ComponentInstance* activeBoard() const;
        RealBoard realBoard() const;   // the active board with the wiring; invalid without one

        // The sketch the robot runs: the ideal board's (script.current) at
        // Prototype, the active board's slot at Real. Empty = none.
        QString activeSketchPath() const;
        QString readActiveSketch() const;
        // robot_pins.h for the active sketch (ideal or real board).
        QString pinsHeader();
        // Real level: writes <project>/<header_folder>/robot_pins.h for the
        // active board (what tools/check_transfer.sh compiles). False on error.
        bool writeBoardHeader();
        // Writes the .rsproj only; the editor (EditorWindow::saveProject) owns
        // the "unsaved changes" state and marks it clean on success.
        bool saveProject();
        void unloadProject();
        void refresh();

        QJsonObject getPlotting() const { return plotting; }
        void setPlottingProvider(std::function<QJsonObject()> provider) { plottingProvider = std::move(provider); }

        QJsonObject getProjectData();
        void setProjectData(QJsonObject data);

        // Seed of every random generator in a run, so a run repeats exactly
        // ("simulation": {"seed"} in the .rsproj, default 1).
        int getSimulationSeed() const;
        void setSimulationSeed(int seed);

        // QList<PlotTarget> getActivePlotsVal(){ return activePlots; }
        // QList<PlotTarget>* getActivePlots(){ return &activePlots; }
        ComponentInstance* getRootComponent();
        ComponentInstance* getComponentByUid(int uid);
        Rotation getRootRotation() const { return rootRotation; }
        void setRootRotation(const Rotation& rot) { rootRotation = rot; }
        QMap<int, ComponentInstance*>& getComponentMap();
        FirmwareRuntime* getFirmware() { return &firmware; }
        // The active sketch is firmware (C++: .ino/.cpp). A .js robot script
        // (JavaScript robot scripts were removed) is refused at Play.
        bool usesFirmware() const;
        bool hasRobotScript() const { return !activeSketchPath().isEmpty(); }
        // The controller that runs the robot: always its firmware.
        RobotController* getRobotController() { return &firmware; }
        void setFirmwareBinary(const QByteArray& wasm) { firmwareBinary = wasm; }
        const QByteArray& getFirmwareBinary() const { return firmwareBinary; }
        MicroController* getStageController() { return &stageController; }
        Stage* getStage() { return &stage; }
        QString readStageScript() const;   // empty when the project has none

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

        // Subtree variants used by Commands/RemoveComponentCommand.h: take/adopt a
        // component together with all its descendants (intra-subtree parent/children
        // links are preserved; only the root's external link is severed/restored).
        ComponentInstance* takeSubtree(int uid);
        void adoptSubtree(ComponentInstance* root);



        void reloadScript();
        void setScript(int idx);
        void setScriptPaths(QJsonArray newarr){ scriptPaths = newarr; };
        QJsonArray getScriptPaths() const { return scriptPaths; }
        int getCurrentScriptIdx() const { return currentScriptIdx; }
        QString getScriptPath() const {return scriptPaths[currentScriptIdx].toString(); }
        bool hasScriptAt(int idx) const { return idx >= 0 && idx < scriptPaths.size(); }
        QString getScript() const { return currentScriptContent; }

        QString getProjectDirectory() const { return directoryPath; }


        QString generateMujocoXML(bool forSimulation = false);
};
