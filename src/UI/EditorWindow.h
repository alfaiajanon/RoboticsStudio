#pragma once    

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include "Docking/SceneTreePanel.h"
#include "Docking/InspectorPanel.h"
#include "Docking/PlotPanel.h"
#include "BottomBar/ComponentsPanel.h"
#include "Rendering/CameraController.h"
#include "BottomBar/ScriptPanel.h"
#include "BottomBar/OutputPanel.h"
#include "Rendering/MViewport.h"

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