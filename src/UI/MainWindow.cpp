#include "MainWindow.h"
#include <QDockWidget>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabBar>
#include "Rendering/MViewport.h"
#include "Docking/SceneTreePanel.h"
// #include "Docking/InspectorPanel.h"
#include "BottomBar/ConsolePanel.h"
#include "BottomBar/ComponentsPanel.h"
#include "Core/InputManager.h"
#include "Rendering/CameraController.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent){
    this->setWindowTitle("Robotics Studio");
    this->resize(1280, 720);

    topDownSplitter=nullptr;
    viewport=nullptr;
    bottomTabs=nullptr;

    setupSplitting();
    setupRightDocking();
}




void MainWindow::setupSplitting(){
    this->topDownSplitter = new QSplitter(Qt::Vertical);
    setCentralWidget(topDownSplitter);


    
    viewport = new MViewport();
    topDownSplitter->addWidget(viewport);

    CameraController* controller = new OrbitCameraController();
    Camera *camera = new Camera(MujocoContext::getInstance()->getCamera());
    controller->setCamera(camera);
    camera->setPosition(Position(0.0, -3.0, 1.0));
    camera->setTarget(Position(0.0, 0.0, 0.0));

    InputManager &im=InputManager::getInstance();
    im.bind(viewport);
    im.onMouseButtonPress(
        [controller](int x, int y){
            controller->onMouseDown(x, y);
        }
    );
    im.onCursorPos(
        [controller](double x, double y){
            controller->onMouseMove(static_cast<int>(x), static_cast<int>(y));
        }
    );
    im.onMouseButtonRelease(
        [controller](int x, int y){
            controller->onMouseUp();
        }
    );
    im.onScroll(
        [controller](double xoffset, double yoffset){
            controller->onMouseScroll(yoffset);
        }
    );



    bottomTabs = new QTabWidget();
    bottomTabs->setMinimumHeight(bottomTabs->tabBar()->sizeHint().height());
    bottomTabs->setMovable(true);
    bottomTabs->setTabsClosable(false);
    bottomTabs->addTab(new QWidget(), "Console");
    bottomTabs->addTab(new QWidget(), "Components");
    topDownSplitter->addWidget(bottomTabs);
    topDownSplitter->setCollapsible(0, false);
    topDownSplitter->setCollapsible(1, false);
    topDownSplitter->setSizes({1000, bottomTabs->tabBar()->sizeHint().height()});

    connect(bottomTabs->tabBar(), &QTabBar::tabBarClicked, this, [this](int index){
        if(index==bottomTabs->currentIndex()){
            QList<int> sizes = topDownSplitter->sizes();
            int topSize    = sizes[0];
            int bottomSize = sizes[1];
            int tabHeight = bottomTabs->tabBar()->sizeHint().height();

            if(bottomSize <= tabHeight+10){
                topDownSplitter->setSizes({ topSize-250, 250});
            }
            else{
                topDownSplitter->setSizes({ topSize+bottomSize-tabHeight, tabHeight });
            }
        }
    });


    // QWidget *rightPanel = new QWidget();
    // rightPanel->setMinimumWidth(200);
    // mainSplitter->addWidget(rightPanel);
    // mainSplitter->setCollapsible(0, false);
    // mainSplitter->setCollapsible(1, false);
    // int w1 = this->width()*0.75;
    // int w2 = this->width()*0.25;
    // mainSplitter->setSizes({w1, w2});
}





void MainWindow::setupRightDocking(){
    QDockWidget *sceneDock = new QDockWidget("Scene tree");
    sceneTree = new SceneTreePanel();
    sceneDock->setWidget(sceneTree);
    addDockWidget(Qt::RightDockWidgetArea, sceneDock);

    QDockWidget *inspectorDock = new QDockWidget("Inspector");
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    QDockWidget *graphDock = new QDockWidget("Graph");
    addDockWidget(Qt::BottomDockWidgetArea, graphDock);


    sceneDock->setFeatures(QDockWidget::DockWidgetMovable);
    inspectorDock->setFeatures(QDockWidget::DockWidgetMovable);
    graphDock->setFeatures(QDockWidget::DockWidgetMovable);
    sceneDock->setMinimumWidth(150);
    inspectorDock->setMinimumWidth(150);

    
    splitDockWidget(sceneDock, graphDock, Qt::Vertical);
    splitDockWidget(sceneDock, inspectorDock, Qt::Horizontal);
}



