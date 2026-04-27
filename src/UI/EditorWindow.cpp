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
#include "Simulation/Components/LibraryManager.h"
#include "UI/Toast/Toast.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include "mujoco/mujoco.h"
#include "Dialogs/NewProjectDialog.h"


/*
 * Initializes the main Editor Window workspace.
 * Sets up the layout splitting and default docking areas.
 */
EditorWindow::EditorWindow(QWidget* parent) : QMainWindow(parent){
    this->setWindowTitle("Robotics Studio");
    this->resize(1400, 850);

    topDownSplitter=nullptr;
    viewport=nullptr;
    bottomTabs=nullptr;

    setupMenuBar();
    setupSplitting();
    setupRightDocking();
}






void EditorWindow::setupSimConn(){
    SimulationManager* simManager = Application::getInstance()->getSimulationManager();
    connect(simManager, &SimulationManager::fpsUpdated, this, [this](int fps) {
        fpsLabel->setText(QString("FPS: %1").arg(fps));
        if (fps < 30) {
            fpsLabel->setStyleSheet("color: #ff5555; background-color: #2d2d2d; padding: 4px; border-radius: 4px;");
        } else {
            fpsLabel->setStyleSheet("color: #cccccc; background-color: #2d2d2d; padding: 4px; border-radius: 4px;");
        }
    });
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






// Instantiates the top menu bar and populates it with standard application actions.
// Categories are split into File, Edit, View, and Help, with standard OS keyboard shortcuts applied.
#pragma region MenuBar

void EditorWindow::setupMenuBar() {
    QMenuBar* topMenu = this->menuBar();

    QMenu* fileMenu = topMenu->addMenu("File");
    QAction* newAct = fileMenu->addAction("New Project");
    newAct->setShortcut(QKeySequence::New);
    QAction* openAct = fileMenu->addAction("Open Project...");
    openAct->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    QAction* saveAct = fileMenu->addAction("Save");
    saveAct->setShortcut(QKeySequence::Save);
    QAction* saveAsAct = fileMenu->addAction("Save As...");
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    QAction* exitAct = fileMenu->addAction("Exit");
    exitAct->setShortcut(QKeySequence::Quit);

    QMenu* editMenu = topMenu->addMenu("Edit");
    QAction* undoAct = editMenu->addAction("Undo");
    undoAct->setShortcut(QKeySequence::Undo);
    QAction* redoAct = editMenu->addAction("Redo");
    redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addSeparator();
    QAction* prefsAct = editMenu->addAction("Preferences");
    prefsAct->setShortcut(QKeySequence::Preferences);

    QMenu* viewMenu = topMenu->addMenu("View");
    QAction* toggleGridAct = viewMenu->addAction("Toggle Grid");
    toggleGridAct->setCheckable(true); 
    toggleGridAct->setChecked(true);   
    viewMenu->addSeparator();
    QAction* resetLayoutAct = viewMenu->addAction("Reset Layout");
    QAction* frameSceneAct = viewMenu->addAction("Frame Scene");
    frameSceneAct->setShortcut(QKeySequence(Qt::Key_F));

    QMenu* helpMenu = topMenu->addMenu("Help");
    QAction* shortcutsAct = helpMenu->addAction("Keyboard Shortcuts");
    QAction* docsAct = helpMenu->addAction("MuJoCo Documentation");
    helpMenu->addSeparator();
    QAction* aboutAct = helpMenu->addAction("About RoboticsStudio");

    // signals 
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    connect(frameSceneAct, &QAction::triggered, this, &EditorWindow::frameScene);

    connect(newAct, &QAction::triggered, this, [this]() {
        NewProjectDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            QString newPath = dialog.getCreatedProjectPath();

            Application::getInstance()->openProject(newPath);
        }
    });

    connect(openAct, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Open Project", "", "Robotics Studio Project (*.rsproj);;All Files (*)");
        if (!path.isEmpty()) {
            Application::getInstance()->openProject(path);
        }
    });

    connect(saveAct, &QAction::triggered, this, [this, saveAsAct]() {
        Project* proj = Application::getInstance()->getProject();
        bool success = proj->saveProject();
        if (success) {
            Toast::showMessage(this, "Project saved successfully!");
        } else {
            Toast::showMessage(this, "Failed to save project. Please choose a location.", 3000);
            saveAsAct->trigger(); 
        }
    });

    connect(saveAsAct, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Save Project As", "", "Robotics Studio Project (*.rsproj)");
        if (!path.isEmpty()) {
            Project* proj = Application::getInstance()->getProject();
            
            if (!path.endsWith(".rsproj")) {
                path += ".rsproj";
            }
            
            proj->setProjectPath(path);
            proj->saveProject();
            Toast::showMessage(this, "Project saved successfully!");
        }
    });
}







#pragma region Splitting, bottom

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
    bottomTabs->addTab(outputPanel, "Output");
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

    // add ScriptPanel
    scriptPanel = new ScriptPanel();
    scriptPanel->setMinimumHeight(0);
    scriptPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    bottomTabs->addTab(scriptPanel, "Script");

    
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
            project->reloadScript();
            
            if (this->plotPanel) {
                this->plotPanel->clearAllGraphs();
            }

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
    camera = new Camera(MujocoContext::getInstance()->getCamera());
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






#pragma region right docking

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
    plotPanel = new PlotPanel();
    graphDock->setWidget(plotPanel);
    addDockWidget(Qt::BottomDockWidgetArea, graphDock);

    sceneDock->setFeatures(QDockWidget::DockWidgetMovable);
    inspectorDock->setFeatures(QDockWidget::DockWidgetMovable);
    graphDock->setFeatures(QDockWidget::DockWidgetMovable);

    sceneDock->setMinimumWidth(150);
    inspectorDock->setMinimumWidth(150);
    graphDock->setMinimumHeight(200);
    
    splitDockWidget(sceneDock, graphDock, Qt::Vertical);
    splitDockWidget(sceneDock, inspectorDock, Qt::Horizontal);



    QList<QDockWidget*> horizontalDocks = {sceneDock, inspectorDock};
    QList<QDockWidget*> verticalDocks = {sceneDock, graphDock};
    QList<int> horizontalSizes = {270, 330}; 
    QList<int> verticalSizes = {450, 400}; 
    resizeDocks(horizontalDocks, horizontalSizes, Qt::Horizontal);
    resizeDocks(verticalDocks, verticalSizes, Qt::Vertical);
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





#pragma region Utility




// Assuming your wrapper is accessible, e.g., 'camera' is a member variable of ViewportPanel
void EditorWindow::frameScene() {
    MujocoContext* mj = MujocoContext::getInstance();
    mjModel* m = mj->getModel();
    mjData* d = mj->getData();

    if (!m || !d || m->ngeom == 0) return;

    // 1. Initialize extreme boundaries
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();

    bool isValid = false;

    // 2. Iterate over all geometry to find the bounding box
    for (int i = 0; i < m->ngeom; i++) {
        // Skip the infinite ground plane!
        if (m->geom_type[i] == mjGEOM_PLANE) continue;

        float cx = d->geom_xpos[3*i + 0];
        float cy = d->geom_xpos[3*i + 1];
        float cz = d->geom_xpos[3*i + 2];

        // Grab the largest size dimension for a quick bounding radius
        float s0 = m->geom_size[3*i + 0];
        float s1 = m->geom_size[3*i + 1];
        float s2 = m->geom_size[3*i + 2];
        float maxRadius = std::max({s0, s1, s2});

        // Expand the bounds
        minX = std::min(minX, cx - maxRadius);
        minY = std::min(minY, cy - maxRadius);
        minZ = std::min(minZ, cz - maxRadius);
        
        maxX = std::max(maxX, cx + maxRadius);
        maxY = std::max(maxY, cy + maxRadius);
        maxZ = std::max(maxZ, cz + maxRadius);
        
        isValid = true;
    }

    // 3. Frame the camera using the calculated bounds
    if (isValid && camera) {
        float centerX = (minX + maxX) / 2.0f;
        float centerY = (minY + maxY) / 2.0f;
        float centerZ = (minZ + maxZ) / 2.0f;

        float dx = maxX - centerX;
        float dy = maxY - centerY;
        float dz = maxZ - centerZ;
        float radius = std::sqrt(dx*dx + dy*dy + dz*dz);

        // Center target
        Position targetPoint(centerX, centerY, centerZ);

        // Pull back on X/Y, push up on Z
        float pullBack = radius * 1.5f; 
        float pullUp = radius * 0.8f;   
        Position cameraPoint(centerX - pullBack, centerY - pullBack, centerZ + pullUp);

        // Apply to your wrapper
        camera->setTarget(targetPoint);
        camera->setPosition(cameraPoint);
    }
}