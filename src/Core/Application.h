#pragma once

#include <QApplication>
#include "Project.h"
#include "UI/EditorWindow.h"
#include "Simulation/SimulationManager.h"

class Application {
    static Application* instance;
    
    QApplication qtApp;
    EditorWindow editor;
    SimulationManager* simManager;

    Application(int& argc, char** argv);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    public:
        Project currentProject;

        static Application* init(int& argc, char** argv);
        static Application* getInstance();
        static void destroy(); 

        Project* getProject();
        SimulationManager* getSimulationManager();
        EditorWindow* getEditor();

        void loadStyle(const QString& path);    
        int run();
};