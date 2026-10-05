#include "Application.h"

#include <QFile>
#include <QSettings>
#include <QMessageBox>

#include "Utils/Log.h"
#include "Document/Components/LibraryManager.h"
#include "Simulation/MujocoContext.h"
#include "Simulation/SimulationManager.h"
#include "View/Windows/EditorWindow.h"
#include "View/Panels/SceneTreePanel.h"



#pragma region creation


Application* Application::instance = nullptr;


Application* Application::getInstance() {
    return instance;
}


void Application::init(int& argc, char** argv) {
    if (!instance) {
        new Application(argc, argv);
    }
}




Application::Application(int& argc, char** argv) : qtApp(argc, argv) {
    Application::instance = this;
    qtApp.setApplicationName("Robotics Studio");
    qtApp.setOrganizationName("Anon Engineering");

    this->loadStyle(":/styles/dark.qss");

    // Safe only now that Application::instance is assigned.
    editor.setupUndoRedo();

    // The plot layout lives in the view; Project asks for a snapshot on every save.
    currentProject.setPlottingProvider([this]() {
        return editor.plotPanel ? editor.plotPanel->toJson() : QJsonObject();
    });

    Log::info("Application initialized.");

    QSettings settings("RoboticsStudio", "RoboticsStudio");
    bool autoOpen = settings.value("autoOpenEnabled", false).toBool();
    QStringList recents = settings.value("recentProjects").toStringList();

    // Always connected: "Return to Launcher" can show it even when auto-open skipped it at startup.
    connect(&launcher, &LauncherWindow::projectOpened, this, &Application::openProject);

    if (autoOpen && !recents.isEmpty()) {
        Log::info("Auto-open enabled. Skipping Launcher.");
        openProject(recents.first());
    } else {
        Log::info("Showing Launcher Hub.");
        launcher.show();
    }
}








#pragma region setup

void Application::openProject(const QString& projectPath) {
    bool dev_flag = true;
    // QString catalogPath = getModelsDirectory() + "/Catalog.json";

    if(dev_flag){
        LibraryManager::getInstance().load("/home/anon/Documents/Code Projects/Mixed Projects/RoboticsStudio/models");
    }else{
        bool success = LibraryManager::getInstance().load();
        if (!success) {
            QMessageBox::critical(nullptr, "Error", "Download/Fetch the component library before opening a project.");
            return;
        }
    }
    LibraryManager::getInstance().loadLocalComponents(QFileInfo(projectPath).absolutePath());


    launcher.hide();

    // One SimulationManager for the whole app lifetime: it owns the
    // MujocoContext, and the viewport camera holds a pointer into that
    // context -- recreating the manager per project would dangle it.
    if (!simManager) {
        simManager = new SimulationManager(&currentProject);
    }
    simManager->pause();

    currentProject.loadProject(projectPath);
    saveLastProject(projectPath);


    undoStack.clear();

    // The physics thread may still be live from a previously opened
    // project -- reloadModel takes physicsMutex for the swap.
    simManager->reloadModel(currentProject.generateMujocoXML(), currentProject.getRootComponent());

    editor.sceneTree->buildFromProject(&currentProject);
    editor.scriptPanel->loadScript(
        currentProject.getProjectDirectory() + "/" + currentProject.getScriptPath()
    );

    simManager->edit();

    editor.setupSimConn();
    editor.frameScene();
    editor.refresh();
    editor.plotPanel->loadJson(currentProject.getPlotting());
    editor.show();

    Log::info("Project loaded: " + projectPath);

    Log::info("================================");
    Log::info("  Welcome to Robotics Studio!  ");
    Log::info("  version 0.1.0                 ");
    Log::info("================================");

    editor.selectComponent(0);
}




bool Application::closeProjectToLauncher() {
    if (!undoStack.isClean()) {
        QMessageBox::StandardButton choice = QMessageBox::question(
            &editor, "Unsaved Changes",
            "Save changes to the project before closing it?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return false;
        if (choice == QMessageBox::Save && !currentProject.saveProject()) {
            QMessageBox::warning(&editor, "Save Failed",
                "Could not save the project. Use Save As... first, or choose Discard.");
            return false;
        }
    }

    if (simManager) simManager->pause();
    undoStack.clear();

    editor.hide();
    launcher.show();   // showEvent reloads the recent projects list
    launcher.raise();
    launcher.activateWindow();
    Log::info("Project closed, returned to launcher.");
    return true;
}




void Application::destroy() {
    if (instance) {
        if (instance->simManager) {
            delete instance->simManager;
            instance->simManager = nullptr;
        }
        delete instance;
        instance = nullptr;
    }
}




void Application::loadStyle(const QString& path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Failed to load style:" << path;
        return;
    }
    QString style = file.readAll();
    qApp->setStyleSheet(style);
}




int Application::run() {
    return qtApp.exec();
}





void Application::reloadSimulation() {
    if (reloadScheduled) return;
    reloadScheduled = true;

    Project* project = getProject();

    QTimer::singleShot(0, this, [this, project]() {
        reloadScheduled = false;

        SimulationManager* simManager = Application::getInstance()->getSimulationManager();

        project->refresh();
        simManager->reloadModel(project->generateMujocoXML(), project->getRootComponent());
        // Coalesced: the command that triggered this reload also fires the
        // undo stack's indexChanged -> scheduleRefresh() in the same turn.
        Application::getInstance()->getEditor()->scheduleRefresh();
    });
}







#pragma region Project Persistence

void Application::saveLastProject(const QString& path) {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    settings.setValue("lastOpenedProject", path);
}


QString Application::getLastProject() {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    return settings.value("lastOpenedProject", "").toString();
}


// QString Application::getModelsDirectory() {
//     QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
//     QDir dir(dataPath);

//     if (!dir.exists("models")) {
//         dir.mkpath("models");
//     }

//     return dir.absoluteFilePath("models");
// }






#pragma region Getters


Project* Application::getProject() {
    return &currentProject;
}


EditorWindow* Application::getEditor() {
    return &editor;
}


SimulationManager* Application::getSimulationManager() {
    return simManager;
}
