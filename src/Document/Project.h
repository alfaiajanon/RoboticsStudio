#pragma once

#include <functional>

#include "Utils/Global.h"
#include "Utils/Spatial.h"
// #include "Telemetry/PlotTarget.h"
#include "Simulation/MicroController/MicroController.h"
#include <QByteArray>
#include <QHash>
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
        // Parts whose model isn't in the library: not loaded, written back on save.
        QJsonArray unknownComponents;
        Rotation rootRotation;

        QHash<int, QByteArray> firmwareBinaries;   // compiled firmware for the next run, by board uid (-1: the ideal board)
        QHash<int, int> serialMonitorBauds;        // the Serial Monitor's baud rate per board ("serial_monitor")
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
        static QString levelName(ProjectLevel l) { return l == ProjectLevel::Real ? "real" : "prototype"; }

        Netlist& netlist() { return wiring; }
        QHash<int, QPointF>& layout() { return schematicLayout; }

        // Board components (a "board" block in their .rsdef), by uid.
        QList<ComponentInstance*> boards() const;
        static bool isBoard(const ComponentInstance* comp);
        // The boards that run at the Real level: every board with a sketch in
        // its slot, by uid. They all run at once, in lockstep on one sim clock.
        QList<ComponentInstance*> runningBoards() const;
        RealBoard realBoard(int uid) const;   // a board with the wiring; invalid if uid isn't a board
        QList<RealBoard> realBoards() const;  // every board, by uid

        // What Play compiles and runs: one sketch per board (uid; -1 = the
        // ideal board at the Prototype level, running script.current).
        struct FirmwareJob {
            int uid = -1;
            QString name;         // the board's name ("the ideal board")
            QString sketchPath;   // relative to the project
        };
        QList<FirmwareJob> firmwareJobs() const;

        // The sketch the ideal board runs (Prototype: script.current); empty
        // at the Real level, where each board's slot holds its own.
        QString activeSketchPath() const;
        QString readSketch(const QString& relPath) const;
        // Prototype: the ideal board's robot_pins.h for the sketch (its pin
        // names). Real: empty; the sketch uses the board's own pins.
        QString pinsHeader();
        // Real: a board's pins_arduino.h (A0, LED_BUILTIN, ...) and its
        // macros, as the real core has them; empty for the ideal board.
        QString boardVariantHeader(int uid) const;
        QStringList boardDefines(int uid) const;
        static bool isFirmwarePath(const QString& path);

        // Scripts live under <project>/script/: a firmware sketch in a folder
        // of its own, named after it (script/arm/arm.ino, with its .h and .cpp
        // files: an Arduino sketch folder); a stage script as one file
        // (script/stage.js). Older projects keep their files at the root.
        static constexpr const char* kScriptDir = "script";
        // A sketch path (relative or absolute) whose folder is named after it.
        static bool inSketchFolder(const QString& sketchPath);
        // A valid Arduino sketch name from what the user typed ("My arm" ->
        // "My_arm": letters, digits, '_', '-', '.', first a letter or digit,
        // at most 63 characters); empty if nothing is left.
        static QString sketchName(const QString& typed);

        // Prototype -> Real is one way: a new project at the Real level in
        // `destDir` (must not exist), copied from this project as saved on
        // disk. Same robot, sketches, stage and plots; no board, no wiring.
        // A sketch that includes robot_pins.h gets one of its own there, with
        // the Prototype's names set to -1 ("not wired yet") for the user to
        // fill in with board pins; it is the user's file, never regenerated.
        // Returns the new .rsproj path, or empty with `error` set.
        QString createRealCopy(const QString& destDir, const QString& name, QString* error);
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
        // Compiled firmware per board (Play compiles, the run loads).
        void setFirmwareBinary(int uid, const QByteArray& wasm) { firmwareBinaries.insert(uid, wasm); }
        QByteArray firmwareBinary(int uid) const { return firmwareBinaries.value(uid); }
        void clearFirmwareBinaries() { firmwareBinaries.clear(); }
        // The Serial Monitor's baud rate per board (UI state, saved with the
        // project like the plots; not undoable).
        int serialMonitorBaud(int uid) const { return serialMonitorBauds.value(uid, 9600); }
        void setSerialMonitorBaud(int uid, int baud) { serialMonitorBauds.insert(uid, baud); }
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
