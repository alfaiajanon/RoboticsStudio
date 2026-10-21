#pragma once

#include "Emulator.h"
#include "EmulatorIO.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Controller/PinBus.h"
#include <algorithm>
#include <vector>

/*
 * A dual H-bridge motor driver module (L298N, TB6612FNG): reads its logic
 * inputs from the board's pins and drives its outputs as voltage sources
 * from its motor supply, for the motor model to solve (DcGearEmulatorCpp).
 *
 * Each output is a half bridge: its high side connects it to VS, its low
 * side to ground, each with the datasheet's saturation drop as V0 + R_on·I
 * (Thevenin: high = VS - high_v0 through high_r, low = low_v0 through
 * low_r). PWM on an input is averaged: an output that is HIGH for a fraction
 * f of the period is (f·high + (1-f)·low) through (f·high_r + (1-f)·low_r).
 * Both outputs of a channel LOW brake the motor; outputs off are open.
 *
 * Truth tables (datasheets):
 *   l298    per channel IN1, IN2, EN: EN LOW = both outputs off (coast);
 *           EN HIGH: IN HIGH = high side, IN LOW = low side (IN1 = IN2:
 *           brake). PWM on EN: outputs off for the rest of the period (fast
 *           decay, the motor can't drive current back). The module's ENA/ENB
 *           jumpers tie EN HIGH (per-instance driver.ena_jumper /
 *           driver.enb_jumper, default fitted).
 *   tb6612  per channel IN1, IN2, PWM, plus STBY: STBY LOW = all off;
 *           IN1/IN2 H/H = short brake, L/L = stop (open), H/L or L/H =
 *           drive while PWM is HIGH, short brake while it is LOW (slow decay).
 *
 * The supply current is the high sides' share of the motor currents (the
 * previous step's, as the motor wrote them back) plus nothing else: the
 * quiescent draw is the part's static load in its power block.
 *
 * Not modelled: the outputs' clamp diodes beyond the averaged braking,
 * thermal shutdown, the L298's current-sense pins (wired to GND on modules).
 *
 * Parameters (emulator.parameters): truth_table, high_v0, high_r, low_v0,
 * low_r (V, ohm), ideal_supply_v (VS when there is no power model).
 */
class HBridgeEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        struct Channel {
            int in1 = -1, in2 = -1, en = -1;   // en: L298 ENx / TB6612 PWMx
            int out1 = -1, out2 = -1;
            QString jumper;                    // L298N: the instance parameter of its EN jumper
            double high1 = 0.0, high2 = 0.0;   // this step: fraction of the time each output is HIGH
        };
        std::vector<Channel> channels;
        int stby = -1;
        bool tb6612 = false;
        double highV0 = 1.0, highR = 0.5, lowV0 = 1.0, lowR = 0.5, idealSupply = 12.0;
        double lastSupply = 0.0;

        void drive(int h, double high, double conn) {
            if (conn <= 0.0) { pins->openSource(h); return; }
            const double vs = supplyVolts >= 0.0 ? supplyVolts : idealSupply;
            const double vHigh = std::max(0.0, vs - highV0);
            pins->writeSource(h, high * vHigh + (1.0 - high) * lowV0, high * highR + (1.0 - high) * lowR, conn);
        }
        void open(Channel& c) {
            pins->openSource(c.out1);
            pins->openSource(c.out2);
            c.high1 = c.high2 = 0.0;
        }

    public:
        using Emulator::Emulator;

        void init() override {
            using EmulatorIO::param;
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            const QMap<QString, QVariant> p = bp ? bp->emulatorDef.parameters : QMap<QString, QVariant>{};
            tb6612 = p.value("truth_table").toString() == "tb6612";
            highV0 = param(p, "high_v0", 1.0);
            highR = param(p, "high_r", 0.5);
            lowV0 = param(p, "low_v0", 1.0);
            lowR = param(p, "low_r", 0.5);
            idealSupply = param(p, "ideal_supply_v", 12.0);
            channels.clear();
            if (!pins || !component) return;
            const int uid = component->uid;
            auto pin = [&](const char* id, const char* type) { return pins->ensurePin(uid, id, type); };
            if (tb6612) {
                stby = pin("stby", "digital");
                channels.push_back({pin("ain1", "digital"), pin("ain2", "digital"), pin("pwma", "pwm"), pin("ao1", "motor_out"), pin("ao2", "motor_out"), {}});
                channels.push_back({pin("bin1", "digital"), pin("bin2", "digital"), pin("pwmb", "pwm"), pin("bo1", "motor_out"), pin("bo2", "motor_out"), {}});
            } else {
                channels.push_back({pin("in1", "digital"), pin("in2", "digital"), pin("ena", "pwm"), pin("out1", "motor_out"), pin("out2", "motor_out"), "driver.ena_jumper"});
                channels.push_back({pin("in3", "digital"), pin("in4", "digital"), pin("enb", "pwm"), pin("out3", "motor_out"), pin("out4", "motor_out"), "driver.enb_jumper"});
            }
        }

        void update() override {
            if (!pins) return;
            // The supply current from last step's motor currents (as they were driven then).
            double supply = 0.0;
            for (const Channel& c : channels)
                supply += c.high1 * std::max(0.0, pins->amps(c.out1)) + c.high2 * std::max(0.0, pins->amps(c.out2));
            lastSupply = supply;

            const bool standby = tb6612 && pins->dutyCycle(stby) < 0.5;
            for (Channel& c : channels) {
                if (standby) { open(c); continue; }
                if (tb6612) {
                    const bool a = pins->dutyCycle(c.in1) >= 0.5, b = pins->dutyCycle(c.in2) >= 0.5;
                    const double pwm = pins->dutyCycle(c.en);
                    if (!a && !b) { open(c); continue; }                       // stop: outputs off
                    c.high1 = a && !b ? pwm : 0.0;                              // PWM LOW or H/H: both LOW, short brake
                    c.high2 = b && !a ? pwm : 0.0;
                    drive(c.out1, c.high1, 1.0);
                    drive(c.out2, c.high2, 1.0);
                } else {
                    const bool jumper = !c.jumper.isEmpty() && component->parameters.value(c.jumper, true).toBool();
                    const double en = jumper ? 1.0 : pins->dutyCycle(c.en);
                    if (en <= 0.0) { open(c); continue; }
                    c.high1 = pins->dutyCycle(c.in1);
                    c.high2 = pins->dutyCycle(c.in2);
                    drive(c.out1, c.high1, en);
                    drive(c.out2, c.high2, en);
                    c.high1 *= en;
                    c.high2 *= en;
                }
            }
        }

        void reset() override {
            lastSupply = 0.0;
            if (pins) for (Channel& c : channels) open(c);
        }

        double supplyCurrent() const override { return lastSupply; }
};
