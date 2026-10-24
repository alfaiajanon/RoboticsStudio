#pragma once

#include "Emulator.h"


/*
 * A BSS138 logic level shifter module. It does nothing per step: at Play,
 * PinBus joins each channel's LV and HV nets into one signal while the
 * module is powered (DesignRules::unpoweredParts), because the MOSFETs switch
 * in nanoseconds, far below anything the simulation resolves. So digital
 * levels, I2C and UART cross it with no code here; each side keeps its own
 * logic voltage for the design rules. Parameters (read by PinBus and the
 * design rules): channels ([[lv, hv], ...]) and max_bitrate.
 */
class LevelShifterEmulatorCpp : public Emulator {
    Q_OBJECT

    public:
        using Emulator::Emulator;

        void init() override {}
        void update() override {}
        void reset() override {}
};
