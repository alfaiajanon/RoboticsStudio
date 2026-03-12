#include "ComponentInstance.h"
#include "Core/Log.h"



/*
 * Initializes the IO memory buffers based on the component's blueprint.
 * Pre-allocates streams and locks the mutex to prevent partial initialization reads.
 */
void ComponentInstance::initializeIO() {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (!blueprint) return;

    for (const QString& key : blueprint->inputDefs.keys()) {
        inputs[key] = IOStream();
    }
    for (const QString& key : blueprint->outputDefs.keys()) {
        outputs[key] = IOStream();
    }
}




/*
 * Thread-safe setter for an actuator's target value.
 * Used by the UI sliders or scripting engine to command the component.
 */
void ComponentInstance::setInputTarget(const QString& key, double value) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (inputs.contains(key)) {
        inputs[key].targetValue = value;
        Log::info("--Updated Input Target: " + key + " --> " + QString::number(value) + " (Component: " + name + ")");
    }
}




/*
 * Thread-safe getter for an actuator's target value.
 * Exclusively read by the MuJoCo pre-step synchronizer.
 */
double ComponentInstance::getInputTarget(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return inputs.contains(key) ? inputs.value(key).targetValue : 0.0;
}




/*
 * Thread-safe setter for sensor telemetry.
 * Exclusively written by the MuJoCo post-step synchronizer.
 */
void ComponentInstance::setOutputCurrent(const QString& key, double value) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (outputs.contains(key)) {
        outputs[key].currentValue = value;
    }
}




/*
 * Thread-safe getter for sensor telemetry.
 * Used by the Inspector UI to display live feedback to the user.
 */
double ComponentInstance::getOutputCurrent(const QString& key) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    return outputs.contains(key) ? outputs.value(key).currentValue : 0.0;
}




/*
 * Safely caches the pre-computed MuJoCo integer ID for an IO stream.
 * Binds the high-level UI component directly to the low-level C array index.
 */
void ComponentInstance::setMujocoId(const QString& key, int id, bool isInput) {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (isInput && inputs.contains(key)) {
        inputs[key].mujocoId = id;
    } else if (!isInput && outputs.contains(key)) {
        outputs[key].mujocoId = id;
    }
}




/*
 * Thread-safe getter for the cached MuJoCo integer ID.
 * Prevents expensive string lookups during the high-frequency physics loop.
 */
int ComponentInstance::getMujocoId(const QString& key, bool isInput) const {
    std::lock_guard<std::mutex> lock(ioMutex);
    if (isInput && inputs.contains(key)) return inputs.value(key).mujocoId;
    if (!isInput && outputs.contains(key)) return outputs.value(key).mujocoId;
    return -1;
}