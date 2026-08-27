#pragma once

#include <QApplication>
#include "Document/Project.h"
#include <QObject>
#include "View/Windows/EditorWindow.h"
#include "View/Windows/LauncherWindow.h"



class SimulationManager;




class Application : public QObject {
    Q_OBJECT

    static Application* instance;
    Application(int& argc, char** argv);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    QApplication qtApp;
    QUndoStack undoStack;
    LauncherWindow launcher;
    EditorWindow editor;
    SimulationManager* simManager=nullptr;

    public:
        Project currentProject;

        static Application* getInstance();
        static void init(int& argc, char** argv);
        static void destroy();

        void saveLastProject(const QString& path);
        QString getLastProject();
        // QString getModelsDirectory();

        void openProject(const QString& projectPath);
        void reloadSimulation();

        Project* getProject();
        SimulationManager* getSimulationManager();
        EditorWindow* getEditor();
        QUndoStack* getUndoStack() { return &undoStack; }

        void loadStyle(const QString& path);
        int run();
};
