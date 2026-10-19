#include "MicroController.h"






MicroController::MicroController(const QString& tag, QObject* parent)
    : QObject(parent),
      RobotController(tag, [this] { engine.setInterrupted(true); }),
      tag(tag) {
    // Setup Console
    consoleProxy = new JSConsoleProxy(tag, this);
    QJSEngine::setObjectOwnership(consoleProxy, QJSEngine::CppOwnership);
    QJSValue jsConsole = engine.newQObject(consoleProxy);
    engine.globalObject().setProperty("console", jsConsole);

    // Native time functions (sim clock)
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
    QJSValue mcuObject = engine.newQObject(this);
    for (const char* fn : {"delay", "delayMicroseconds", "millis", "micros"}) {
        engine.globalObject().setProperty(fn, mcuObject.property(fn));
    }
}






MicroController::~MicroController() {
    stop();
}







void MicroController::setGlobal(const QString& name, QObject* obj) {
    QJSEngine::setObjectOwnership(obj, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(name, engine.newQObject(obj));
}




void MicroController::compile(const QString& script) {
    // A previous run may still be unwinding out of its code on the controller
    // thread; the engine must not be used from two threads at once.
    stop();
    if (!gate.waitUntilOutOfCall(LockstepGate::kWatchdog)) {
        Log::error(tag + ": previous script run did not stop; compiling anyway");
    }

    isCompiled = false;
    setupDone = false;
    gate.reset();
    engine.setInterrupted(false); // Clear any previous interrupts

    QJSValue result = engine.evaluate(script);
    if (result.isError()) {
        Log::error("JS Compile Error: " + result.toString() + " at line " + QString::number(result.property("lineNumber").toInt()));
        return;
    }

    setupFunction = engine.globalObject().property("setup");
    loopFunction = engine.globalObject().property("loop");
    if (loopFunction.isCallable()) {
        isCompiled = true;
        Log::info("MicroController compiled and ready!");
        gate.start();
    } else {
        Log::error("Compile Error: No callable 'loop()' function found in script.");
    }
}




/*
 * One call of the script once released: setup() the first time (when the
 * script defines one), loop() after that, as on an Arduino.
 * Called continuously by the controller's background thread.
 */
bool MicroController::runCycle() {
    if (!gate.beginCall()) return false;

    const bool isSetup = !setupDone;
    setupDone = true;
    QJSValue resultObj;
    if (!isSetup) resultObj = loopFunction.call();
    else if (setupFunction.isCallable()) resultObj = setupFunction.call();

    gate.endCall();

    // stop() and the watchdog interrupt the engine, which surfaces here as an
    // error; both already reported themselves and left the gate idle.
    if (gate.stopRequested() || gate.watchdogFired()) return true;

    if (resultObj.isError()) {
        Log::error("JS Runtime Error: " + resultObj.toString());
        isCompiled = false;
        gate.setIdle();
        return true;
    }

    if (!isSetup) gate.charge(LockstepGate::kLoopCost);
    return true;
}



/*
 * Safely aborts any running JS code and wakes a blocked delay().
 * Called from the UI thread when the user clicks Edit (and before compiling).
 */
void MicroController::stop() {
    gate.stop();
    engine.setInterrupted(true);
}



void MicroController::delay(double milliseconds) {
    gate.wait(milliseconds / 1000.0);
}



void MicroController::delayMicroseconds(double microseconds) {
    gate.wait(microseconds / 1e6);
}



double MicroController::millis() {
    return gate.millis();
}



double MicroController::micros() {
    return gate.micros();
}
