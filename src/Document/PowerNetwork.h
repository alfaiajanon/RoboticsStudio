#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVector>
#include "Document/Netlist.h"

class Project;
class ComponentInstance;


/*
 * The Real level's power wiring as a static DC network: sources (a board's
 * USB feed, batteries, bench supplies) with internal resistance, regulators
 * (linear, buck) between rails, and loads. Built from the project's nets and
 * the parts' "power" blocks; solved for the voltage of every rail given the
 * current each rail's loads draw.
 *
 * Shared by the design rules (nominal currents) and the simulation (live
 * currents, once per physics step), so both agree. No widgets, no Simulation.
 *
 * Model (docs/user/wiring.md, "Power"):
 *   source       V = Voc - I·R; a bench supply over its current limit drops its
 *                voltage (constant current, against a resistive load)
 *   linear reg   Vout = min(Vset, Vin - dropout); Iin = Iout + Iq
 *   buck         Vout = min(Vset, Vin - headroom); Iin = Vout·Iout / (η·Vin) + Iq
 *                (in dropout it passes the input through: Iin = Iout + Iq)
 *   over Ilimit  a regulator's output folds back (Vout scaled by Ilimit / Iout;
 *                Ilimit is its current limit, at or above its rating Imax)
 *   several sources on one rail: a board's USB feed is dropped when another
 *                source drives its rail (the board is powered externally); of
 *                the rest the highest voltage wins (a short is a design-rule error)
 */
class PowerNetwork {
    public:
        // One rail: a net of power pins, or an unwired power pin on its own
        // (a board's 3V3 rail exists without a wire).
        struct Node {
            QList<PinRef> pins;
            bool live = false;   // some source can reach it (static)
        };
        struct Supply {
            int uid = -1;
            QString pin, kind;
            int node = -1;
            double voc = 0.0, r = 0.0;
            double currentLimit = 0.0;   // supply: CC limit; 0 = none
            double holdCurrent = 0.0;    // usb: polyfuse hold (warnings)
            bool dropped = false;        // usb with another source on its rail
        };
        struct Regulator {
            int uid = -1;
            QString id, kind;
            QString outPin;   // the pin it drives
            int in = -1, out = -1;
            double vOut = 0.0, dropout = 0.0, efficiency = 1.0, iMax = 0.0, iq = 0.0;
            double iLimit = 0.0;   // folds back above this (>= iMax)
        };
        struct Load {
            int uid = -1;
            QString pin;
            int node = -1;
            double current = 0.0;   // static, A
            double minV = 0.0;
        };
        struct Solution {
            QVector<double> voltage;     // per node
            QVector<double> demand;      // per node: current drawn from it (loads + regulator inputs)
            QVector<double> supplyCurrent;
            QVector<double> regIn, regOut;
        };

        QList<Node> nodes;
        QList<Supply> supplies;
        QList<Regulator> regulators;
        QList<Load> loads;
        bool hasLoop = false;        // a regulator feeds its own input (through other rails)

        static PowerNetwork build(Project& project);
        bool isEmpty() const { return nodes.isEmpty(); }

        int nodeOf(const PinRef& pin) const { return nodeIndex.value(pin, -1); }
        int nodeOf(int uid, const QString& pin) const { return nodeOf(PinRef{uid, pin}); }
        // The pin a part draws its supply from (its first typed power pin), or "".
        static QString supplyPin(const ComponentInstance* comp);

        // Live sources driving a node: supply indices (not dropped) and
        // regulator indices whose input is live.
        QList<int> suppliesOn(int node) const;
        QList<int> regulatorsInto(int node) const;

        // extra: current per node on top of the static loads (motors).
        // previous: the last solution (the regulators' input currents use its
        // voltages; the network is solved in a few passes from it).
        Solution solve(const QVector<double>& extra, const Solution* previous = nullptr) const;
        Solution solveStatic() const { return solve(QVector<double>(nodes.size(), 0.0)); }

        // Ground: every part's ground pins are one (common inside the part);
        // wires join them. Same domain = a shared reference.
        int groundDomainOf(const PinRef& pin) const { return groundDomain.value(pin, -1); }
        int groundDomainOfPart(int uid) const;

        // Per-instance settings (ComponentInstance::parameters, "power." keys).
        static constexpr const char* kVoltage = "power.voltage";             // bench supply, V
        static constexpr const char* kCurrentLimit = "power.current_limit_a"; // bench supply, A
        static constexpr const char* kVOut = "power.v_out";                 // buck module, V
        static constexpr const char* kChargePercent = "power.charge_percent"; // battery, 0..100
        static constexpr const char* kDrains = "power.drains";              // battery, bool
        static constexpr const char* kProtection = "power.protection";      // battery with a protection block, bool

        // The most a part can draw at a supply voltage: its static load plus
        // its motor at stall (servo: stall_current_a at rated_voltage, scaled;
        // stepper: the chopper's power 2·I²·R over the supply).
        static double peakCurrent(const ComponentInstance* comp, double supplyVolts);

    private:
        QHash<PinRef, int> nodeIndex;
        QHash<PinRef, int> groundDomain;
        QVector<int> order;   // nodes, sources first (regulator chains in order)
};
