#include "DesignRules.h"

#include <QMap>
#include <algorithm>
#include "Document/Project.h"
#include "Document/Netlist.h"
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
    const Wiring w(project);

    for (ComponentInstance* comp : project.getComponentMap()) {
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp || bp->board.present) continue;   // boards: powered (the power model comes later)
        QStringList unpowered, grounds;
        bool grounded = false;
        for (const PinDef& p : bp->pins) {
            if (p.type == "power" && w.boardPins(w.net(comp->uid, p.id), "power_out").isEmpty()) unpowered.append(p.id);
            if (p.type == "ground") {
                grounds.append(p.id);
                grounded = grounded || !w.boardPins(w.net(comp->uid, p.id), "ground").isEmpty();
            }
        }
        if (!unpowered.isEmpty())
            out.insert(comp->uid, QString("no supply on %1").arg(unpowered.join(", ")));
        else if (!grounds.isEmpty() && !grounded)
            out.insert(comp->uid, QString("%1 not wired to a board's GND").arg(grounds.join("/")));
    }
    return out;
}



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
        QList<PinRef> sources = w.boardPins(n.id, "power_out");
        QList<PinRef> grounds = w.boardPins(n.id, "ground");
        QStringList names;
        for (const PinRef& r : gpios + sources) names.append(partName(project, r.uid) + " " + label(r.pin));
        if (gpios.size() + sources.size() > 1)
            add(S::Error, QString("Short: %1 are wired together").arg(names.join(" and ")), gpios.value(0, sources.value(0)).uid,
                gpios.value(0, sources.value(0)).pin, n.id);
        else if (!sources.isEmpty() && !grounds.isEmpty())
            add(S::Error, QString("Short: %1 is wired to ground").arg(names.join("")), sources[0].uid, sources[0].pin, n.id);
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

            if (p.type == "power" && net >= 0) {
                for (const PinRef& src : w.boardPins(net, "power_out")) {
                    const PinDef* s = w.pinDef(src);
                    const double v = s ? s->voltageRange.first : 0.0;
                    if (p.voltageRange.second > 0.0f && (v < p.voltageRange.first - 0.05 || v > p.voltageRange.second + 0.05))
                        add(S::Warning, QString("%1 %2 takes %3-%4 V but is on %5 (%6 V)")
                                .arg(name, label(p.id)).arg(p.voltageRange.first).arg(p.voltageRange.second)
                                .arg(label(src.pin)).arg(v), comp->uid, p.id, net);
                }
            }
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
        if (!floating.isEmpty()) {
            QStringList labels;
            for (const QString& f : floating) labels.append(label(f));
            add(S::Warning, QString("%1: not wired: %2").arg(name, labels.join(", ")), comp->uid, floating.size() == 1 ? floating[0] : QString());
        }
    }
    return issues;
}
