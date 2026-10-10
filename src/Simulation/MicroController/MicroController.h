#pragma once

#include <atomic>
#include <QJSEngine>
#include "Utils/Log.h"


class ComponentInstance;



class JSConsoleProxy : public QObject {
    Q_OBJECT
    QString tag;
    public:
        explicit JSConsoleProxy(const QString& tag, QObject* parent = nullptr) : QObject(parent), tag(tag) {}
        Q_INVOKABLE void log(const QString& msg) {
            Log::info("[" + tag + "]: " + msg);
        }
};

class MicroController : public QObject {
    Q_OBJECT
    private:
        QJSEngine engine;
        QJSValue loopFunction;
        JSConsoleProxy* consoleProxy;
        
        bool isCompiled = false;
        std::atomic<bool> stopRequested; 
        // While set, delay() holds and its remaining time stops counting down.
        std::atomic<bool> paused{false};
    
        void injectEmulators(ComponentInstance* comp);

    public:
        // tag prefixes console.log output ("MCU" for the robot, "STAGE" for the stage script)
        explicit MicroController(const QString& tag = "MCU", QObject* parent = nullptr);
        ~MicroController();

        // root may be null (stage script: its globals come from setGlobal)
        void compile(const QString& script, ComponentInstance* root);
        void setGlobal(const QString& name, QObject* obj);
        void run(); 
        void stop();
        void setPaused(bool p);

        // The native C++ sleep function exposed to JS
        Q_INVOKABLE void delay(int milliseconds); 
};