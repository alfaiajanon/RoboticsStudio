#pragma once

#include <QJSEngine>
#include "Simulation/Controller/RobotController.h"
#include "Utils/Log.h"





class JSConsoleProxy : public QObject {
    Q_OBJECT
    QString tag;
    public:
        explicit JSConsoleProxy(const QString& tag, QObject* parent = nullptr) : QObject(parent), tag(tag) {}
        Q_INVOKABLE void log(const QString& msg) {
            Log::info("[" + tag + "]: " + msg);
        }
};



/*
 * Runs the stage script (JavaScript: setup() once, then loop()) in lockstep
 * with the physics through its LockstepGate (see there): sim time is the only
 * clock. Its globals come from setGlobal() (prop_<id>, robot). The robot runs
 * C++ firmware instead (FirmwareRuntime).
 */
class MicroController : public QObject, public RobotController {
    Q_OBJECT
    private:
        QJSEngine engine;
        QJSValue setupFunction;
        QJSValue loopFunction;
        JSConsoleProxy* consoleProxy;
        QString tag;

        bool isCompiled = false;
        bool setupDone = false;   // controller thread (and compile(), while idle)


    public:
        // tag prefixes console.log output ("STAGE")
        explicit MicroController(const QString& tag = "MCU", QObject* parent = nullptr);
        ~MicroController();

        // Starts the script at sim time 0, due on the first step.
        void compile(const QString& script);
        void setGlobal(const QString& name, QObject* obj);

        bool runCycle() override;
        void stop() override;

        // The native functions exposed to JS, all on the sim clock.
        Q_INVOKABLE void delay(double milliseconds);
        Q_INVOKABLE void delayMicroseconds(double microseconds);
        Q_INVOKABLE double millis();
        Q_INVOKABLE double micros();
};
