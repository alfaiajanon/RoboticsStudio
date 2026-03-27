#include "EmulatorFactory.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/ErrorSystem/DefaultEmulatorCpp.h"
#include "Simulation/ErrorSystem/IMUEmulatorCpp.h"
#include "Simulation/ErrorSystem/ServoEmulatorCpp.h"
#include "Core/Log.h"

Emulator* EmulatorFactory::create(const QString& type, ComponentInstance* comp) {
    if (type == "servo_emulator") {
        Log::info("Creating Servo Emulator for component: " + comp->name);
        return new ServoEmulatorCpp(comp);
    } 
    else if (type == "imu_emulator") {
        Log::info("Creating IMU Emulator for component: " + comp->name);
        return new IMUEmulatorCpp(comp);
    }
    else if(type == "custom_emulator"){
        Log::error("Custom Emulator requested but factory logic not implemented. Returning nullptr.");
        return nullptr; 
    }
    else if(type == "default_emulator"){
        return new DefaultEmulatorCpp(comp); 
    }
    else {
        Log::error("Factory failed: Unknown emulator type requested -> " + type);
        return nullptr;
    }
}