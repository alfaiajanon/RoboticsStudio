#include "EmulatorFactory.h"

#include "Utils/Log.h"
#include "Document/Components/ComponentInstance.h"
#include "Simulation/ErrorSystem/DefaultEmulatorCpp.h"
#include "Simulation/ErrorSystem/IMUEmulatorCpp.h"
#include "Simulation/ErrorSystem/ServoEmulatorCpp.h"
#include "Simulation/ErrorSystem/StepperEmulatorCpp.h"
#include "Simulation/ErrorSystem/DcGearEmulatorCpp.h"
#include "Simulation/ErrorSystem/HBridgeEmulatorCpp.h"
#include "Simulation/ErrorSystem/LevelShifterEmulatorCpp.h"
#include "Simulation/ErrorSystem/CameraEmulatorCpp.h"
#include "Simulation/ErrorSystem/DisplayEmulatorCpp.h"

QStringList EmulatorFactory::types() {
    return {"servo_emulator", "stepper_emulator", "dc_gear_emulator", "hbridge_emulator", "level_shifter_emulator", "imu_emulator",
            "camera_emulator", "display_emulator", "default_emulator"};
}


Emulator* EmulatorFactory::create(const QString& type, ComponentInstance* comp) {
    if (type == "servo_emulator") {
        return new ServoEmulatorCpp(comp);
    }
    else if(type == "stepper_emulator") {
        return new StepperEmulatorCpp(comp);
    }
    else if (type == "dc_gear_emulator") {
        return new DcGearEmulatorCpp(comp);
    }
    else if (type == "hbridge_emulator") {
        return new HBridgeEmulatorCpp(comp);
    }
    else if (type == "level_shifter_emulator") {
        return new LevelShifterEmulatorCpp(comp);
    }
    else if (type == "imu_emulator") {
        return new IMUEmulatorCpp(comp);
    }
    else if (type == "camera_emulator") {
        return new CameraEmulatorCpp(comp);
    }
    else if (type == "display_emulator") {
        return new DisplayEmulatorCpp(comp);
    }
    else if(type == "custom_emulator"){
        Log::error("Custom Emulator requested but factory logic not implemented. Returning nullptr.");
        return nullptr;
    }
    else if(type == "default_emulator"){
        Log::warning("Default Emulator created for component '" + comp->name + "' of '" + comp->modelId + "'. Consider implementing a specific emulator for better simulation fidelity.");
        return new DefaultEmulatorCpp(comp);
    }
    else {
        Log::error("Factory failed: Unknown emulator type requested -> " + type);
        return nullptr;
    }
}
