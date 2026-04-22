#include "Application.h"
#include "Core/Log.h"
#include "Simulation/Components/LibraryManager.h"
#include <QFile>




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
    Log::info("Loading component catalog...");

    LibraryManager::getInstance().load("./models/Catalog.json");

    Log::info("Loading project...");
    QString lastProjectPath = loadLastProject();
    if (lastProjectPath.isEmpty()) {
        saveLastProject("./demo/demo1.rsproj");
        lastProjectPath = "./demo/demo1.rsproj";
        Log::info("No last project found, loading default demo.");
    }
    currentProject.loadProject(lastProjectPath);

    MujocoContext::getInstance()->loadModelFromString(
        currentProject.generateMujocoXML().toStdString()
    );
    
    editor.sceneTree->buildFromProject(&currentProject);
    editor.scriptPanel->loadScript(currentProject.getScriptPath());

    simManager = new SimulationManager();
    
    ComponentInstance* root = currentProject.getRootComponent();
    simManager->cacheMujocoIds(root, MujocoContext::getInstance()->getModel());
    simManager->edit();

    editor.setupSimConn();
    editor.frameScene();
    editor.show();


    Log::info("Application setup complete.");
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


QString Application::loadLastProject() {
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










