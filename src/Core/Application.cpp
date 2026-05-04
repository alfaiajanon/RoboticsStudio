#include "Application.h"
#include "Core/Log.h"
#include "Simulation/Components/LibraryManager.h"
#include <QFile>
#include <QSettings>
#include "UI/Dialogs/ModelDownloaderDialog.h"



#pragma region creation

Application* Application::init(int& argc, char** argv) {
    if (!instance) {
        instance = new Application(argc, argv);
    }
    return instance;
}

Application* Application::instance = nullptr;


Application* Application::getInstance() {
    return instance;
}

Application::~Application() {
}







#pragma region setup

Application::Application(int& argc, char** argv) : qtApp(argc, argv) {
    Application::instance = this;
    qtApp.setApplicationName("Robotics Studio");
    qtApp.setOrganizationName("Anon Engineering");
    
    this->loadStyle(":/styles/dark.qss");

    Log::info("Application initialized.");

    
    
    // LaucherWindow setup
    connect(&launcher, &LauncherWindow::projectOpened, this, &Application::openProject);
    
    connect(&launcher, &LauncherWindow::fetchModelsRequested, this, [this]() {
        Log::info("Fetch models requested via Launcher....");
        ModelDownloaderDialog dialog(nullptr);
        dialog.startSync(); 
        dialog.exec();
        Log::info("Models downloaded to: " + getModelsDirectory());
    });

    QSettings settings("RoboticsStudio", "RoboticsStudio");
    bool autoOpen = settings.value("autoOpenEnabled", false).toBool();
    QStringList recents = settings.value("recentProjects").toStringList();

    if (autoOpen && !recents.isEmpty()) {
        Log::info("Auto-open enabled. Skipping Launcher.");
        openProject(recents.first());
    } else {
        Log::info("Showing Launcher Hub.");
        launcher.show(); 
    }

}




void Application::createProject(const QString& projectPath) {
    // currentProject.createNewProject(projectPath);
    // openProject(projectPath);
}



void Application::openProject(const QString& projectPath) {
    launcher.hide();
    
    if (simManager) {
        simManager->pause();
        delete simManager;
    }

    QString catalogPath = getModelsDirectory() + "/Catalog.json";
    if (QFile::exists(catalogPath)) {
        Log::info("Loading from downloaded Catalog: " + catalogPath);
        LibraryManager::getInstance().load(catalogPath);
    } else {
        LibraryManager::getInstance().load("./models/Catalog.json");
    }

    currentProject.loadProject(projectPath);
    saveLastProject(projectPath);

    // make necessary simulation setup
    MujocoContext::getInstance()->loadModelFromString(
        currentProject.generateMujocoXML().toStdString()
    );
    
    editor.sceneTree->buildFromProject(&currentProject);
    editor.scriptPanel->loadScript(
        currentProject.getProjectDirectory() + "/" + currentProject.getScriptPath()
    );

    simManager = new SimulationManager();
    ComponentInstance* root = currentProject.getRootComponent();
    simManager->cacheMujocoIds(root, MujocoContext::getInstance()->getModel());
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









#pragma region Project Persistence

void Application::saveLastProject(const QString& path) {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    settings.setValue("lastOpenedProject", path);
}


QString Application::getLastProject() {
    QSettings settings("RoboticsStudio", "RoboticsStudio");
    return settings.value("lastOpenedProject", "").toString();
}


QString Application::getModelsDirectory() {
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    
    if (!dir.exists("models")) {
        dir.mkpath("models");
    }
    
    return dir.absoluteFilePath("models");
}






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










