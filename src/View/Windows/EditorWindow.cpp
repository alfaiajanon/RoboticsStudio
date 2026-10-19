#include "EditorWindow.h"
#include "Simulation/Firmware/FirmwareCompiler.h"
#include "View/Widgets/CodeEditor.h"

#include <QDockWidget>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTabBar>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <qaction.h>
#include <QDomDocument>
#include <QUndoStack>
#include <QTimer>
#include <QCloseEvent>
#include <qjsondocument.h>
#include <qlabel.h>

#include "Application/Application.h"
#include "View/Widgets/PanelCorners.h"
#include "View/Theme.h"
#include "Commands/GenericCommand.h"
#include "Document/Components/ComponentBlueprint.h"
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
    // Same 3px gap around the outer panels as between them (menu bar included;
    // it has the window color, so only the panels show the inset)
    this->setContentsMargins(3, 3, 3, 3);

    topDownSplitter=nullptr;
    viewport=nullptr;
    centerStack=nullptr;
    bottomTabs=nullptr;
    camera=nullptr;
    stagePanel=nullptr;

    setupMenuBar();
    setupSplitting();
    setupRightDocking();

    connect(centerStack, &HudStack::currentPageChanged, this, [this](QWidget* page) {
        saveAct->setText(page == scriptPanel ? "Save Script" : "Save Project");
    });
    connect(scriptPanel, &ScriptPanel::modificationChanged, this, &EditorWindow::updateModifiedIndicator);
    // Plot edits emit nothing, so poll the cheap snapshot compare for the title
    // marker; the close prompt itself always compares fresh.
    QTimer* modifiedTimer = new QTimer(this);
    connect(modifiedTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) updateModifiedIndicator();
    });
    modifiedTimer->start(1000);
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
            fpsLabel->setStyleSheet("color: #e6e7e9;");
        }
        simTimeLabel->setText(QString("Sim Time: %1").arg(status.simTime));
        collisionCountLabel->setText(QString("Contacts: %1").arg(status.activeContacts));

        SimulationManager* sim = Application::getInstance()->getSimulationManager();
        if (sim->getState() != SimulationState::PLAYING) {
            rtfLabel->setText("Real-time: --");
            rtfLabel->setStyleSheet("color: #e6e7e9;");
        } else {
            rtfLabel->setText(QString("Real-time: %1x").arg(status.realTimeFactor, 0, 'f', 2));
            // Falling well behind the selected scale: the scripts or cameras
            // are heavier than the host can run at that speed.
            bool behind = status.timeScale > 0.0 && status.realTimeFactor < 0.9 * status.timeScale;
            rtfLabel->setStyleSheet(behind ? "color: #ff5555;" : "color: #e6e7e9;");
        }
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
    if(stagePanel){
        stagePanel->refresh();
    }
    if(scriptPanel){
        scriptPanel->refresh();
    }
    if (wiringPage) {
        Project* project = Application::getInstance()->getProject();
        const bool real = project && project->getLevel() == ProjectLevel::Real;
        centerStack->setPageVisible(wiringPage, real);
        if (real) wiringPage->refresh();
    }
    updateLevelBadge();
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




void EditorWindow::updateLevelBadge() {
    Project* project = Application::getInstance() ? Application::getInstance()->getProject() : nullptr;
    if (!levelBadge || !project) return;
    if (project->getLevel() == ProjectLevel::Prototype) {
        levelBadge->setText("Prototype · ideal board");
        return;
    }
    const QList<ComponentInstance*> boards = project->boards();
    if (boards.isEmpty()) levelBadge->setText("Real · no board");
    else if (boards.size() == 1) levelBadge->setText("Real · " + boards.first()->name);
    else levelBadge->setText(QString("Real · %1 boards").arg(boards.size()));
}



int EditorWindow::getCurrentSelectedUid() const {
    return currentSelectedUid;
}






#pragma region Save / unsaved changes

bool EditorWindow::saveProject() {
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

    if (!proj->saveProject()) return false;
    markProjectSaved();
    return true;
}


void EditorWindow::markProjectSaved() {
    Application::getInstance()->getUndoStack()->setClean();
    savedPlotLayout = plotPanel ? plotPanel->toJson() : QJsonObject();
    updateModifiedIndicator();
}


bool EditorWindow::isProjectModified() const {
    Application* app = Application::getInstance();
    if (!app) return false;
    if (!app->getUndoStack()->isClean()) return true;
    return plotPanel && plotPanel->toJson() != savedPlotLayout;
}


bool EditorWindow::confirmUnsavedChanges() {
    const bool projectDirty = isProjectModified();
    const bool scriptDirty = scriptPanel && scriptPanel->hasUnsavedChanges();
    if (!projectDirty && !scriptDirty) return true;

    QStringList dirty;
    if (projectDirty) dirty << "the project";
    if (scriptDirty) dirty << QFileInfo(scriptPanel->currentPath()).fileName();

    QMessageBox::StandardButton choice = QMessageBox::question(
        this, "Unsaved Changes",
        "There are unsaved changes in " + dirty.join(" and ") + ". Save them first?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;

    if (scriptDirty && !scriptPanel->saveScript()) {
        QMessageBox::warning(this, "Save Failed",
            "Could not save " + QFileInfo(scriptPanel->currentPath()).fileName() + ".");
        return false;
    }
    if (projectDirty && !saveProject()) {
        QMessageBox::warning(this, "Save Failed",
            "Could not save the project. Use Save As... first, or choose Discard.");
        return false;
    }
    return true;
}


void EditorWindow::updateModifiedIndicator() {
    Application* app = Application::getInstance();
    if (!app) return;
    QString name = QFileInfo(app->getProject()->getProjectPath()).completeBaseName();
    setWindowTitle(name.isEmpty() ? "Robotics Studio[*]" : name + "[*] - Robotics Studio");
    setWindowModified(isProjectModified() || (scriptPanel && scriptPanel->hasUnsavedChanges()));
}


// Window X, Project > Exit and Ctrl+Q all land here.
void EditorWindow::closeEvent(QCloseEvent* event) {
    if (!confirmUnsavedChanges()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
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
    saveAct = projectMenu->addAction("Save Project");
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
    QAction* returnToLauncherAct = projectMenu->addAction("Return to Launcher");
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
    viewMenu->addSeparator();
    QAction* showViewportAct = viewMenu->addAction("3D View");
    showViewportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    QAction* showScriptAct = viewMenu->addAction("Script");
    showScriptAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    QAction* showWiringAct = viewMenu->addAction("Wiring (Real level)");
    showWiringAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));

    QMenu* debugMenu = topMenu->addMenu("Debug");
    QAction* dumpXMLAct = debugMenu->addAction("Dump Mujoco XML (Edit Mode)");
    QAction* dumpXMLActSim = debugMenu->addAction("Dump Mujoco XML (Simulation Mode)");

    QMenu* helpMenu = topMenu->addMenu("Help");
    QAction* shortcutsAct = helpMenu->addAction("Keyboard Shortcuts");
    QAction* docsAct = helpMenu->addAction("MuJoCo Documentation");
    helpMenu->addSeparator();
    QAction* aboutAct = helpMenu->addAction("About RoboticsStudio");

    // signals
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    connect(returnToLauncherAct, &QAction::triggered, this, []() {
        Application::getInstance()->closeProjectToLauncher();
    });
    connect(frameSceneAct, &QAction::triggered, this, &EditorWindow::frameScene);
    connect(showViewportAct, &QAction::triggered, this, [this]() {
        if (centerStack) centerStack->setCurrentPage(viewport);
    });
    connect(showScriptAct, &QAction::triggered, this, [this]() {
        if (centerStack) centerStack->setCurrentPage(scriptPanel);
    });
    connect(showWiringAct, &QAction::triggered, this, [this]() {
        Project* project = Application::getInstance()->getProject();
        if (!centerStack || !wiringPage || !project) return;
        if (project->getLevel() != ProjectLevel::Real) {
            Toast::showMessage(this, "Wiring is part of the Real level (project settings: Level)", 2500);
            return;
        }
        centerStack->setCurrentPage(wiringPage);
    });

    connect(newAct, &QAction::triggered, this, [this]() {
        if (!confirmUnsavedChanges()) return;
        NewProjectDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            QString newPath = dialog.getCreatedProjectPath();

            Application::getInstance()->openProject(newPath);
        }
    });

    connect(openAct, &QAction::triggered, this, [this]() {
        if (!confirmUnsavedChanges()) return;
        QString path = QFileDialog::getOpenFileName(this, "Open Project", "", "Robotics Studio Project (*.rsproj);;All Files (*)");
        if (!path.isEmpty()) {
            Application::getInstance()->openProject(path);
        }
    });

    connect(saveAct, &QAction::triggered, this, [this, saveAsAct]() {
        // On the Script page Ctrl+S saves the open script, like a code editor.
        if (centerStack && centerStack->currentPage() == scriptPanel && !scriptPanel->currentPath().isEmpty()) {
            scriptPanel->saveScript();   // toasts on its own
            return;
        }

        if (saveProject()) {
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
            if (saveProject()) Toast::showMessage(this, "Project saved successfully!");
            else Toast::showMessage(this, "Failed to save project.", 3000);
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
        if(!data.isValid){
            Log::error("Embedded servo template is invalid: " + data.errorString);
            return;
        }
        data.modelId.clear();   // a template seeds a NEW component; an empty id is what makes the editor treat it as one
        ComponentEditorWindow* dialog = new ComponentEditorWindow(data,this);
        dialog->show();
    });

    connect(blankTemplateAct, &QAction::triggered, this, [this]() {
        ComponentData data;
        ComponentEditorWindow* dialog = new ComponentEditorWindow(data,this);
        dialog->show();
    });

    connect(editComponentAct, &QAction::triggered, this, [this]() {
        LibraryManager& lib = LibraryManager::getInstance();
        QList<ComponentBlueprint*> customComponents = lib.getBlueprints("custom");
        if (customComponents.isEmpty()) {
            Log::error("No component found");
            return;
        }

        QDialog picker(this);
        picker.setWindowTitle("Edit Component");
        picker.setMinimumWidth(360);
        QVBoxLayout* layout = new QVBoxLayout(&picker);
        layout->addWidget(new QLabel("Select a component:"));
        QListWidget* list = new QListWidget();
        for (ComponentBlueprint* bp : customComponents) {
            if (!bp) continue;
            QListWidgetItem* item = new QListWidgetItem(bp->getModelId());
            item->setData(Qt::UserRole, bp->getModelId());
            list->addItem(item);
        }
        list->setCurrentRow(0);
        layout->addWidget(list);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* deleteBtn = new QPushButton("Delete");
        QPushButton* cancelBtn = new QPushButton("Cancel");
        QPushButton* editBtn = new QPushButton("Edit");
        editBtn->setDefault(true);
        btnLayout->addWidget(deleteBtn);
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(editBtn);
        layout->addLayout(btnLayout);

        QString chosenId;
        connect(cancelBtn, &QPushButton::clicked, &picker, &QDialog::reject);
        connect(editBtn, &QPushButton::clicked, &picker, [&]() {
            if (!list->currentItem()) return;
            chosenId = list->currentItem()->data(Qt::UserRole).toString();
            picker.accept();
        });
        connect(list, &QListWidget::itemDoubleClicked, editBtn, &QPushButton::click);
        connect(deleteBtn, &QPushButton::clicked, &picker, [&]() {
            QListWidgetItem* item = list->currentItem();
            if (!item) return;
            const QString id = item->data(Qt::UserRole).toString();

            Project* project = Application::getInstance()->getProject();
            for (ComponentInstance* inst : project->getComponentMap()) {
                if (inst && inst->modelId == id) {
                    QMessageBox::warning(&picker, "Cannot Delete",
                        "'" + id + "' is used by a component in this project. Remove those first.");
                    return;
                }
            }
            if (QMessageBox::question(&picker, "Delete Component",
                    "Permanently delete '" + id + "' and its folder?") != QMessageBox::Yes)
                return;
            if (LibraryManager::getInstance().deleteLocalComponent(id)) {
                delete list->takeItem(list->row(item));
                if (list->count() == 0) picker.reject();
            }
        });

        if (picker.exec() != QDialog::Accepted || chosenId.isEmpty()) return;
        ComponentBlueprint* bp = lib.getBlueprint(chosenId);
        if (!bp) return;
        ComponentData compData = (ComponentData)(*bp);
        ComponentEditorWindow* dialog = new ComponentEditorWindow(compData, this);
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
            "  Ctrl+S: Save Script (on the Script page) / Save Project\n"
            "  Ctrl+Shift+S: Save Project As\n"
            "  Ctrl+Q: Exit Application\n\n"
            "Edit Operations:\n"
            "  Ctrl+Z: Undo\n"
            "  Ctrl+Y: Redo\n"
            "  Ctrl+, : Preferences\n\n"
            "View Operations:\n"
            "  F: Frame Scene\n"
            "  Ctrl+1: 3D View\n"
            "  Ctrl+2: Script\n\n"
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

    connect(undoStack, &QUndoStack::cleanChanged, this, &EditorWindow::updateModifiedIndicator);

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
 * Top: the HUD-switched page stack (3D viewport, script editor).
 * Bottom: the collapsible tab panels.
 */
void EditorWindow::setupSplitting(){
    this->topDownSplitter = new QSplitter(Qt::Vertical);
    setCentralWidget(topDownSplitter);

    centerStack = new HudStack();
    topDownSplitter->addWidget(centerStack);

    setupMainViewport();

    // ScriptPanel lives in the central stack, not the bottom tabs
    scriptPanel = new ScriptPanel();
    centerStack->addPage(scriptPanel, Theme::icon(":/icons/code.svg", Theme::accentLight), "Script (Ctrl+2)");

    // The schematic of the Real level's electronics (hidden at Prototype).
    wiringPage = new WiringPage();
    centerStack->addPage(wiringPage, Theme::icon(":/icons/wire.svg", Theme::accentLight), "Wiring (Ctrl+3)");
    centerStack->setPageVisible(wiringPage, false);
    connect(wiringPage, &WiringPage::componentSelected, this, &EditorWindow::selectComponent);

    bottomTabs = new QTabWidget();
    bottomTabs->setMinimumHeight(bottomTabs->tabBar()->sizeHint().height());
    bottomTabs->setMovable(true);
    bottomTabs->setTabsClosable(false);

    // add OutputPanel
    outputPanel = new OutputPanel();
    outputPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    outputPanel->setMinimumHeight(0);
    bottomTabs->addTab(outputPanel, Theme::icon(":/icons/terminal.svg"), "Output");
    connect(LogDispatcher::getInstance(), &LogDispatcher::messageLogged,
            outputPanel, &OutputPanel::appendMessage);

    // add ComponentsPanel
    componentsPanel = new ComponentsPanel();
    componentsPanel->setMinimumHeight(0);
    componentsPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    connect(&LibraryManager::getInstance(), &LibraryManager::catalogLoaded,
            componentsPanel, &ComponentsPanel::loadCatalog);
    bottomTabs->addTab(componentsPanel, Theme::icon(":/icons/chip.svg"), "Components");

    // add StagePanel
    stagePanel = new StagePanel();
    stagePanel->setMinimumHeight(0);
    stagePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    bottomTabs->addTab(stagePanel, Theme::icon(":/icons/stage.svg"), "Stage");


    topDownSplitter->addWidget(bottomTabs);
    PanelCorners::install(centerStack);
    PanelCorners::install(bottomTabs);
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

    // One panel for all stats: the style is scoped to the container, otherwise
    // it cascades and every label paints its own box
    QWidget* container = new QWidget(viewport);
    container->setObjectName("statusHud");
    container->setAttribute(Qt::WA_StyledBackground);
    container->setStyleSheet(R"(
        #statusHud { background-color: rgba(39, 40, 43, 220); border: 1px solid #101113; border-radius: 6px; }
        #statusHud QLabel { background: transparent; border: none; padding: 0px; }
    )");
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(10, 6, 10, 6);
    containerLayout->setSpacing(2);

    fpsLabel = new QLabel("FPS: --", viewport);
    simTimeLabel = new QLabel("Sim Time: --", viewport);
    collisionCountLabel=new QLabel("Collisions: --", viewport);
    rtfLabel = new QLabel("Real-time: --", viewport);
    containerLayout->addWidget(simTimeLabel);
    containerLayout->addWidget(rtfLabel);
    containerLayout->addWidget(collisionCountLabel);
    containerLayout->addWidget(fpsLabel);

    hudLayout->addWidget(container, 0, Qt::AlignTop | Qt::AlignLeft);

    // The project's level; a click opens the project settings, where it is set.
    levelBadge = new QPushButton(viewport);
    levelBadge->setObjectName("levelBadge");
    levelBadge->setCursor(Qt::PointingHandCursor);
    levelBadge->setFocusPolicy(Qt::NoFocus);
    levelBadge->setToolTip("Project level: click to change it in the project settings");
    levelBadge->setStyleSheet(R"(
        #levelBadge { background-color: rgba(39, 40, 43, 220); border: 1px solid #101113; border-radius: 6px;
                      padding: 4px 10px; color: #a6a8ae; text-align: left; }
        #levelBadge:hover { background-color: rgba(49, 50, 54, 240); color: #e6e7e9; }
    )");
    connect(levelBadge, &QPushButton::clicked, this, [this]() {
        currentSelectedUid = -2;   // force the Inspector to rebuild even if the project settings were shown
        selectComponent(0);
    });
    hudLayout->addSpacing(6);
    hudLayout->addWidget(levelBadge, 0, Qt::AlignTop | Qt::AlignLeft);
    hudLayout->addStretch();

    centerStack->addPage(viewport, Theme::icon(":/icons/cube.svg", Theme::accentLight), "3D View (Ctrl+1)");

    // Sim controls belong to the master HUD: visible on every central page.
    // [PLAY] while editing, [PAUSE/RESUME][STOP] while running.
    // Neutral HUD-panel buttons; only the icon carries a (muted) state color.
    // The icon is a single fixed tint so it keeps its color on hover.
    static const QColor playColor {0x5c, 0xb8, 0x7a};
    static const QColor pauseColor{0xd9, 0xa4, 0x41};
    static const QColor stopColor {0xe0, 0x6c, 0x6c};
    // QPushButton offers no icon-text gap or per-part alignment, so the icon and
    // text are labels in the button's own layout (transparent to the mouse).
    auto setHudBtn = [this](QPushButton* btn, const QString& text, const char* iconPath, const QColor& color) {
        btn->findChild<QLabel*>("hudBtnIcon")->setPixmap(Theme::pixmap(iconPath, 14, color, devicePixelRatioF()));
        btn->findChild<QLabel*>("hudBtnText")->setText(text);
    };
    auto makeHudBtn = [](const QString& name) {
        QPushButton* btn = new QPushButton();
        btn->setObjectName(name);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(QString(R"(
            #%1 { background-color: rgba(39, 40, 43, 230); border: 1px solid #101113; border-radius: 6px; padding: 0px; }
            #%1:hover { background-color: rgba(49, 50, 54, 240); }
            #%1:pressed { background-color: rgba(31, 32, 35, 240); }
            #%1 QLabel { background: transparent; border: none; padding: 0px; color: #e6e7e9; font-weight: 600; }
        )").arg(name));

        // QPushButton's own sizeHint ignores the layout: the layout sizes the
        // button instead (and re-sizes it when the text changes)
        QHBoxLayout* layout = new QHBoxLayout(btn);
        layout->setSizeConstraint(QLayout::SetFixedSize);
        layout->setContentsMargins(12, 8, 15, 8);
        layout->setSpacing(9);

        QLabel* icon = new QLabel(btn);
        icon->setObjectName("hudBtnIcon");
        icon->setFixedSize(16, 16);
        icon->setAlignment(Qt::AlignCenter);
        QLabel* label = new QLabel(btn);
        label->setObjectName("hudBtnText");
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        for (QLabel* l : {icon, label}) l->setAttribute(Qt::WA_TransparentForMouseEvents);

        layout->addWidget(icon, 0, Qt::AlignVCenter);
        layout->addWidget(label, 0, Qt::AlignVCenter);
        return btn;
    };

    QWidget* simControls = new QWidget();
    QHBoxLayout* simControlsLayout = new QHBoxLayout(simControls);
    simControlsLayout->setContentsMargins(0, 0, 0, 0);
    simControlsLayout->setSpacing(6);

    // Time scale: sim seconds per wall second. Sim time is the only clock, so
    // this changes only how fast a run plays, never what the scripts see.
    QComboBox* scaleCombo = new QComboBox();
    scaleCombo->setObjectName("timeScaleCombo");
    scaleCombo->setFocusPolicy(Qt::NoFocus);
    scaleCombo->setCursor(Qt::PointingHandCursor);
    scaleCombo->setToolTip("Simulation speed. Max runs as fast as the computer allows.");
    scaleCombo->setStyleSheet(R"(
        #timeScaleCombo { background-color: rgba(39, 40, 43, 230); border: 1px solid #101113; border-radius: 6px;
                          padding: 7px 10px; color: #e6e7e9; font-weight: 600; }
        #timeScaleCombo:hover { background-color: rgba(49, 50, 54, 240); }
    )");
    for (auto [label, scale] : {std::pair{"0.25x", 0.25f}, {"0.5x", 0.5f}, {"1x", 1.0f}, {"2x", 2.0f}, {"Max", 0.0f}}) {
        scaleCombo->addItem(label, scale);
    }
    scaleCombo->setCurrentIndex(2);
    connect(scaleCombo, &QComboBox::currentIndexChanged, this, [scaleCombo](int index) {
        Application::getInstance()->getSimulationManager()->setTimeScale(scaleCombo->itemData(index).toFloat());
    });
    simControlsLayout->addWidget(scaleCombo);

    pauseBtn = makeHudBtn("pauseSimBtn");
    setHudBtn(pauseBtn, "Pause", ":/icons/pause.svg", pauseColor);
    pauseBtn->hide();
    simControlsLayout->addWidget(pauseBtn);

    playBtn = makeHudBtn("playSimBtn");
    setHudBtn(playBtn, "Play", ":/icons/play.svg", playColor);
    simControlsLayout->addWidget(playBtn);

    centerStack->addHudControl(simControls);

    connect(pauseBtn, &QPushButton::clicked, this, [this, setHudBtn]() {
        SimulationManager* sim = Application::getInstance()->getSimulationManager();
        if (sim->getState() == SimulationState::PLAYING) {
            sim->pause();
            setHudBtn(pauseBtn, "Resume", ":/icons/play.svg", playColor);
        } else if (sim->getState() == SimulationState::PAUSED) {
            sim->resume();
            setHudBtn(pauseBtn, "Pause", ":/icons/pause.svg", pauseColor);
        }
    });

    Project* project = Application::getInstance()->getProject();
    auto reloadSimulation = [this](Project* project, SimulationManager* simManager, bool isSimulation) {
        simManager->reloadModel(project->generateMujocoXML(isSimulation), project->getRootComponent());
    };

    firmwareCompiler = new FirmwareCompiler(this);

    // Start the run (the script, or the compiled firmware, is ready).
    auto startRun = [this, reloadSimulation, setHudBtn]() {
        SimulationManager* sim = Application::getInstance()->getSimulationManager();
        Project* project = Application::getInstance()->getProject();
        centerStack->setCurrentPage(viewport);
        reloadSimulation(project, sim, true);

        sim->play();
        setHudBtn(playBtn, "Stop", ":/icons/stop.svg", stopColor);
        setHudBtn(pauseBtn, "Pause", ":/icons/pause.svg", pauseColor);
        pauseBtn->show();

        if (this->inspector) this->inspector->setInputState(false);
        if (this->componentsPanel) this->componentsPanel->setEnabled(false);
        if (this->stagePanel) this->stagePanel->setEnabled(false);
        scriptPanel->setCreationEnabled(false);
        if (wiringPage) wiringPage->setEditable(false);
    };

    // A C++ sketch is compiled first; the run starts only if that succeeds.
    connect(firmwareCompiler, &FirmwareCompiler::finished, this, [this, startRun, setHudBtn](bool ok) {
        playBtn->setEnabled(true);
        setHudBtn(playBtn, "Play", ":/icons/play.svg", playColor);
        SimulationManager* sim = Application::getInstance()->getSimulationManager();
        if (sim->getState() != SimulationState::EDITING) return;

        // The sketch's own errors and warnings, as markers in the script editor.
        Project* project = Application::getInstance()->getProject();
        QList<LineMarker> markers;
        for (const FirmwareCompiler::Diagnostic& d : firmwareCompiler->diagnostics()) {
            if (d.inSketch && d.severity != "note") markers.append({d.line, d.severity == "error", d.message});
        }
        const QString sketchPath = project->getProjectDirectory() + "/" + project->activeSketchPath();
        if (!ok) {
            // Show the sketch at its first error (opening it if another script is shown).
            centerStack->setCurrentPage(scriptPanel);
            if (QFileInfo(scriptPanel->currentPath()) != QFileInfo(sketchPath) && scriptPanel->confirmLeaveUnsaved())
                scriptPanel->loadScript(sketchPath);
            if (QFileInfo(scriptPanel->currentPath()) == QFileInfo(sketchPath)) scriptPanel->showDiagnostics(markers, true);
            Toast::showMessage(this, "Firmware didn't compile: see the marked lines and the Output panel", 3000);
            return;
        }
        if (QFileInfo(scriptPanel->currentPath()) == QFileInfo(sketchPath)) scriptPanel->showDiagnostics(markers, false);
        Application::getInstance()->getProject()->setFirmwareBinary(firmwareCompiler->wasm());
        startRun();
    });

    connect(playBtn, &QPushButton::clicked, this, [this, startRun, reloadSimulation, setHudBtn]() {
        SimulationManager* sim = Application::getInstance()->getSimulationManager();

        if (sim->getState() == SimulationState::EDITING) {
            if (firmwareCompiler->isBusy()) return;
            // The run reads the script from disk: flush pending editor edits
            if (scriptPanel->hasUnsavedChanges()) scriptPanel->saveScript();

            Project* project = Application::getInstance()->getProject();
            if (project->hasScriptAt(project->getCurrentScriptIdx())) project->reloadScript();
            // The active sketch: the ideal board's (Prototype) or the active
            // board's slot (Real), with that board's robot_pins.h.
            if (project->usesFirmware()) {
                project->writeBoardHeader();
                setHudBtn(playBtn, "Compiling...", ":/icons/play.svg", playColor);
                playBtn->setEnabled(false);
                firmwareCompiler->compile(project->activeSketchPath(), project->readActiveSketch(), project->pinsHeader());
                return;
            }
            startRun();

        } else {
            sim->edit();
            Project* project = Application::getInstance()->getProject();
            reloadSimulation(project, sim, false);

            frameScene();

            setHudBtn(playBtn, "Play", ":/icons/play.svg", playColor);
            pauseBtn->hide();

            if (this->inspector) this->inspector->setInputState(true);
            if (this->componentsPanel) this->componentsPanel->setEnabled(true);
            if (this->stagePanel) this->stagePanel->setEnabled(true);
            scriptPanel->setCreationEnabled(true);
            if (wiringPage) wiringPage->setEditable(true);
        }
    });

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

    PanelCorners::install(sceneDock);
    PanelCorners::install(inspectorDock);
    PanelCorners::install(graphDock);

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
    if (wiringPage) wiringPage->setSelectedUid(uid);
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
        // Skip the infinite ground plane, and the rest of the stage (floor
        // meshes, props): frame the robot, not the world around it.
        if (m->geom_type[i] == mjGEOM_PLANE) continue;
        const char* geomName = mj_id2name(m, mjOBJ_GEOM, i);
        if (geomName && (strncmp(geomName, "stage_", 6) == 0 || strncmp(geomName, "prop_", 5) == 0)) continue;

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
