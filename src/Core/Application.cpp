#include "Application.h"
#include "UI/MainWindow.h"
#include "Core/Log.h"
#include "Simulation/Components/LibraryManager.h"
#include <QFile>

Application::Application(int& argc, char** argv) : qtApp(argc, argv){
    qtApp.setApplicationName("Robotics Studio");
    qtApp.setOrganizationName("Anon Engineering");
    
    this->loadStyle(":/styles/dark.qss");

    LibraryManager::getInstance().load("./models/Catalog.json");

    currentProject.loadProject("./demo.rsproj");

    mainWindow=new MainWindow();
    // Log::enableInAppLogging(true);
    mainWindow->sceneTree->buildFromRoot(currentProject.getRootComponent());
    mainWindow->show();
}


Application::~Application(){
    if(mainWindow){
        delete mainWindow;
        mainWindow = nullptr;
    }
}

void Application::loadStyle(const QString& path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Failed to load style:" << path;
        return;
    }
    QString style = file.readAll();
    qApp->setStyleSheet(style);
}

int Application::run(){
    return qtApp.exec();
}