#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include <QMenu>

#include "View/Panels/SceneTreePanel.h"
#include "View/Panels/InspectorPanel.h"
#include "View/Panels/PlotPanel.h"
#include "View/Panels/ComponentsPanel.h"
#include "View/Panels/ScriptPanel.h"
#include "View/Panels/OutputPanel.h"
#include "View/Viewport/MViewport.h"
#include "View/Viewport/Camera.h"



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
        QMenu* editMenu = nullptr;

        void setupMenuBar();
        void setupSplitting();
        void setupRightDocking();
        void setupMainViewport();
        void setupCameraControls();
        bool simConnInitialized = false;

    public:
        Camera *camera;
        MViewport *viewport;
        QTabWidget *bottomTabs;

        SceneTreePanel* sceneTree;
        InspectorPanel* inspector;
        ComponentsPanel* componentsPanel;
        OutputPanel* outputPanel;
        PlotPanel* plotPanel;
        ScriptPanel* scriptPanel;


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

    signals:
        void componentLibDragStart(const QString& modelId);
        void componentLibDragEnd();


    public slots:
        void selectComponent(int uid);
        void clearSelection();
};
