#pragma once

#include <QApplication>
#include "Project.h"
#include <QObject>
#include "UI/EditorWindow.h"
#include  "UI/LauncherWindow.h"
#include "Simulation/SimulationManager.h"

class Application : public QObject {
    Q_OBJECT

    static Application* instance;
    
    QApplication qtApp;
    LauncherWindow launcher;
    EditorWindow editor;
    SimulationManager* simManager=nullptr;

    Application(int& argc, char** argv);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    public:
        Project currentProject;

        static Application* init(int& argc, char** argv);
        static Application* getInstance();
        static void destroy(); 

        void saveLastProject(const QString& path);
        QString getLastProject();
        QString getModelsDirectory();

        void createProject(const QString& projectPath);
        void openProject(const QString& projectPath);

        Project* getProject();
        SimulationManager* getSimulationManager();
        EditorWindow* getEditor();

        void loadStyle(const QString& path);    
        int run();
};