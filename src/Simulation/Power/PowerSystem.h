#pragma once

#include <algorithm>
#include <QHash>
#include <QList>
#include <QString>
#include <QVector>
#include "Document/PowerNetwork.h"
#include "Document/Components/ComponentData.h"

class Project;
class Emulator;


/*
 * The Real level's power during a run: the project's PowerNetwork solved once
 * per physics step with the current each part draws right now. Physics
 * thread only (built at Play, between steps).
 *
 * Each step (after the emulators ran):
 *   - currents: every powered part's static load plus its motor's current
 *     (Emulator::supplyCurrent); a part that is off draws nothing;
 *   - batteries: open-circuit voltage and resistance from their discharge
 *     curve at the level of their available charge (Kinetic Battery Model:
 *     a heavy load empties the available well faster than the bound charge
 *     refills it, so the voltage drops early, and it recovers at rest); with
 *     "drains" on, the charge goes down with the current drawn (sim time),
 *     past the rated cut-off (0 %) down to the cell's dead point; a fitted
 *     protection circuit cuts the pack off below its threshold;
 *   - solve: the voltage of every rail;
 *   - parts: below their load's min_v a part switches off (back on 50 mV
 *     above it);
 *   - boards: below the brownout threshold the MCU is held in reset; once the
 *     rail is back above threshold + hysteresis for restart_ms it starts again.
 *   - the extremes of each supply (lowest voltage, highest current) for the
 *     summary at Stop, and USB over its hold current for more than 1 s.
 *
 * The currents used are the ones the parts drew in this step, and the
 * voltages they see are applied in the next one: a one-step lag that acts
 * like the bulk capacitance on a real rail.
 */
class PowerSystem {
    public:
        struct Part {
            int uid = -1;
            QString name;
            Emulator* emulator = nullptr;
            int node = -1;
            double staticLoad = 0.0;
            double minV = 0.0;
            bool on = true;
            double volts = 0.0, amps = 0.0;   // last step
        };
        struct Board {
            int uid = -1;
            QString name;
            int node = -1;
            BrownoutDef brownout;
            enum class State { Running, Held, Releasing } state = State::Running;
            double releaseAt = 0.0;
            int resets = 0;
        };
        struct Battery {
            int supply = -1;     // index in the network's supplies
            int uid = -1;
            QString name;
            BatteryDef def;
            // The two wells, in % of the rated capacity above the dead point.
            double available = 0.0, bound = 0.0;
            bool drains = false;
            bool protection = false;      // a protection circuit is fitted
            bool cut = false;             // ... and has cut the pack off (until Stop)
            double belowSince = -1.0;     // protection: under its threshold since
            double cutAt = 0.0;
            bool reportedZero = false, reportedOverDischarge = false, reportedFlat = false;

            double total() const { return def.deadPercent() + available + bound; }   // % of rated
            // The available well's level, where the curve is read (% of rated).
            double level() const { return def.deadPercent() + available / def.availableFraction; }
            // What a fuel gauge on the pack reads: 0..100 %, 0 at the rated cut-off.
            double charge() const { return std::clamp(total(), 0.0, 100.0); }
            double availableCharge() const { return std::clamp(level(), 0.0, 100.0); }
        };
        struct Extremes {
            double minV = 1e9, minVAt = 0.0;
            double peakI = 0.0, peakIAt = 0.0;
            double overSince = -1.0;
            bool warnedOver = false;
        };
        // What changed in the last step, for SimulationManager to apply.
        struct Events {
            QList<int> switchedOff, switchedOn;   // part uids
            QList<int> brownedOut;                // board uids now held in reset
            QList<int> released;                  // board uids to restart now
        };

        void build(Project& project, const QHash<int, Emulator*>& emulators);
        void clear();
        bool isActive() const { return active; }

        Events step(double now, double dt);

        // The voltage on a part's supply pin (last step); -1 if the part has
        // none (ideal power).
        double supplyOf(int uid) const;
        bool isOn(int uid) const;
        const QList<Part>& parts() const { return partList; }
        const QList<Board>& boards() const { return boardList; }
        const QList<Battery>& batteries() const { return batteryList; }
        double railVolts(int uid, const QString& pin) const;
        double partCurrent(int uid) const;   // A drawn from its supply pin (last step); 0 if none
        // A battery's state (last step); false if `uid` is not a battery.
        // percent and available are fuel-gauge readings (0..100 %).
        bool battery(int uid, double* percent, double* volts, double* amps, double* available = nullptr) const;
        double boardRailVolts(int boardUid) const;
        // Current the board's pins carry into loads this step (A): drawn from
        // the board's MCU rail, on top of its own load.
        void setBoardLoad(int boardUid, double amps) { boardLoads[boardUid] = amps; }

        // One line per source and per board MCU rail, for the Output at Stop.
        QStringList summary() const;

    private:
        bool active = false;
        PowerNetwork net;
        PowerNetwork::Solution last;
        bool solved = false;
        QList<Part> partList;
        QList<Board> boardList;
        QList<Battery> batteryList;
        QVector<Extremes> supplyExtremes;    // per network supply
        QVector<Extremes> boardExtremes;     // per board
        QHash<int, int> partIndex;           // uid -> partList index
        QHash<int, QString> names;           // uid -> part name
        QHash<int, double> boardLoads;       // board uid -> A from its pins (setBoardLoad)

        QString supplyName(int i) const;
        void stepBattery(Battery& b, double now, double dt);
};
