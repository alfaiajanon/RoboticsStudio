#pragma once    

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include "Docking/SceneTreePanel.h"
#include "Rendering/MViewport.h"

class MainWindow : public QMainWindow{
    Q_OBJECT

    private:
    QSplitter *topDownSplitter;
    
    
    void setupSplitting();
    void setupRightDocking();
    
    public:
        MViewport *viewport;
        QTabWidget *bottomTabs;
        SceneTreePanel* sceneTree;
        
        MainWindow(QWidget* parent = nullptr);
};