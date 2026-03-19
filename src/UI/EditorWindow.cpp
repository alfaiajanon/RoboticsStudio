#include "EditorWindow.h"
#include <QDockWidget>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabBar>
#include "Core/Application.h"
#include "Rendering/MViewport.h"
#include "Docking/SceneTreePanel.h"
#include "Docking/InspectorPanel.h"
#include "BottomBar/OutputPanel.h"
#include "BottomBar/ComponentsPanel.h"
#include "Core/InputManager.h"
#include "Rendering/CameraController.h"
#include "Simulation/Components/LibraryManager.h"

/*
 * Initializes the main Editor Window workspace.
 * Sets up the layout splitting and default docking areas.
 */
EditorWindow::EditorWindow(QWidget* parent) : QMainWindow(parent){
    this->setWindowTitle("Robotics Studio");
    this->resize(1280, 720);

    topDownSplitter=nullptr;
    viewport=nullptr;
    bottomTabs=nullptr;

    setupSplitting();
    setupRightDocking();
}





void EditorWindow::refresh() {
    if(sceneTree){
        Project* project = Application::getInstance()->getProject();
        sceneTree->buildFromProject(project);
    }
    if(inspector){
        inspector->buildUI();
    }
    int temp = currentSelectedUid;
    currentSelectedUid = -1;
    selectComponent(temp);
}




int EditorWindow::getCurrentSelectedUid() const {
    return currentSelectedUid;
}




/*
 * Configures the central vertical splitter.
 * Initializes the 3D viewport, camera controller, and bottom tab panels.
 */
void EditorWindow::setupSplitting(){
    this->topDownSplitter = new QSplitter(Qt::Vertical);
    setCentralWidget(topDownSplitter);

    setupMainViewport();

    bottomTabs = new QTabWidget();
    bottomTabs->setMinimumHeight(bottomTabs->tabBar()->sizeHint().height());
    bottomTabs->setMovable(true);
    bottomTabs->setTabsClosable(false);

    // add OutputPanel
    outputPanel = new OutputPanel();
    outputPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    outputPanel->setMinimumHeight(0);
    bottomTabs->addTab(outputPanel, "Console");
    connect(LogDispatcher::getInstance(), &LogDispatcher::messageLogged, 
            outputPanel, &OutputPanel::appendMessage);

    // add ComponentsPanel
    componentsPanel = new ComponentsPanel();
    componentsPanel->setMinimumHeight(0);
    componentsPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    connect(&LibraryManager::getInstance(), &LibraryManager::catalogLoaded, 
            componentsPanel, &ComponentsPanel::loadCatalog);
    QString existingCatalog = LibraryManager::getInstance().getCatalogPath();
    if (!existingCatalog.isEmpty()) {
        componentsPanel->loadCatalog(existingCatalog);
    }
    bottomTabs->addTab(componentsPanel, "Components");

    
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
}








void EditorWindow::setupMainViewport() {
    viewport = new MViewport();

    QVBoxLayout* hudLayout = new QVBoxLayout(viewport);
    hudLayout->setContentsMargins(15, 15, 15, 15);

    fpsLabel = new QLabel("FPS: --", viewport);
    fpsLabel->setStyleSheet("color: #8e8e8e; background-color: rgba(30, 30, 30, 150); padding: 5px; border-radius: 4px;");
    hudLayout->addWidget(fpsLabel, 0, Qt::AlignTop | Qt::AlignLeft);

    hudLayout->addStretch();

    playBtn = new QPushButton("▶ PLAY", viewport);
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setStyleSheet("background-color: #2E7D32; color: white; padding: 8px 15px; border-radius: 4px; font-size: 14px;");
    hudLayout->addWidget(playBtn, 0, Qt::AlignBottom | Qt::AlignLeft);

    connect(playBtn, &QPushButton::clicked, this, [this]() {
        SimulationManager* sim = Application::getInstance()->getSimulationManager();
        
        if (sim->getState() == SimulationState::EDITING) {
            Project* project = Application::getInstance()->getProject();
            project->setScript("/home/anon/Documents/Code Projects/Mixed Projects/RoboticsStudio/demo/demoScript.js");
            project->getMicrocontroller()->compile(project->getScript(), project->getRootComponent());

            sim->play();
            playBtn->setText("⬛ STOP");
            playBtn->setStyleSheet("background-color: #C62828; color: white; padding: 8px 15px; border-radius: 4px; font-weight: bold; font-size: 14px;");
            
            if (this->inspector) this->inspector->setInputState(false);
            if (this->componentsPanel) this->componentsPanel->setEnabled(false);
            
        } else {
            sim->edit();
            playBtn->setText("▶ PLAY");
            playBtn->setStyleSheet("background-color: #2E7D32; color: white; padding: 8px 15px; border-radius: 4px; font-weight: bold; font-size: 14px;");
            
            if (this->inspector) this->inspector->setInputState(true);
            if (this->componentsPanel) this->componentsPanel->setEnabled(true);
        }
    });


    CameraController* controller = new OrbitCameraController();
    Camera *camera = new Camera(MujocoContext::getInstance()->getCamera());
    controller->setCamera(camera);
    camera->setPosition(Position(0.0, -0.20, 0.05));
    camera->setTarget(Position(0.0, 0.0, 0.0));

    InputManager &im = InputManager::getInstance();
    im.bind(viewport);
    im.onMouseButtonPress([controller](int x, int y){ controller->onMouseDown(x, y); });
    im.onCursorPos([controller](double x, double y){ controller->onMouseMove(static_cast<int>(x), static_cast<int>(y)); });
    im.onMouseButtonRelease([controller](int x, int y){ controller->onMouseUp(); });
    im.onScroll([controller](double xoffset, double yoffset){ controller->onMouseScroll(yoffset); });

    topDownSplitter->addWidget(viewport);
}







/*
 * Configures the right and bottom docking panels.
 * Routes UI signals through the central EditorWindow mediator.
 */
void EditorWindow::setupRightDocking(){
    QDockWidget *inspectorDock = new QDockWidget("Inspector");
    inspector = new InspectorPanel();
    inspectorDock->setWidget( inspector );
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    QDockWidget *sceneDock = new QDockWidget();
    QWidget* emptyTitleBar = new QWidget();
    sceneDock->setTitleBarWidget(emptyTitleBar);
    sceneTree = new SceneTreePanel();
    sceneDock->setWidget(sceneTree);
    addDockWidget(Qt::RightDockWidgetArea, sceneDock);

    connect(sceneTree, &SceneTreePanel::componentSelected, this, &EditorWindow::selectComponent);

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




/*
 * Master selection coordinator.
 * Safely updates all panels and viewports without triggering circular signal loops.
 */
void EditorWindow::selectComponent(int uid) {
    if (this->currentSelectedUid == uid) return;
    this->currentSelectedUid = uid;

    Log::info("Selected component UID: " + QString::number(uid));

    sceneTree->blockSignals(true);
    sceneTree->highlightItem(uid);
    sceneTree->blockSignals(false);

    viewport->blockSignals(true);
    // viewport->highlightMesh(uid); // TODO: Implement 3D selection highlight
    viewport->blockSignals(false);

    inspector->setComponent(uid);
}





/*
 * Clears the active component selection.
 * Resets tracking variables and clears downstream UI panels.
 */
void EditorWindow::clearSelection() {
    if (this->currentSelectedUid == -1) return;
    this->currentSelectedUid = -1;

    sceneTree->blockSignals(true);
    sceneTree->highlightItem(-1);
    sceneTree->blockSignals(false);

    viewport->blockSignals(true);
    // viewport->clearHighlight(); // TODO: Implement 3D selection clear
    viewport->blockSignals(false);

    inspector->setComponent(-1);
}