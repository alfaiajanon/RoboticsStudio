#include "DesignRules.h"

#include <QMap>
#include <algorithm>
#include "Document/Project.h"
#include "Document/Netlist.h"
#include "Document/PowerNetwork.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"



namespace {

// What the checks need to know about the project's wiring, built once per check.
struct Wiring {
    Project& project;
    QList<Net> nets;
    QHash<PinRef, int> netOf;

    explicit Wiring(Project& p) : project(p), nets(p.netlist().nets()), netOf(Netlist::indexOf(nets)) {}

    const PinDef* pinDef(const PinRef& r) const {
        ComponentInstance* comp = project.getComponentByUid(r.uid);
        if (!comp || !comp->getBlueprint()) return nullptr;
        for (const PinDef& p : comp->getBlueprint()->pins) if (p.id == r.pin) return &p;
        return nullptr;
    }
    bool isBoardComp(int uid) const { return Project::isBoard(project.getComponentByUid(uid)); }
    int net(int uid, const QString& pin) const { return netOf.value(PinRef{uid, pin}, -1); }

    // Pins of a net with a given type, on any part.
    QList<PinRef> pins(int net, const QString& type) const {
        QList<PinRef> out;
        if (net < 0) return out;
        for (const PinRef& r : nets[net].pins) {
            const PinDef* p = pinDef(r);
            if (p && p->type == type) out.append(r);
        }
        return out;
    }
    // Pins of a net with a given type, on boards only.
    QList<PinRef> boardPins(int net, const QString& type) const {
        QList<PinRef> out;
        if (net < 0) return out;
        for (const PinRef& r : nets[net].pins) {
            const PinDef* p = pinDef(r);
            if (p && isBoardComp(r.uid) && p->type == type) out.append(r);
        }
        return out;
    }
};

QString partName(Project& project, int uid) {
    ComponentInstance* comp = project.getComponentByUid(uid);
    return comp ? comp->name : QString("part %1").arg(uid);
}

QString label(const QString& pinId) { return pinId.toUpper(); }

// Highest voltage a part's signal pin takes: its own range, else the part's supply.
double signalLimit(const ComponentBlueprint* bp, const PinDef& pin) {
    if (pin.voltageRange.second > 0.0f) return pin.voltageRange.second;
    for (const PinDef& p : bp->pins)
        if (p.type == "power" && p.voltageRange.second > 0.0f) return p.voltageRange.second;
    return 0.0;
}

} // namespace



QHash<int, QString> DesignRules::unpoweredParts(Project& project) {
    QHash<int, QString> out;
    if (project.getLevel() != ProjectLevel::Real) return out;
    const PowerNetwork pn = PowerNetwork::build(project);

    // The boards' ground: what every part's signals are measured against.
    QSet<int> boardGround;
    for (ComponentInstance* b : project.boards()) {
        const int d = pn.groundDomainOfPart(b->uid);
        if (d >= 0) boardGround.insert(d);
    }

    for (ComponentInstance* comp : project.getComponentMap()) {
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp || bp->board.present) continue;   // boards: checked as sources (their feed) in check()
        QStringList unpowered;
        bool hasGround = false, hasPower = false;
        for (const PinDef& p : bp->pins) {
            if (p.type == "power") {
                hasPower = true;
                const int node = pn.nodeOf(comp->uid, p.id);
                if (node < 0 || !pn.nodes[node].live) unpowered.append(p.id);
            }
            hasGround = hasGround || p.type == "ground";
        }
        if (!unpowered.isEmpty()) {
            out.insert(comp->uid, QString("no supply on %1").arg(unpowered.join(", ")));
        } else if (hasPower && hasGround && !boardGround.contains(pn.groundDomainOfPart(comp->uid))) {
            QStringList grounds;
            for (const PinDef& p : bp->pins) if (p.type == "ground") grounds.append(p.id);
            out.insert(comp->uid, QString("%1 not common with a board's GND").arg(grounds.join("/")));
        }
    }
    return out;
}



namespace {

QString railName(Project& project, const PowerNetwork& pn, int node) {
    // Name a rail after a board pin on it, else a source pin, else its first pin.
    const QList<PinRef>& pins = pn.nodes[node].pins;
    for (const PinRef& r : pins)
        if (Project::isBoard(project.getComponentByUid(r.uid))) return partName(project, r.uid) + " " + label(r.pin);
    for (const PowerNetwork::Supply& s : pn.supplies)
        if (s.node == node) return partName(project, s.uid) + " " + label(s.pin);
    return pins.isEmpty() ? QString("a rail") : partName(project, pins[0].uid) + " " + label(pins[0].pin);
}

double emuParam(const ComponentBlueprint* bp, const char* key, double def) {
    return bp->emulatorDef.parameters.contains(key) ? bp->emulatorDef.parameters.value(key).toDouble() : def;
}

bool isDriver(const ComponentInstance* comp) {
    return comp && comp->getBlueprint() && comp->getBlueprint()->emulatorDef.type == "hbridge_emulator";
}

// A motor on a driver's outputs, with its stall current through the driver
// at the driver's motor supply `volts` (the datasheet drops of both sides).
struct DrivenMotor { ComponentInstance* motor; double stallA; };
QList<DrivenMotor> drivenMotors(const Wiring& w, ComponentInstance* driver, double volts) {
    QList<DrivenMotor> out;
    const ComponentBlueprint* dbp = driver->getBlueprint();
    const double drop = emuParam(dbp, "high_v0", 0.0) + emuParam(dbp, "low_v0", 0.0);
    const double rOn = emuParam(dbp, "high_r", 0.0) + emuParam(dbp, "low_r", 0.0);
    QSet<int> seen;
    for (const PinDef& p : dbp->pins) {
        if (p.type != "motor_out") continue;
        for (const PinRef& r : w.pins(w.net(driver->uid, p.id), "motor")) {
            ComponentInstance* m = w.project.getComponentByUid(r.uid);
            if (!m || seen.contains(r.uid)) continue;
            seen.insert(r.uid);
            const ComponentBlueprint* mbp = m->getBlueprint();
            const double rated = emuParam(mbp, "rated_voltage", 12.0), stall = emuParam(mbp, "stall_current_a", 1.0);
            const double rArm = rated / std::max(1e-3, stall);
            out.append({m, std::max(0.0, volts - drop) / (rOn + rArm)});
        }
    }
    return out;
}

QString partTypeName(const ComponentInstance* comp) {
    const ComponentBlueprint* bp = comp->getBlueprint();
    return bp && !bp->meta.name.isEmpty() ? bp->meta.name : comp->modelId;
}

// The power checks: sources fighting, ranges, the budget, regulator headroom.
void checkPower(Project& project, QList<RuleIssue>& issues) {
    using S = RuleIssue::Severity;
    auto add = [&](S sev, const QString& msg, int uid = -1, const QString& pin = QString()) {
        issues.append(RuleIssue{sev, msg, uid, pin, -1});
    };
    const PowerNetwork pn = PowerNetwork::build(project);
    if (pn.isEmpty()) return;
    const Wiring w(project);
    if (pn.hasLoop) add(S::Error, "A regulator feeds its own input (through other rails): the power wiring loops");

    const PowerNetwork::Solution nominal = pn.solveStatic();

    // Two live sources on one rail: back-feeding (a short between supplies).
    for (int n = 0; n < pn.nodes.size(); ++n) {
        QStringList names;
        int uid = -1;
        QString pin;
        for (int si : pn.suppliesOn(n)) {
            names << partName(project, pn.supplies[si].uid) + " " + label(pn.supplies[si].pin);
            uid = pn.supplies[si].uid; pin = pn.supplies[si].pin;
        }
        for (int ri : pn.regulatorsInto(n)) {
            const auto& r = pn.regulators[ri];
            names << partName(project, r.uid) + " " + label(r.outPin);
            uid = r.uid;
        }
        if (names.size() > 1)
            add(S::Error, QString("Short: %1 drive the same rail (they back-feed each other)").arg(names.join(" and ")), uid, pin);
    }

    // Each powered part's supply against its range (at nominal load).
    for (ComponentInstance* comp : project.getComponentMap()) {
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp) continue;
        for (const PinDef& p : bp->pins) {
            if (p.type != "power" || p.voltageRange.second <= 0.0f) continue;
            const int node = pn.nodeOf(comp->uid, p.id);
            if (node < 0 || !pn.nodes[node].live) continue;
            const double v = nominal.voltage[node];
            if (v < p.voltageRange.first - 0.05 || v > p.voltageRange.second + 0.05)
                add(S::Warning, QString("%1 %2 takes %3-%4 V but gets %5 V from %6")
                        .arg(comp->name, label(p.id)).arg(p.voltageRange.first).arg(p.voltageRange.second)
                        .arg(v, 0, 'f', 2).arg(railName(project, pn, node)), comp->uid, p.id);
        }
    }

    // Regulator headroom at nominal load.
    for (int ri = 0; ri < pn.regulators.size(); ++ri) {
        const auto& r = pn.regulators[ri];
        if (!pn.nodes[r.in].live) continue;
        const double vin = nominal.voltage[r.in];
        if (vin > 0.0 && vin < r.vOut + r.dropout - 1e-6)
            add(S::Warning, QString("%1 gets %2 V: its %3 regulator needs at least %4 V for %5 V out (it gives %6 V)")
                    .arg(railName(project, pn, r.in)).arg(vin, 0, 'f', 2).arg(r.kind)
                    .arg(r.vOut + r.dropout, 0, 'f', 2).arg(r.vOut, 0, 'f', 2).arg(nominal.voltage[r.out], 0, 'f', 2), r.uid);
    }

    // Budget: typical (static loads) and peak (motors stalled) against each
    // source's limit; and whether a board's MCU rail survives the peak.
    QVector<double> peakExtra(pn.nodes.size(), 0.0);
    QHash<int, QStringList> motorsOn;   // node -> "2 x MG996R"
    QHash<int, QMap<QString, int>> motorCount;
    for (ComponentInstance* comp : project.getComponentMap()) {
        const QString pin = PowerNetwork::supplyPin(comp);
        const int node = pin.isEmpty() ? -1 : pn.nodeOf(comp->uid, pin);
        if (node < 0 || !pn.nodes[node].live) continue;
        const PowerLoadDef* l = comp->getBlueprint()->power.loadOn(pin);
        if (isDriver(comp)) {
            // A motor driver draws its motors' current from its supply: all stalled at once.
            const double rated = emuParam(comp->getBlueprint(), "channel_max_a", 0.0);
            for (const DrivenMotor& m : drivenMotors(w, comp, nominal.voltage[node])) {
                peakExtra[node] += m.stallA;
                motorCount[node][partTypeName(m.motor)]++;
                if (rated > 0.0 && m.stallA > rated + 1e-6)
                    add(S::Warning, QString("%1 stalls at %2 A through %3: over its %4 A per channel (it heats up and shuts down)")
                            .arg(m.motor->name).arg(m.stallA, 0, 'f', 2).arg(comp->name).arg(rated), comp->uid);
            }
            continue;
        }
        const double extra = PowerNetwork::peakCurrent(comp, nominal.voltage[node]) - (l ? l->current : 0.0);
        if (extra <= 1e-9) continue;
        peakExtra[node] += extra;
        motorCount[node][partTypeName(comp)]++;
    }
    const PowerNetwork::Solution peak = pn.solve(peakExtra, &nominal);
    auto motorsUpstream = [&](int node) {
        // the motors sharing this rail's source: up to the source, then down
        // every rail it feeds (a servo on 5V sags the 3V3 regulator behind it)
        QSet<int> nodes{node};
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& r : pn.regulators) if (nodes.contains(r.out) && !nodes.contains(r.in)) { nodes.insert(r.in); grew = true; }
        }
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& r : pn.regulators) if (nodes.contains(r.in) && !nodes.contains(r.out)) { nodes.insert(r.out); grew = true; }
        }
        QMap<QString, int> count;
        for (int n : nodes) for (auto it = motorCount[n].begin(); it != motorCount[n].end(); ++it) count[it.key()] += it.value();
        QStringList out;
        for (auto it = count.begin(); it != count.end(); ++it) out << (it.value() > 1 ? QString("%1 x %2").arg(it.value()).arg(it.key()) : it.key());
        return out.join(", ");
    };
    auto budget = [&](double typical, double peakA, double limit, const QString& what, const QString& limitText, int node, int uid) {
        if (limit <= 0.0) return;
        if (typical > limit + 1e-6)
            add(S::Error, QString("%1 draws %2 A at rest: over %3").arg(what).arg(typical, 0, 'f', 2).arg(limitText), uid);
        else if (peakA > limit + 1e-6)
            add(S::Warning, QString("%1 can draw %2 A with the motors stalled (%3): over %4")
                    .arg(what).arg(peakA, 0, 'f', 2).arg(motorsUpstream(node)).arg(limitText), uid);
    };
    for (int si = 0; si < pn.supplies.size(); ++si) {
        const auto& s = pn.supplies[si];
        if (s.dropped) continue;
        const QString what = railName(project, pn, s.node);
        if (s.kind == "usb")
            budget(nominal.supplyCurrent[si], peak.supplyCurrent[si], s.holdCurrent, what,
                   QString("the USB feed's %1 A (a real polyfuse cuts out, a PC port may shut down)").arg(s.holdCurrent), s.node, s.uid);
        else if (s.kind == "supply")
            budget(nominal.supplyCurrent[si], peak.supplyCurrent[si], s.currentLimit, what,
                   QString("its %1 A current limit (the voltage drops)").arg(s.currentLimit), s.node, s.uid);
    }
    for (int ri = 0; ri < pn.regulators.size(); ++ri) {
        const auto& r = pn.regulators[ri];
        if (!pn.nodes[r.in].live) continue;
        budget(nominal.regOut[ri], peak.regOut[ri], r.iMax, railName(project, pn, r.out),
               QString("its regulator's %1 A").arg(r.iMax), r.out, r.uid);
    }
    for (ComponentInstance* b : project.boards()) {
        const BrownoutDef& bo = b->getBlueprint()->power.brownout;
        if (!bo.present) continue;
        const int node = pn.nodeOf(b->uid, bo.rail);
        if (node < 0) continue;
        if (!pn.nodes[node].live) {
            add(S::Error, QString("%1 is not powered: nothing feeds its %2 rail").arg(b->name, label(bo.rail)), b->uid, bo.rail);
            continue;
        }
        if (nominal.voltage[node] < bo.threshold)
            add(S::Error, QString("%1 %2 is at %3 V at rest: below the %4 V brownout, the board can't start")
                    .arg(b->name, label(bo.rail)).arg(nominal.voltage[node], 0, 'f', 2).arg(bo.threshold), b->uid, bo.rail);
        else if (peak.voltage[node] < bo.threshold)
            add(S::Warning, QString("%1 %2 would drop to %3 V with the motors stalled (%4): below the %5 V brownout, the board resets. "
                                    "Power the motors from their own supply (common GND)")
                    .arg(b->name, label(bo.rail)).arg(peak.voltage[node], 0, 'f', 2).arg(motorsUpstream(node)).arg(bo.threshold),
                b->uid, bo.rail);
    }
}

} // namespace



QList<RuleIssue> DesignRules::check(Project& project) {
    QList<RuleIssue> issues;
    if (project.getLevel() != ProjectLevel::Real) return issues;
    auto add = [&](RuleIssue::Severity s, const QString& msg, int uid = -1, const QString& pin = QString(), int net = -1) {
        issues.append(RuleIssue{s, msg, uid, pin, net});
    };
    using S = RuleIssue::Severity;

    // ---- boards ----
    const QList<ComponentInstance*> boards = project.boards();
    ComponentInstance* active = project.activeBoard();
    if (boards.isEmpty()) {
        add(S::Error, "No board: add one from the library (Boards). Without a board Play runs the physics only.");
        return issues;
    }
    for (ComponentInstance* b : boards) {
        if (b != active)
            add(S::Warning, QString("%1 stays idle: one board runs at a time until boards can talk to each other (UART)").arg(b->name), b->uid);
    }
    if (active->firmware.isEmpty())
        add(S::Warning, QString("%1 has no firmware: pick a sketch in its Inspector").arg(active->name), active->uid);
    const BoardDef& def = active->getBlueprint()->board;

    const Wiring w(project);

    // ---- nets: shorts ----
    for (const Net& n : w.nets) {
        QList<PinRef> gpios = w.boardPins(n.id, "gpio");
        QList<PinRef> sources = w.pins(n.id, "power_out");
        QList<PinRef> grounds = w.pins(n.id, "ground");
        QStringList names;
        for (const PinRef& r : gpios) names.append(partName(project, r.uid) + " " + label(r.pin));
        if (gpios.size() > 1)
            add(S::Error, QString("Short: %1 are wired together").arg(names.join(" and ")), gpios[0].uid, gpios[0].pin, n.id);
        else if (!gpios.isEmpty() && !sources.isEmpty())
            add(S::Error, QString("Short: %1 is wired to %2 %3").arg(names.join(""), partName(project, sources[0].uid), label(sources[0].pin)),
                gpios[0].uid, gpios[0].pin, n.id);
        if (!sources.isEmpty() && !grounds.isEmpty())
            add(S::Error, QString("Short: %1 %2 is wired to ground").arg(partName(project, sources[0].uid), label(sources[0].pin)),
                sources[0].uid, sources[0].pin, n.id);
        // A motor driver's output fights anything else that drives the net.
        const QList<PinRef> outs = w.pins(n.id, "motor_out");
        if (!outs.isEmpty()) {
            const QString out0 = partName(project, outs[0].uid) + " " + label(outs[0].pin);
            QString other;
            if (outs.size() > 1) other = partName(project, outs[1].uid) + " " + label(outs[1].pin);
            else if (!gpios.isEmpty()) other = partName(project, gpios[0].uid) + " " + label(gpios[0].pin);
            else if (!sources.isEmpty()) other = partName(project, sources[0].uid) + " " + label(sources[0].pin);
            else if (!grounds.isEmpty()) other = partName(project, grounds[0].uid) + " " + label(grounds[0].pin);
            if (!other.isEmpty())
                add(S::Error, QString("Short: %1 is wired to %2 (a driver output fights it when it drives the other way)").arg(out0, other),
                    outs[0].uid, outs[0].pin, n.id);
        }
        for (const PinRef& r : n.pins) {
            const PinDef* p = w.pinDef(r);
            if (p && p->internal)
                add(S::Error, QString("%1 %2 is wired on the board itself and can't take a wire").arg(partName(project, r.uid), label(r.pin)), r.uid, r.pin, n.id);
        }
    }

    // ---- parts ----
    const QHash<int, QString> unpowered = unpoweredParts(project);
    // The active board's bus pins (pin ids on the board), by bus line type.
    const QMap<QString, QString> busPin{
        {"i2c_sda", def.i2cSda}, {"i2c_scl", def.i2cScl},
        {"spi_sck", def.spiSck}, {"spi_mosi", def.spiMosi}, {"spi_miso", def.spiMiso}};

    for (ComponentInstance* comp : project.getComponentMap()) {
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp || bp->board.present) continue;
        const QString name = comp->name;

        if (unpowered.contains(comp->uid))
            add(S::Error, QString("%1 is not powered (%2): it stays dead").arg(name, unpowered[comp->uid]), comp->uid);

        QStringList floating;
        for (const PinDef& p : bp->pins) {
            const int net = w.net(comp->uid, p.id);

            if (!p.isBoardPin()) continue;

            const QList<PinRef> gpios = w.boardPins(net, "gpio");
            const bool onActive = std::any_of(gpios.begin(), gpios.end(), [&](const PinRef& r) { return r.uid == active->uid; });
            if (net < 0 || gpios.isEmpty()) {
                // Wired only to other parts (e.g. part to part) is fine; nothing at all floats.
                if (net < 0) floating.append(p.id);
                continue;
            }
            if (!onActive) continue;   // on an idle board: reported with that board
            const PinRef board = *std::find_if(gpios.begin(), gpios.end(), [&](const PinRef& r) { return r.uid == active->uid; });
            const PinDef* g = w.pinDef(board);
            if (!g) continue;
            const QString where = QString("%1 %2").arg(active->name, label(board.pin));

            if (p.type == "motor") {
                // A board pin is a signal, not a power output: it can't run a motor.
                const double stall = emuParam(bp, "stall_current_a", 1.0) * def.logicVoltage / emuParam(bp, "rated_voltage", 12.0);
                add(S::Error, QString("%1 %2 is wired straight to %3: a board pin gives at most %4 mA, this motor wants up to "
                                      "%5 mA at stall. It barely moves and the pin is overloaded: drive it through a motor driver "
                                      "(Components -> Drivers)")
                        .arg(name, label(p.id), where).arg(def.pinAbsMaxA > 0.0 ? def.pinAbsMaxA * 1000.0 : 40.0, 0, 'f', 0)
                        .arg(stall * 1000.0, 0, 'f', 0), comp->uid, p.id, net);
                continue;
            }

            if (busPin.contains(p.type)) {
                const QString want = busPin.value(p.type);
                if (want.isEmpty())
                    add(S::Warning, QString("%1 %2: %3 has no hardware bus for it").arg(name, label(p.id), active->name), comp->uid, p.id, net);
                else if (want != board.pin)
                    add(S::Warning, QString("%1 %2 is on %3, not on the board's %4 pin (%5): the part won't answer")
                            .arg(name, label(p.id), where, p.type.toUpper().replace('_', ' '), label(want)), comp->uid, p.id, net);
            } else if (p.type == "pwm" && !g->hasCap("pwm")) {
                add(S::Warning, QString("%1 %2 is on %3, which has no hardware PWM: Servo pulses work, analogWrite() is only on/off")
                        .arg(name, label(p.id), where), comp->uid, p.id, net);
            } else if (p.type == "analog" && !g->hasCap("analog")) {
                add(S::Error, QString("%1 %2 is on %3, which has no ADC: analogRead() can't read it").arg(name, label(p.id), where),
                    comp->uid, p.id, net);
            } else if ((p.type == "uart_tx" || p.type == "uart_rx") && !g->hasCap(p.type == "uart_tx" ? "uart_rx" : "uart_tx")) {
                add(S::Warning, QString("%1 %2 is on %3, which is not a hardware UART pin").arg(name, label(p.id), where),
                    comp->uid, p.id, net);
            }

            const double limit = signalLimit(bp, p);
            if (limit > 0.0 && def.logicVoltage > limit + 0.3)
                add(S::Warning, QString("%1 %2 gets %3 V logic from %4 but takes at most %5 V: add a level shifter")
                        .arg(name, label(p.id)).arg(def.logicVoltage).arg(where).arg(limit), comp->uid, p.id, net);
        }
        // L298N module: an EN pin wired while its jumper still ties it to 5 V.
        if (isDriver(comp) && bp->emulatorDef.parameters.value("truth_table").toString() == "l298") {
            for (const auto& [enPin, key] : {std::pair<QString, QString>{"ena", "driver.ena_jumper"}, {"enb", "driver.enb_jumper"}}) {
                const int net = w.net(comp->uid, enPin);
                if (net >= 0 && !w.boardPins(net, "gpio").isEmpty() && comp->parameters.value(key, true).toBool())
                    add(S::Warning, QString("%1 %2 is wired, but its jumper still ties it to the module's 5 V: the board pin fights "
                                            "the jumper. Remove the jumper (Inspector) to drive %2 from the board")
                            .arg(name, label(enPin)), comp->uid, enPin, net);
            }
        }
        // A dual driver: a channel whose outputs aren't wired is simply unused;
        // an L298N EN pin under its jumper needs no wire.
        if (isDriver(comp)) {
            const bool tb = bp->emulatorDef.parameters.value("truth_table").toString() == "tb6612";
            const QList<QPair<QStringList, QStringList>> channels = tb
                ? QList<QPair<QStringList, QStringList>>{{{"ain1", "ain2", "pwma"}, {"ao1", "ao2"}}, {{"bin1", "bin2", "pwmb"}, {"bo1", "bo2"}}}
                : QList<QPair<QStringList, QStringList>>{{{"in1", "in2", "ena"}, {"out1", "out2"}}, {{"in3", "in4", "enb"}, {"out3", "out4"}}};
            bool anyUsed = false;
            for (const auto& ch : channels) {
                bool used = false;
                for (const QString& o : ch.second) used = used || w.net(comp->uid, o) >= 0;
                anyUsed = anyUsed || used;
                if (!used) for (const QString& i : ch.first) floating.removeAll(i);
            }
            if (!anyUsed) floating.removeAll("stby");
            if (comp->parameters.value("driver.ena_jumper", true).toBool()) floating.removeAll("ena");
            if (comp->parameters.value("driver.enb_jumper", true).toBool()) floating.removeAll("enb");
        }
        if (!floating.isEmpty()) {
            QStringList labels;
            for (const QString& f : floating) labels.append(label(f));
            add(S::Warning, QString("%1: not wired: %2").arg(name, labels.join(", ")), comp->uid, floating.size() == 1 ? floating[0] : QString());
        }
    }
    checkPower(project, issues);
    return issues;
}
