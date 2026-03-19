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
            Log::info(msg); // Routes JS directly to your UI OutputPanel!
        }
};




class Microcontroller : public QObject{
    Q_OBJECT
    private:
        QJSEngine engine;
        QJSValue loopFunction;
        JSConsoleProxy* consoleProxy;
        QJSValue jsIterator;       
        bool isSleeping = false;   
        double wakeupTime = 0.0;
        bool isCompiled = false;
        

        // Recursively walks your component tree and exposes emulators to JS
        void injectEmulators(ComponentInstance* comp) {
            Log::info("injecting .... (UID):" + QString::number(comp->uid));
            if (!comp){
                Log::info("Comp invalid");
                return;
            }
            if (comp->emulator) {
                QString jsVarName = "comp_" + QString::number(comp->uid);
                QJSValue wrappedEmulator = engine.newQObject(comp->emulator);
                engine.globalObject().setProperty(jsVarName, wrappedEmulator);
            }else{
                Log::error("emulator not found");
            }
            for (ComponentInstance* child : comp->children) {
                injectEmulators(child);
            }
        }


        
    public:


        Microcontroller(QObject* parent = nullptr) : QObject(parent) {
            consoleProxy = new JSConsoleProxy(this);
            QJSValue jsConsole = engine.newQObject(consoleProxy);
            engine.globalObject().setProperty("console", jsConsole);
        }




        void compile(const QString& script, ComponentInstance* root) {
            isCompiled = false;

            engine.evaluate("function delay(ms) { return ms / 1000.0; }");

            injectEmulators(root);

            QJSValue result = engine.evaluate(script);
            if (result.isError()) {
                Log::error("JS Compile Error: " + result.toString() + " at line " + QString::number(result.property("lineNumber").toInt()));
                return;
            }

            loopFunction = engine.globalObject().property("loop");
            if (loopFunction.isCallable()) {
                
                // --- THE FIX IS HERE ---
                // We MUST call the function once to generate the iterator object!
                jsIterator = loopFunction.call(); 
                
                // Safety check: Did they actually use a generator (function*)?
                if (jsIterator.hasProperty("next")) {
                    isCompiled = true;
                    isSleeping = false;
                    wakeupTime = 0.0;
                    Log::info("Microcontroller compiled and coroutine primed!");
                } else {
                    Log::error("Compile Error: loop() must be a generator! Use 'function* loop()'");
                }
                // -----------------------
                
            } else {
                Log::error("No callable 'loop()' function found in script.");
            }
        }
        

        void tick(mjData* d) {
            if (!isCompiled || !d) return;

            double currentSimTime = d->time; 
            
            if (isSleeping) {
                if (currentSimTime < wakeupTime) return; 
                isSleeping = false; 
            }

            QJSValue nextFunc = jsIterator.property("next");
            QJSValue resultObj = nextFunc.callWithInstance(jsIterator);

            if (resultObj.isError()) {
                Log::error("JS Runtime Error: " + resultObj.toString());
                isCompiled = false;
                return;
            }

            bool isDone = resultObj.property("done").toBool();
            if (isDone) {
                jsIterator = loopFunction.call();
                return;
            }

            QJSValue yieldedValue = resultObj.property("value");
            if (yieldedValue.isNumber()) {
                double delaySeconds = yieldedValue.toNumber();
                wakeupTime = currentSimTime + delaySeconds;
                isSleeping = true;
            }
        }
        
        void stop() {
            isCompiled = false;
            // Optional: Reset engine state, clear memory, etc.
        }
};