#pragma once

#include <QJSEngine>
#include <QString>
#include <atomic>
#include "Simulation/Components/ComponentInstance.h"
#include "Core/Log.h"

class JSConsoleProxy : public QObject {
    Q_OBJECT
    public:
        explicit JSConsoleProxy(QObject* parent = nullptr) : QObject(parent) {}
        Q_INVOKABLE void log(const QString& msg) {
            Log::info(msg); 
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
    
        void injectEmulators(ComponentInstance* comp);

    public:
        MicroController(QObject* parent = nullptr);
        ~MicroController();

        void compile(const QString& script, ComponentInstance* root);
        void run(); 
        void stop();

        // The native C++ sleep function exposed to JS
        Q_INVOKABLE void delay(int milliseconds); 
};