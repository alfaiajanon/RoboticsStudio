#include "PowerSystem.h"

#include <algorithm>
#include "Document/Project.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Utils/Log.h"


// A part switches back on this far above its min_v (no chatter at the edge).
static constexpr double kOnHysteresis = 0.05;
// A rail this low is dead for a part without a min_v.
static constexpr double kDeadRail = 0.05;
// USB over its hold current for this long gets a warning (a polyfuse trips in ~1 s at 2x hold).
static constexpr double kOverHoldWarnAfter = 1.0;



void PowerSystem::clear() {
    active = false;
    net = PowerNetwork();
    last = PowerNetwork::Solution();
    solved = false;
    partList.clear();
    boardList.clear();
    batteryList.clear();
    supplyExtremes.clear();
    boardExtremes.clear();
    partIndex.clear();
    names.clear();
}



void PowerSystem::build(Project& project, const QHash<int, Emulator*>& emulators) {
    clear();
    if (project.getLevel() != ProjectLevel::Real) return;
    net = PowerNetwork::build(project);
    if (net.isEmpty()) return;
    active = true;

    for (ComponentInstance* comp : project.getComponentMap()) names.insert(comp->uid, comp->name);

    QList<ComponentInstance*> comps = project.getComponentMap().values();
    std::sort(comps.begin(), comps.end(), [](ComponentInstance* a, ComponentInstance* b) { return a->uid < b->uid; });
    for (ComponentInstance* comp : comps) {
        const ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp) continue;
        if (bp->board.present) {
            if (!bp->power.brownout.present) continue;
            Board b;
            b.uid = comp->uid;
            b.name = comp->name;
            b.node = net.nodeOf(comp->uid, bp->power.brownout.rail);
            b.brownout = bp->power.brownout;
            if (b.node >= 0) boardList.append(b);
            continue;
        }
        const QString pin = PowerNetwork::supplyPin(comp);
        if (pin.isEmpty()) continue;
        Part p;
        p.uid = comp->uid;
        p.name = comp->name;
        p.emulator = emulators.value(comp->uid, nullptr);
        p.node = net.nodeOf(comp->uid, pin);
        if (const PowerLoadDef* l = bp->power.loadOn(pin)) {
            p.staticLoad = l->current;
            p.minV = l->minV;
        }
        if (p.node < 0) continue;
        partIndex.insert(p.uid, partList.size());
        partList.append(p);
    }

    for (int i = 0; i < net.supplies.size(); ++i) {
        const PowerNetwork::Supply& s = net.supplies[i];
        if (s.kind != "battery") continue;
        ComponentInstance* comp = project.getComponentByUid(s.uid);
        if (!comp || !comp->getBlueprint()) continue;
        Battery b;
        b.supply = i;
        b.uid = s.uid;
        b.name = comp->name;
        b.curve = comp->getBlueprint()->power.battery;
        b.percent = std::clamp(comp->parameters.value(PowerNetwork::kChargePercent, 100.0).toDouble(), 0.0, 100.0);
        b.drains = comp->parameters.value(PowerNetwork::kDrains, false).toBool();
        batteryList.append(b);
    }
    supplyExtremes = QVector<Extremes>(net.supplies.size());
    boardExtremes = QVector<Extremes>(boardList.size());

    // A first solve at rest, so the first step's parts see their supply.
    last = net.solveStatic();
    solved = true;
    for (Part& p : partList) {
        p.volts = last.voltage[p.node];
        p.on = p.volts >= std::max(p.minV, kDeadRail);
    }
}



PowerSystem::Events PowerSystem::step(double now, double dt) {
    Events ev;
    if (!active) return ev;

    // ---- currents drawn this step ----
    QVector<double> extra(net.nodes.size(), 0.0);
    for (Part& p : partList) {
        p.amps = p.on ? p.staticLoad + (p.emulator ? p.emulator->supplyCurrent() : 0.0) : 0.0;
        extra[p.node] += p.amps - p.staticLoad;   // the network already counts the static load
    }

    // ---- batteries at their present charge ----
    for (Battery& b : batteryList) {
        const BatteryDef::Point pt = b.curve.at(b.percent);
        net.supplies[b.supply].voc = pt.voltage;
        net.supplies[b.supply].r = pt.resistance;
    }

    last = net.solve(extra, solved ? &last : nullptr);
    solved = true;

    for (Battery& b : batteryList) {
        if (!b.drains || b.curve.capacityAh <= 0.0) continue;
        b.percent = std::max(0.0, b.percent - last.supplyCurrent[b.supply] * dt / 3600.0 / b.curve.capacityAh * 100.0);
        if (b.percent <= 0.0 && !b.reportedEmpty) {
            b.reportedEmpty = true;
            Log::warning(QString("Power: %1 is empty at %2 s (it stays at its cut-off voltage; a real one drops further)")
                             .arg(b.name).arg(now, 0, 'f', 3));
        }
    }

    // ---- extremes and USB over its hold current ----
    for (int i = 0; i < net.supplies.size(); ++i) {
        const PowerNetwork::Supply& s = net.supplies[i];
        if (s.dropped) continue;
        Extremes& x = supplyExtremes[i];
        const double v = last.voltage[s.node], amps = last.supplyCurrent[i];
        if (v < x.minV) { x.minV = v; x.minVAt = now; }
        if (amps > x.peakI) { x.peakI = amps; x.peakIAt = now; }
        if (s.kind == "usb" && s.holdCurrent > 0.0) {
            if (amps > s.holdCurrent) {
                if (x.overSince < 0.0) x.overSince = now;
                if (!x.warnedOver && now - x.overSince >= kOverHoldWarnAfter) {
                    x.warnedOver = true;
                    Log::warning(QString("Power: %1 has drawn over %2 A for 1 s (%3 A now): a real polyfuse would cut the power "
                                         "(not simulated). Power the motors from their own supply")
                                     .arg(supplyName(i)).arg(s.holdCurrent).arg(amps, 0, 'f', 2));
                }
            } else {
                x.overSince = -1.0;
            }
        }
    }

    // ---- parts on/off ----
    for (Part& p : partList) {
        p.volts = last.voltage[p.node];
        const double off = std::max(p.minV, kDeadRail);
        if (p.on && p.volts < off) {
            p.on = false;
            ev.switchedOff.append(p.uid);
        } else if (!p.on && p.volts >= off + kOnHysteresis) {
            p.on = true;
            ev.switchedOn.append(p.uid);
        }
    }

    // ---- boards: brownout detector ----
    for (int i = 0; i < boardList.size(); ++i) {
        Board& b = boardList[i];
        const double v = last.voltage[b.node];
        Extremes& x = boardExtremes[i];
        if (v < x.minV) { x.minV = v; x.minVAt = now; }
        switch (b.state) {
            case Board::State::Running:
                if (v < b.brownout.threshold) {
                    b.state = Board::State::Held;
                    ++b.resets;
                    ev.brownedOut.append(b.uid);
                }
                break;
            case Board::State::Held:
                if (v >= b.brownout.threshold + b.brownout.hysteresis) {
                    b.state = Board::State::Releasing;
                    b.releaseAt = now + b.brownout.restart;
                }
                break;
            case Board::State::Releasing:
                if (v < b.brownout.threshold) {
                    b.state = Board::State::Held;
                } else if (now >= b.releaseAt - 1e-9) {
                    b.state = Board::State::Running;
                    ev.released.append(b.uid);
                }
                break;
        }
    }
    return ev;
}



double PowerSystem::supplyOf(int uid) const {
    const int i = partIndex.value(uid, -1);
    return i >= 0 ? partList[i].volts : -1.0;
}


bool PowerSystem::isOn(int uid) const {
    const int i = partIndex.value(uid, -1);
    return i < 0 || partList[i].on;
}


double PowerSystem::railVolts(int uid, const QString& pin) const {
    const int n = net.nodeOf(uid, pin);
    return n >= 0 && solved ? last.voltage[n] : -1.0;
}


double PowerSystem::partCurrent(int uid) const {
    const int i = partIndex.value(uid, -1);
    return i >= 0 ? partList[i].amps : 0.0;
}


bool PowerSystem::battery(int uid, double* percent, double* volts, double* amps) const {
    for (const Battery& b : batteryList) {
        if (b.uid != uid) continue;
        const PowerNetwork::Supply& s = net.supplies[b.supply];
        if (percent) *percent = b.percent;
        if (volts) *volts = solved ? last.voltage[s.node] : s.voc;
        if (amps) *amps = solved ? last.supplyCurrent[b.supply] : 0.0;
        return true;
    }
    return false;
}


double PowerSystem::boardRailVolts(int boardUid) const {
    for (const Board& b : boardList) if (b.uid == boardUid) return solved ? last.voltage[b.node] : -1.0;
    return -1.0;
}


QString PowerSystem::supplyName(int i) const {
    const PowerNetwork::Supply& s = net.supplies[i];
    return names.value(s.uid, QString("part %1").arg(s.uid)) + " " + s.pin.toUpper();
}


QStringList PowerSystem::summary() const {
    QStringList out;
    if (!active) return out;
    for (int i = 0; i < net.supplies.size(); ++i) {
        const Extremes& x = supplyExtremes[i];
        if (net.supplies[i].dropped || x.minV > 1e8) continue;
        out << QString("%1: min %2 V at %3 s, peak %4 A at %5 s")
                   .arg(supplyName(i)).arg(x.minV, 0, 'f', 2).arg(x.minVAt, 0, 'f', 3)
                   .arg(x.peakI, 0, 'f', 2).arg(x.peakIAt, 0, 'f', 3);
    }
    for (const Battery& b : batteryList)
        out << QString("%1: %2 % charge left%3").arg(b.name).arg(b.percent, 0, 'f', 1).arg(b.drains ? "" : " (not draining)");
    for (int i = 0; i < boardList.size(); ++i) {
        const Board& b = boardList[i];
        const Extremes& x = boardExtremes[i];
        if (x.minV > 1e8) continue;
        out << QString("%1 %2 (MCU): min %3 V at %4 s, %5 brownout reset%6")
                   .arg(b.name, b.brownout.rail.toUpper()).arg(x.minV, 0, 'f', 2).arg(x.minVAt, 0, 'f', 3)
                   .arg(b.resets).arg(b.resets == 1 ? "" : "s");
    }
    return out;
}
