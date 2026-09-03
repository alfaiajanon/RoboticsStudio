#include "Application.h"

#include <QFile>
#include <QSettings>

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

    Log::info("Application initialized.");

    QSettings settings("RoboticsStudio", "RoboticsStudio");
    bool autoOpen = settings.value("autoOpenEnabled", false).toBool();
    QStringList recents = settings.value("recentProjects").toStringList();

    if (autoOpen && !recents.isEmpty()) {
        Log::info("Auto-open enabled. Skipping Launcher.");
        openProject(recents.first());
    } else {
        Log::info("Showing Launcher Hub.");
        connect(&launcher, &LauncherWindow::projectOpened, this, &Application::openProject);
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
        // if (QFile::exists(catalogPath)) {
        //     Log::info("Loading from downloaded Catalog: " + catalogPath);
        //     LibraryManager::getInstance().load(catalogPath);
        // } else {
        //     QMessageBox::critical(nullptr, "Error", "Download/Fetch the component library before opening a project.");
        //     return;
        // }
    }


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

    // Old commands point into the previous project's component tree --
    // undoing them now would be a use-after-free.
    undoStack.clear();

    // make necessary simulation setup
    {
        // The physics thread may still be live from a previously opened
        // project -- don't swap the model out from under it.
        std::lock_guard<std::mutex> lock(simManager->physicsMutex);
        simManager->getMujocoContext()->loadModelFromString(
            currentProject.generateMujocoXML().toStdString()
        );
    }

    editor.sceneTree->buildFromProject(&currentProject);
    editor.scriptPanel->loadScript(
        currentProject.getProjectDirectory() + "/" + currentProject.getScriptPath()
    );

    ComponentInstance* root = currentProject.getRootComponent();
    simManager->cacheMujocoIds(root, simManager->getMujocoContext()->getModel());
    simManager->edit();

    editor.setupSimConn();
    editor.frameScene();
    editor.refresh();
    editor.show();

    Log::info("Project loaded: " + projectPath);

    Log::info("================================");
    Log::info("  Welcome to Robotics Studio!  ");
    Log::info("  version 0.1.0                 ");
    Log::info("================================");

    editor.selectComponent(0);
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
    Project* project = getProject();

    QTimer::singleShot(0, this, [project]() {
        SimulationManager* simManager = Application::getInstance()->getSimulationManager();
        std::lock_guard<std::mutex> lock(simManager->physicsMutex);

        project->refresh();
        simManager->getMujocoContext()->loadModelFromString(
            project->generateMujocoXML().toStdString()
        );
        simManager->cacheMujocoIds(
            project->getRootComponent(),
            simManager->getMujocoContext()->getModel()
        );
        Application::getInstance()->getEditor()->refresh();
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
