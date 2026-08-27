#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>

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

        QPushButton* playBtn;
        QLabel* fpsLabel;

        void setupMenuBar();
        void setupSplitting();
        void setupRightDocking();
        void setupMainViewport();

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
        void frameScene();
        void setupSimConn();
        int getCurrentSelectedUid() const;

    signals:
        void componentLibDragStart(const QString& modelId);
        void componentLibDragEnd();


    public slots:
        void selectComponent(int uid);
        void clearSelection();
};
