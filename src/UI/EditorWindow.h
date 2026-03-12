#pragma once    

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include "Docking/SceneTreePanel.h"
#include "Docking/InspectorPanel.h"
#include "Rendering/MViewport.h"

class EditorWindow : public QMainWindow{
    Q_OBJECT

    private:
        QSplitter *topDownSplitter;
        int currentSelectedUid = -1;
        
        void setupSplitting();
        void setupRightDocking();
        
    public:
        MViewport *viewport;
        QTabWidget *bottomTabs;
        SceneTreePanel* sceneTree;
        InspectorPanel* inspector;
        
        EditorWindow(QWidget* parent = nullptr);

    public slots:
        void selectComponent(int uid);
        void clearSelection();
};