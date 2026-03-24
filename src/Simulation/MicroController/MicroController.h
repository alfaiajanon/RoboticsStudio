#pragma once

#include <QJSEngine>
#include <QString>
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




class MicroController : public QObject{
    Q_OBJECT
    private:
        QJSEngine engine;
        QJSValue jsIterator;       
        QJSValue loopFunction;
        JSConsoleProxy* consoleProxy;
        bool isCompiled = false;
        bool isSleeping = false;   
        double wakeupTime = 0.0;
    
        void injectEmulators(ComponentInstance* comp);

    public:
        MicroController(QObject* parent = nullptr);

        void compile(const QString& script, ComponentInstance* root);
        void tick(mjData* d);
        void stop();
};