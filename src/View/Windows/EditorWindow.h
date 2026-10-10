#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include <QMenu>
#include <QJsonObject>

#include "View/Panels/SceneTreePanel.h"
#include "View/Panels/InspectorPanel.h"
#include "View/Panels/PlotPanel.h"
#include "View/Panels/ComponentsPanel.h"
#include "View/Panels/ScriptPanel.h"
#include "View/Panels/StagePanel.h"
#include "View/Panels/OutputPanel.h"
#include "View/Viewport/MViewport.h"
#include "View/Viewport/Camera.h"
#include "View/Widgets/HudStack.h"



class EditorWindow : public QMainWindow{
    Q_OBJECT

    private:
        QSplitter *topDownSplitter;
        int currentSelectedUid = -1;

        // scheduleRefresh() coalescing: several refresh requests in the same
        // event-loop turn (undo-stack indexChanged + reloadSimulation) collapse
        // into a single rebuild instead of double-logging/double-rebuilding.
        bool refreshScheduled = false;
        // Tracks QUndoStack::index() between indexChanged signals so the
        // handler can tell pushes/redos apart from undos.
        int lastUndoIndex = 0;

        QLabel* fpsLabel;
        QLabel* simTimeLabel;
        QLabel* collisionCountLabel;

        QPushButton* playBtn;
        QPushButton* pauseBtn;
        QMenu* editMenu = nullptr;
        // Ctrl+S: saves the open script on the Script page, the project elsewhere.
        QAction* saveAct = nullptr;

        // The plot layout is not command-based, so "modified" compares the
        // panel's current snapshot against the one taken at load/save.
        QJsonObject savedPlotLayout;

        void setupMenuBar();
        void setupSplitting();
        void setupRightDocking();
        void setupMainViewport();
        void setupCameraControls();
        bool simConnInitialized = false;

    public:
        Camera *camera;
        MViewport *viewport;
        HudStack *centerStack;
        QTabWidget *bottomTabs;

        SceneTreePanel* sceneTree;
        InspectorPanel* inspector;
        ComponentsPanel* componentsPanel;
        OutputPanel* outputPanel;
        PlotPanel* plotPanel;
        ScriptPanel* scriptPanel;
        StagePanel* stagePanel;


        EditorWindow(QWidget* parent = nullptr);

        void refresh();
        // Coalesced refresh: collapses multiple requests in the same
        // event-loop turn into one actual refresh().
        void scheduleRefresh();
        void frameScene();
        void setupSimConn();
        // Wires the Edit-menu Undo/Redo actions to Application's undo stack.
        // Must be called after Application::instance exists (Application's ctor
        // body) -- the EditorWindow itself is constructed before that.
        void setupUndoRedo();
        int getCurrentSelectedUid() const;

        // Writes the project (+ launcher thumbnail) and marks it unmodified.
        bool saveProject();
        // Baseline for "unsaved changes": call after a project is loaded or saved.
        void markProjectSaved();
        // Undo stack not clean, or plot layout changed since load/save.
        bool isProjectModified() const;
        // One Save All / Discard / Cancel prompt for the project and the open
        // script. False = cancelled (or a save failed): abort the close/switch.
        bool confirmUnsavedChanges();
        // Window title + its "*" marker.
        void updateModifiedIndicator();

    protected:
        void closeEvent(QCloseEvent* event) override;

    public:

    signals:
        void componentLibDragStart(const QString& modelId);
        void componentLibDragEnd();


    public slots:
        void selectComponent(int uid);
        void clearSelection();
};
