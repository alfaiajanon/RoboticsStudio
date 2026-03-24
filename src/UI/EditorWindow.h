#pragma once    

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include "Docking/SceneTreePanel.h"
#include "Docking/InspectorPanel.h"
#include "Docking/PlotPanel.h"
#include "BottomBar/ComponentsPanel.h"
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
        MViewport *viewport;
        QTabWidget *bottomTabs;

        SceneTreePanel* sceneTree;
        InspectorPanel* inspector;
        ComponentsPanel* componentsPanel;
        OutputPanel* outputPanel;
        PlotPanel* plotPanel;

        EditorWindow(QWidget* parent = nullptr);
        
        void refresh();
        int getCurrentSelectedUid() const;

    signals:
        void componentLibDragStart(const QString& modelId);
        void componentLibDragEnd();
        

    public slots:
        void selectComponent(int uid);
        void clearSelection();
};