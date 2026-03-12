#include "Application.h"
#include "Core/Log.h"
#include "Simulation/Components/LibraryManager.h"
#include <QFile>

Application* Application::instance = nullptr;




Application* Application::init(int& argc, char** argv) {
    if (!instance) {
        instance = new Application(argc, argv);
    }
    return instance;
}




Application* Application::getInstance() {
    return instance;
}




Project* Application::getProject() {
    return &currentProject;
}




EditorWindow* Application::getEditor() {
    return &editor; 
}





SimulationManager* Application::getSimulationManager() {
    return simManager;
}




Application::Application(int& argc, char** argv) : qtApp(argc, argv) {
    Application::instance = this;
    qtApp.setApplicationName("Robotics Studio");
    qtApp.setOrganizationName("Anon Engineering");
    
    this->loadStyle(":/styles/dark.qss");

    LibraryManager::getInstance().load("./models/Catalog.json");

    currentProject.loadProject("./demo.rsproj");

    MujocoContext::getInstance()->loadModelFromString(
        currentProject.generateMujocoXML().toStdString()
    );
    
    editor.sceneTree->buildFromRoot(currentProject.getRootComponent());

    simManager = new SimulationManager();
    
    ComponentInstance* root = currentProject.getRootComponent();
    simManager->cacheMujocoIds(root, MujocoContext::getInstance()->getModel());
    simManager->play();

    editor.show();
}




Application::~Application() {
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





/*
 * Safely destroys the singleton instance.
 * Ensures QApplication and all UI components are cleanly deallocated 
 * before the main thread tears down its local storage.
 */
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
