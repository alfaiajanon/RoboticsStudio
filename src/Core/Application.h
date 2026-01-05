#pragma once

#include <QApplication>
#include "Project.h"

class MainWindow;

class Application{
    QApplication qtApp;
    MainWindow* mainWindow = nullptr;
    
    public:
        Project currentProject;
        
        Application(int& argc, char** argv);
        ~Application();

        void loadStyle(const QString& path);    
        int run();
};