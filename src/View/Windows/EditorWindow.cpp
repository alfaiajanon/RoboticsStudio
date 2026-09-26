#include "EditorWindow.h"

#include <QDockWidget>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabBar>
#include <algorithm>
#include <limits>
#include <cmath>
#include <qaction.h>
#include <QDomDocument>
#include <QUndoStack>
#include <QTimer>
#include <qjsondocument.h>
#include <qlabel.h>

#include "Application/Application.h"
#include "Commands/GenericCommand.h"
#include "Document/Components/ComponentData.h"
#include "Document/Components/LibraryManager.h"
#include "Simulation/SimulationManager.h"
#include "Simulation/MujocoContext.h"
#include "View/Viewport/MViewport.h"
#include "View/Panels/SceneTreePanel.h"
#include "View/Panels/InspectorPanel.h"
#include "View/Panels/OutputPanel.h"
#include "View/Panels/ComponentsPanel.h"
#include "View/Viewport/InputManager.h"
#include "View/Viewport/CameraController.h"
#include "View/Widgets/Toast.h"
#include "View/Dialogs/NewProjectDialog.h"
#include "View/Windows/ComponentEditorWindow.h"



/*
 * Initializes the main Editor Window workspace.
 * Sets up the layout splitting and default docking areas.
 */
EditorWindow::EditorWindow(QWidget* parent) : QMainWindow(parent){
    this->setWindowTitle("Robotics Studio");
    this->resize(1600, 950);
    this->setWindowState(Qt::WindowMaximized);

    topDownSplitter=nullptr;
    viewport=nullptr;
    bottomTabs=nullptr;
    camera=nullptr;

    setupMenuBar();
    setupSplitting();
    setupRightDocking();
}






void EditorWindow::setupSimConn(){
    if (simConnInitialized) return; // simManager persists across project opens now
    simConnInitialized = true;

    SimulationManager* simManager = Application::getInstance()->getSimulationManager();
    connect(simManager, &SimulationManager::statusUpdated, this, [this](SimStatus status) {
        fpsLabel->setText(QString("FPS: %1").arg(status.fps));
        if (status.fps < 30) {
            fpsLabel->setStyleSheet("color: #ff5555;");
        } else {
            fpsLabel->setStyleSheet("color: #cccccc;");
        }
        simTimeLabel->setText(QString("Sim Time: %1").arg(status.simTime));
        collisionCountLabel->setText(QString("Contacts: %1").arg(status.activeContacts));
    });

    setupCameraControls();
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


/*
 * Coalesced refresh: multiple triggers in the same event-loop turn (e.g. the
 * undo stack's indexChanged AND Application::reloadSimulation, both caused by
 * one structural command) collapse into a single actual refresh() -- otherwise
 * every structural edit would rebuild twice and double-log the selection.
 */
void EditorWindow::scheduleRefresh() {
    if (refreshScheduled) return;
    refreshScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        refreshScheduled = false;
        refresh();
    });
}




int EditorWindow::getCurrentSelectedUid() const {
    return currentSelectedUid;
}






// Instantiates the top menu bar and populates it with standard application actions.
// Categories are split into File, Edit, View, and Help, with standard OS keyboard shortcuts applied.
#pragma region MenuBar

void EditorWindow::setupMenuBar() {
    QMenuBar* topMenu = this->menuBar();

    QMenu* projectMenu = topMenu->addMenu("Project");
    QAction* newAct = projectMenu->addAction("New Project");
    newAct->setShortcut(QKeySequence::New);
    QAction* openAct = projectMenu->addAction("Open Project...");
    openAct->setShortcut(QKeySequence::Open);
    projectMenu->addSeparator();
    QAction* saveAct = projectMenu->addAction("Save");
    saveAct->setShortcut(QKeySequence::Save);
    QAction* saveAsAct = projectMenu->addAction("Save As...");
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    projectMenu->addSeparator();
    QMenu* addComponentMenu = projectMenu->addMenu("Add Component");
    QAction* servoTemplateAct = addComponentMenu->addAction("Servo template");
    QAction* stepperTemplateAct = addComponentMenu->addAction("Stepper template");
    QAction* blankTemplateAct = addComponentMenu->addAction("Blank template");
    QAction* editComponentAct = projectMenu->addAction("Edit Component");
    projectMenu->addSeparator();
    QAction* exitAct = projectMenu->addAction("Exit");
    exitAct->setShortcut(QKeySequence::Quit);

    editMenu = topMenu->addMenu("Edit");
    // Undo/Redo actions are wired later in setupUndoRedo(): this window is
    // constructed while Application's singleton pointer is still null (member
    // init runs before Application::instance is assigned).
    editMenu->addSeparator();
    QAction* prefsAct = editMenu->addAction("Preferences");
    prefsAct->setShortcut(QKeySequence::Preferences);

    QMenu* viewMenu = topMenu->addMenu("View");
    QAction* frameSceneAct = viewMenu->addAction("Frame Scene");
    frameSceneAct->setShortcut(QKeySequence(Qt::Key_F));

    QMenu* debugMenu = topMenu->addMenu("Debug");
    QAction* dumpXMLAct = debugMenu->addAction("Dump Mujoco XML (Edit Mode)");
    QAction* dumpXMLActSim = debugMenu->addAction("Dump Mujoco XML (Simulation Mode)");

    QMenu* helpMenu = topMenu->addMenu("Help");
    QAction* shortcutsAct = helpMenu->addAction("Keyboard Shortcuts");
    QAction* rsDocsAct  = helpMenu->addAction("Robotics Studio Documentation");
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

        // --- Thumbnail Generation ---
        if (viewport) {
            QPixmap currentView = viewport->pixmap();
            if (!currentView.isNull()) {
                // Scale to a small, performant thumbnail for the Launcher card
                QPixmap thumbnail = currentView.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

                QFileInfo projInfo(proj->getProjectPath());
                QString projDir = projInfo.absolutePath();
                QString logoFileName = "logo.png";
                QString logoAbsPath = QDir(projDir).absoluteFilePath(logoFileName);

                if (thumbnail.save(logoAbsPath, "PNG")) {
                    // Update the project's JSON meta data to point to the new icon
                    QJsonObject pData = proj->getProjectData();
                    QJsonObject m = pData["meta"].toObject();
                    m["icon"] = logoFileName;
                    pData["meta"] = m;
                    proj->setProjectData(pData);
                } else {
                Log::warning("Failed to save project thumbnail to: " + logoAbsPath);
                }
            }
        }
        // -----------------------------

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


    connect(servoTemplateAct,&QAction::triggered, this, [this]() {
        QFile file(":/templates/servo.rsdef");
        if(!file.open(QIODevice::ReadOnly)){
            Log::error("Failed to load embedded template: " + file.errorString());
            return;
        }
        QJsonDocument doc=QJsonDocument::fromJson(file.readAll());
        ComponentData data=ComponentData::fromJson(doc.object(), "");
        ComponentEditorWindow* dialog = new ComponentEditorWindow(data,this);
        dialog->show();
    });

    connect(blankTemplateAct, &QAction::triggered, this, [this]() {
        ComponentData data;
        ComponentEditorWindow* dialog = new ComponentEditorWindow(data,this);
        dialog->show();
    });

    connect(dumpXMLActSim, &QAction::triggered, this, []() {
        Project* project = Application::getInstance()->getProject();
        QString xml = project->generateMujocoXML(true);
        QDomDocument doc;
        doc.setContent(xml);
        xml=doc.toString(4);
        Log::info("Current Mujoco XML (Simulation Mode):\n" + xml);
    });

    connect(dumpXMLAct, &QAction::triggered, this, []() {
        Project* project = Application::getInstance()->getProject();
        QString xml = project->generateMujocoXML(false);
        QDomDocument doc;
        doc.setContent(xml);
        xml=doc.toString(4);
        Log::info("Current Mujoco XML (Edit Mode):\n" + xml);
    });


    connect(rsDocsAct, &QAction::triggered, this, []() {
        QMessageBox::information(nullptr, "Robotics Studio Documentation",
            "For documentation and tutorials, please visit our GitHub repository:\n\n"
            "https://github.com/robotics-studio/robotics-studio"
        );
    });

    connect(docsAct, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://mujoco.org/book/index.html"));
    });

    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Robotics Studio",
            "Robotics Studio v1.0\n\n"
            "A robotics simulation and design tool built on top of MuJoCo.\n"
            "Developed by the Robotics Studio Team.\n\n"
            "For documentation and source code, visit our GitHub repository."
        );
    });

    connect(shortcutsAct, &QAction::triggered, this, [this]() {
        QString shortcutsInfo =
            "Keyboard Shortcuts:\n\n"
            "File Operations:\n"
            "  Ctrl+N: New Project\n"
            "  Ctrl+O: Open Project\n"
            "  Ctrl+S: Save Project\n"
            "  Ctrl+Shift+S: Save Project As\n"
            "  Ctrl+Q: Exit Application\n\n"
            "Edit Operations:\n"
            "  Ctrl+Z: Undo\n"
            "  Ctrl+Y: Redo\n"
            "  Ctrl+, : Preferences\n\n"
            "View Operations:\n"
            "  F: Frame Scene\n\n"
            "Simulation Controls:\n"
            "  Space: Play/Pause Simulation (when viewport is focused)\n";

        QMessageBox::information(this, "Keyboard Shortcuts", shortcutsInfo);
    });


}



/*
 * Wires the Edit-menu Undo/Redo actions to Application's undo stack.
 * Called from Application's constructor body -- at EditorWindow construction
 * time the Application singleton pointer is still null, so this cannot be
 * done inside setupMenuBar().
 */
void EditorWindow::setupUndoRedo() {
    QUndoStack* undoStack = Application::getInstance()->getUndoStack();

    // createUndoAction/createRedoAction give dynamic labels ("Undo Attach
    // component") and auto enable/disable based on stack state.
    QAction* undoAct = undoStack->createUndoAction(this, tr("&Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    QAction* redoAct = undoStack->createRedoAction(this, tr("&Redo"));
    redoAct->setShortcut(QKeySequence::Redo);

    QAction* firstItem = editMenu->actions().value(0);
    editMenu->insertAction(firstItem, undoAct);
    editMenu->insertAction(firstItem, redoAct);

    // Keep scene tree + inspector in sync after undo/redo/push -- but only
    // when the command actually changes the assembly. Value-only commands
    // (joint targets, robot rename) sync live through the physics loop, and
    // the widget that committed them already shows the new value; rebuilding
    // here would just steal focus (e.g. mid slider drag / spinbox typing).
    // Undoing a value-only command doesn't rebuild either -- the inspector
    // syncs the affected widgets in place instead. (A redo is
    // indistinguishable from a push at the signal level; redoing a value-only
    // command leaves the inspector stale until the next structural edit or
    // reselection -- accepted trade-off.)
    connect(undoStack, &QUndoStack::indexChanged, this, [this, undoStack](int idx) {
        bool wasUndo = idx < lastUndoIndex;
        const QUndoCommand* cmd = nullptr;
        if (wasUndo) cmd = undoStack->command(idx);          // the command just undone
        else if (idx > 0) cmd = undoStack->command(idx - 1); // pushed / merged / redone
        lastUndoIndex = idx;

        const auto* generic = dynamic_cast<const GenericCommand*>(cmd);
        bool structural = !generic || generic->affectsStructure();

        if (structural) {
            scheduleRefresh();
        } else if (wasUndo && inspector) {
            inspector->updateJointValues();
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
    bottomTabs->addTab(componentsPanel, "Components");

    // add ScriptPanel
    scriptPanel = new ScriptPanel();
    scriptPanel->setMinimumHeight(0);
    scriptPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    bottomTabs->addTab(scriptPanel, "Script");


    topDownSplitter->addWidget(bottomTabs);
    topDownSplitter->setCollapsible(0, false);
    topDownSplitter->setCollapsible(1, false);

    // 1. Tell Qt to give all extra space to the top viewport (index 0)
    // and keep the bottom panel (index 1) locked to its fixed size.
    topDownSplitter->setStretchFactor(0, 1);
    topDownSplitter->setStretchFactor(1, 0);

    // 2. Set the initial sizes directly (using a massive number forces the top to take the remainder)
    topDownSplitter->setSizes({9999, 250});

    connect(bottomTabs->tabBar(), &QTabBar::tabBarClicked, this, [this](int index){
        QList<int> sizes = topDownSplitter->sizes();
        int topSize    = sizes[0];
        int bottomSize = sizes[1];
        int tabHeight = bottomTabs->tabBar()->sizeHint().height();

        if(bottomSize <= tabHeight+10){
            // Use the same massive-number trick for a flawless 250px snap, avoiding pixel math
            topDownSplitter->setSizes({ 9999, 250 });
        }
        else{
            if(index==bottomTabs->currentIndex()){
                topDownSplitter->setSizes({ topSize+bottomSize-tabHeight, tabHeight });
            }
        }
    });

    bottomTabs->setCurrentIndex(0);
    // Removed: emit bottomTabs->tabBar()->tabBarClicked(0);
}








void EditorWindow::setupMainViewport() {
    viewport = new MViewport();

    QVBoxLayout* hudLayout = new QVBoxLayout(viewport);
    hudLayout->setContentsMargins(15, 15, 15, 15);

    QWidget* container = new QWidget(viewport);
    container->setStyleSheet("background-color: rgba(30, 30, 30, 150); padding: 5px; border-radius: 4px;");
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    fpsLabel = new QLabel("FPS: --", viewport);
    simTimeLabel = new QLabel("Sim Time: --", viewport);
    collisionCountLabel=new QLabel("Collisions: --", viewport);
    containerLayout->addWidget(simTimeLabel);
    containerLayout->addWidget(collisionCountLabel);
    containerLayout->addWidget(fpsLabel);

    hudLayout->addWidget(container, 0, Qt::AlignTop | Qt::AlignLeft);
    hudLayout->addStretch();

    playBtn = new QPushButton("▶ PLAY", viewport);
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setStyleSheet("background-color: #2E7D32; color: white; padding: 8px 15px; border-radius: 4px; font-size: 14px;");
    hudLayout->addWidget(playBtn, 0, Qt::AlignBottom | Qt::AlignLeft);

    Project* project = Application::getInstance()->getProject();
    auto reloadSimulation = [this](Project* project, SimulationManager* simManager, bool isSimulation) {
        std::lock_guard<std::mutex> lock(simManager->physicsMutex);

        simManager->getMujocoContext()->loadModelFromString(
            project->generateMujocoXML(isSimulation).toStdString()
        );
        simManager->cacheMujocoIds(
            project->getRootComponent(),
            simManager->getMujocoContext()->getModel()
        );
    };

    connect(playBtn, &QPushButton::clicked, this, [this, reloadSimulation]() {
        SimulationManager* sim = Application::getInstance()->getSimulationManager();

        if (sim->getState() == SimulationState::EDITING) {
            Project* project = Application::getInstance()->getProject();
            project->reloadScript();
            reloadSimulation(project, sim, true);

            if (this->plotPanel) {
                // this->plotPanel->clearAllGraphs();
            }

            sim->play();
            playBtn->setText("⬛ STOP");
            playBtn->setStyleSheet("background-color: #C62828; color: white; padding: 8px 15px; border-radius: 4px; font-weight: bold; font-size: 14px;");

            if (this->inspector) this->inspector->setInputState(false);
            if (this->componentsPanel) this->componentsPanel->setEnabled(false);

        } else {
            sim->edit();
            Project* project = Application::getInstance()->getProject();
            reloadSimulation(project, sim, false);

            frameScene();

            playBtn->setText("▶ PLAY");
            playBtn->setStyleSheet("background-color: #2E7D32; color: white; padding: 8px 15px; border-radius: 4px; font-weight: bold; font-size: 14px;");

            if (this->inspector) this->inspector->setInputState(true);
            if (this->componentsPanel) this->componentsPanel->setEnabled(true);
        }
    });


    topDownSplitter->addWidget(viewport);
}




/*
 * Camera + input binding for the main viewport. Deferred out of
 * setupMainViewport: the MujocoContext (and its mjvCamera) is owned by
 * SimulationManager, which doesn't exist yet when the EditorWindow is
 * first constructed -- called from setupSimConn() once a project is open.
 */
void EditorWindow::setupCameraControls() {
    if (camera) return; // one-time setup; the context outlives project reloads

    CameraController* controller = new OrbitCameraController();
    camera = new Camera(Application::getInstance()->getSimulationManager()->getMujocoContext()->getCamera());
    controller->setCamera(camera);
    camera->setPosition(Position(0.0, -0.20, 0.05));
    camera->setTarget(Position(0.0, 0.0, 0.0));

    InputManager &im = InputManager::getInstance();
    im.bind(viewport);
    im.onMouseButtonPress(
        [controller]
        (int x, int y){
            controller->onMouseDown(x, y);
        }
    );
    im.onCursorPos(
        [controller]
        (double x, double y){
            controller->onMouseMove(static_cast<int>(x), static_cast<int>(y));
        }
    );
    im.onMouseButtonRelease(
        [controller]
        (int x, int y){
            controller->onMouseUp();
        }
    );
    im.onScroll(
        [controller]
        (double xoffset, double yoffset){
            controller->onMouseScroll(yoffset);
        }
    );
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
    graphDock->setMinimumHeight(100);

    splitDockWidget(sceneDock, graphDock, Qt::Vertical);
    splitDockWidget(sceneDock, inspectorDock, Qt::Horizontal);



    QList<QDockWidget*> horizontalDocks = {sceneDock, inspectorDock};
    QList<QDockWidget*> verticalDocks = {sceneDock, graphDock};
    QList<int> horizontalSizes = {340, 400};
    QList<int> verticalSizes = {600, 350};
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
    SimulationManager* simManager = Application::getInstance()->getSimulationManager();
    if (!simManager) return;
    MujocoContext* mj = simManager->getMujocoContext();
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
