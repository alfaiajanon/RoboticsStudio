#include "MicroController.h"
#include <thread>
#include <chrono>





/*
 * Recursively walks your component tree and exposes emulators to JS
 */
void MicroController::injectEmulators(ComponentInstance* comp) {
    if (!comp) return;

    if (comp->emulator) {
        QString jsVarName = "comp_" + QString::number(comp->uid);
        QJSEngine::setObjectOwnership(comp->emulator, QJSEngine::CppOwnership);
        QJSValue wrappedEmulator = engine.newQObject(comp->emulator);
        engine.globalObject().setProperty(jsVarName, wrappedEmulator);
    } 
    
    for (ComponentInstance* child : comp->children) {
        injectEmulators(child);
    }
}







MicroController::MicroController(QObject* parent) : QObject(parent), stopRequested(false) {
    // Setup Console
    consoleProxy = new JSConsoleProxy(this);
    QJSEngine::setObjectOwnership(consoleProxy, QJSEngine::CppOwnership);
    QJSValue jsConsole = engine.newQObject(consoleProxy);
    engine.globalObject().setProperty("console", jsConsole);

    // Setup Native Delay Function
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
    QJSValue mcuObject = engine.newQObject(this);
    engine.globalObject().setProperty("delay", mcuObject.property("delay"));
}






MicroController::~MicroController() {
    stop();
}







void MicroController::compile(const QString& script, ComponentInstance* root) {
    isCompiled = false;
    stopRequested = false;
    engine.setInterrupted(false); // Clear any previous interrupts

    injectEmulators(root);

    QJSValue result = engine.evaluate(script);
    if (result.isError()) {
        Log::error("JS Compile Error: " + result.toString() + " at line " + QString::number(result.property("lineNumber").toInt()));
        return;
    }

    loopFunction = engine.globalObject().property("loop");
    if (loopFunction.isCallable()) {
        isCompiled = true;
        Log::info("MicroController compiled and ready!");
    } else {
        Log::error("Compile Error: No callable 'loop()' function found in script.");
    }
}




/*
 * Executes one full cycle of the user's loop() function.
 * Called continuously by the mcuLoop background thread.
 */
void MicroController::run() {
    if (!isCompiled || stopRequested) return;

    QJSValue resultObj = loopFunction.call();

    // If the engine gets interrupted by stop() while stuck in a busy JS loop, 
    // it will return an error here. We safely ignore it if a stop was requested.
    if (resultObj.isError() && !stopRequested) {
        Log::error("JS Runtime Error: " + resultObj.toString());
        isCompiled = false; 
    }
}






/*
 * Safely aborts any running JS code and interrupts native delays.
 * Called from the UI thread when the user clicks Pause or Edit.
 */
void MicroController::stop() {
    stopRequested = true;
    engine.setInterrupted(true); 
}







/*
 * A native sleep function exposed to the JS engine.
 * Fast-polls the stopRequested flag so it can wake up instantly if the user hits stop.
 */
void MicroController::delay(int milliseconds) {
    auto start = std::chrono::steady_clock::now();
    
    while (!stopRequested) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= milliseconds) {
            break;
        }
        
        // Sleep for 1ms at a time so we don't hog the CPU, 
        // but remain incredibly responsive to the stopRequested flag!
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}