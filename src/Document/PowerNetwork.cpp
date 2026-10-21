#include "PowerNetwork.h"

#include <QMap>
#include <algorithm>
#include <functional>
#include "Document/Project.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"



namespace {

double param(const ComponentInstance* comp, const char* key, double def) {
    return comp->parameters.contains(key) ? comp->parameters.value(key).toDouble() : def;
}

double emulatorParam(const ComponentBlueprint* bp, const char* key, double def) {
    return bp->emulatorDef.parameters.contains(key) ? bp->emulatorDef.parameters.value(key).toDouble() : def;
}

// Union-find over small integer ids.
struct Groups {
    QVector<int> parent;
    int add() { parent.append(parent.size()); return parent.size() - 1; }
    int find(int i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; }
    void join(int a, int b) { a = find(a); b = find(b); if (a != b) parent[std::max(a, b)] = std::min(a, b); }
};

} // namespace



QString PowerNetwork::supplyPin(const ComponentInstance* comp) {
    const ComponentBlueprint* bp = comp ? comp->getBlueprint() : nullptr;
    if (!bp) return QString();
    for (const PinDef& p : bp->pins) if (p.type == "power") return p.id;
    return QString();
}


double PowerNetwork::peakCurrent(const ComponentInstance* comp, double supplyVolts) {
    const ComponentBlueprint* bp = comp ? comp->getBlueprint() : nullptr;
    if (!bp) return 0.0;
    double amps = 0.0;
    const QString pin = supplyPin(comp);
    if (const PowerLoadDef* l = bp->power.loadOn(pin)) amps += l->current;
    const double stall = emulatorParam(bp, "stall_current_a", 0.0);
    const double rated = emulatorParam(bp, "rated_voltage", 0.0);
    if (stall > 0.0 && rated > 0.0) amps += stall * std::max(0.0, supplyVolts) / rated;
    const double phase = emulatorParam(bp, "rated_current_a", 0.0);
    const double ohms = emulatorParam(bp, "phase_resistance_ohm", 0.0);
    if (phase > 0.0 && ohms > 0.0 && supplyVolts > 0.0) amps += 2.0 * phase * phase * ohms / supplyVolts;
    return amps;
}


PowerNetwork PowerNetwork::build(Project& project) {
    PowerNetwork pn;
    const QList<Net> nets = project.netlist().nets();
    const QHash<PinRef, int> netOf = Netlist::indexOf(nets);
    QHash<int, int> nodeOfNet;
    Groups grounds;
    QHash<PinRef, int> groundId;

    // Parts in uid order, so node and source indices are stable.
    QList<ComponentInstance*> parts;
    for (ComponentInstance* comp : project.getComponentMap()) if (comp && comp->getBlueprint()) parts.append(comp);
    std::sort(parts.begin(), parts.end(), [](ComponentInstance* a, ComponentInstance* b) { return a->uid < b->uid; });

    // ---- rails: one node per power net, or per unwired power pin ----
    for (ComponentInstance* comp : parts) {
        for (const PinDef& p : comp->getBlueprint()->pins) {
            const PinRef ref{comp->uid, p.id};
            if (p.type == "ground") {
                groundId.insert(ref, grounds.add());
                continue;
            }
            if (!p.isPower()) continue;
            const int net = netOf.value(ref, -1);
            int node = net >= 0 ? nodeOfNet.value(net, -1) : -1;
            if (node < 0) {
                node = pn.nodes.size();
                pn.nodes.append(Node{});
                if (net >= 0) nodeOfNet.insert(net, node);
            }
            pn.nodes[node].pins.append(ref);
            pn.nodeIndex.insert(ref, node);
        }
    }

    // ---- ground domains: common inside a part, joined by wires ----
    for (ComponentInstance* comp : parts) {
        int first = -1;
        for (const PinDef& p : comp->getBlueprint()->pins) {
            if (p.type != "ground") continue;
            const int id = groundId.value(PinRef{comp->uid, p.id});
            if (first < 0) first = id; else grounds.join(first, id);
        }
    }
    for (const Net& n : nets) {
        int first = -1;
        for (const PinRef& r : n.pins) {
            if (!groundId.contains(r)) continue;
            if (first < 0) first = groundId[r]; else grounds.join(first, groundId[r]);
        }
    }
    for (auto it = groundId.begin(); it != groundId.end(); ++it) pn.groundDomain.insert(it.key(), grounds.find(it.value()));

    // ---- sources, regulators, loads ----
    for (ComponentInstance* comp : parts) {
        const ComponentBlueprint* bp = comp->getBlueprint();
        const PowerDef& pw = bp->power;
        for (const SupplyDef& s : pw.supplies) {
            Supply d;
            d.uid = comp->uid;
            d.pin = s.pin;
            d.kind = s.kind;
            d.node = pn.nodeOf(comp->uid, s.pin);
            d.voc = s.voltage;
            d.r = s.rInternal;
            d.currentLimit = s.currentLimit;
            d.holdCurrent = s.holdCurrent;
            if (s.kind == "battery") {
                const BatteryDef::Point pt = pw.battery.packAt(std::clamp(param(comp, kChargePercent, 100.0), 0.0, 100.0));
                d.voc = pt.voltage;
                d.r = pt.resistance;
            } else if (s.kind == "supply") {
                d.voc = std::max(0.0, param(comp, kVoltage, s.voltage));
                d.currentLimit = std::max(0.0, param(comp, kCurrentLimit, s.currentLimit));
            }
            if (d.node >= 0) pn.supplies.append(d);
        }
        for (const RegulatorDef& r : pw.regulators) {
            Regulator d;
            d.uid = comp->uid;
            d.id = r.id;
            d.kind = r.kind;
            d.in = pn.nodeOf(comp->uid, r.in);
            d.out = pn.nodeOf(comp->uid, r.out);
            d.outPin = r.out;
            d.vOut = r.kind == "buck" ? std::max(0.0, param(comp, kVOut, r.vOut)) : r.vOut;
            d.dropout = r.dropout;
            d.efficiency = r.kind == "buck" ? r.efficiency : 1.0;
            d.iMax = r.iMax;
            d.iLimit = r.iLimit > 0.0 ? r.iLimit : r.iMax;
            d.iq = r.iQuiescent;
            if (d.in >= 0 && d.out >= 0) pn.regulators.append(d);
        }
        for (const PinDef& p : bp->pins) {
            if (!p.isPower()) continue;
            const PowerLoadDef* l = pw.loadOn(p.id);
            if (p.type != "power" && !l) continue;   // a rail with no draw of its own
            Load d;
            d.uid = comp->uid;
            d.pin = p.id;
            d.node = pn.nodeOf(comp->uid, p.id);
            d.current = l ? l->current : 0.0;
            d.minV = l ? l->minV : 0.0;
            pn.loads.append(d);
        }
    }

    // ---- which rails a source can reach (fixed point over the regulators) ----
    for (const Supply& s : pn.supplies) pn.nodes[s.node].live = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (const Regulator& r : pn.regulators) {
            if (pn.nodes[r.in].live && !pn.nodes[r.out].live) { pn.nodes[r.out].live = true; changed = true; }
        }
    }
    // A board's USB feed gives way to any other source on its rail: a supply,
    // or a regulator fed from something other than USB (the board is powered
    // externally; on a real Uno the comparator switches USB off).
    QVector<bool> external(pn.nodes.size(), false);
    for (const Supply& s : pn.supplies) if (s.kind != "usb") external[s.node] = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (const Regulator& r : pn.regulators) {
            if (external[r.in] && !external[r.out]) { external[r.out] = true; changed = true; }
        }
    }
    for (Supply& s : pn.supplies) {
        if (s.kind != "usb") continue;
        for (const Supply& o : pn.supplies) s.dropped = s.dropped || (o.node == s.node && o.kind != "usb");
        for (const Regulator& r : pn.regulators) s.dropped = s.dropped || (r.out == s.node && r.in != s.node && external[r.in]);
    }

    // ---- solve order: rails before the regulators they feed ----
    QVector<int> indegree(pn.nodes.size(), 0);
    for (const Regulator& r : pn.regulators) if (r.in != r.out) ++indegree[r.out];
    QList<int> ready;
    for (int n = 0; n < pn.nodes.size(); ++n) if (indegree[n] == 0) ready.append(n);
    while (!ready.isEmpty()) {
        const int n = ready.takeFirst();
        pn.order.append(n);
        for (const Regulator& r : pn.regulators) {
            if (r.in == n && r.out != n && --indegree[r.out] == 0) ready.append(r.out);
        }
    }
    pn.hasLoop = pn.order.size() < pn.nodes.size();
    for (const Regulator& r : pn.regulators) pn.hasLoop = pn.hasLoop || r.in == r.out;
    for (int n = 0; n < pn.nodes.size(); ++n) if (!pn.order.contains(n)) pn.order.append(n);
    return pn;
}


int PowerNetwork::groundDomainOfPart(int uid) const {
    int best = -1;
    for (auto it = groundDomain.begin(); it != groundDomain.end(); ++it) {
        if (it.key().uid != uid) continue;
        if (best < 0 || it.value() < best) best = it.value();   // one domain per part anyway
    }
    return best;
}


QList<int> PowerNetwork::suppliesOn(int node) const {
    QList<int> out;
    for (int i = 0; i < supplies.size(); ++i) if (supplies[i].node == node && !supplies[i].dropped) out.append(i);
    return out;
}


QList<int> PowerNetwork::regulatorsInto(int node) const {
    QList<int> out;
    for (int i = 0; i < regulators.size(); ++i)
        if (regulators[i].out == node && regulators[i].in != node && nodes[regulators[i].in].live) out.append(i);
    return out;
}


PowerNetwork::Solution PowerNetwork::solve(const QVector<double>& extra, const Solution* previous) const {
    const int n = nodes.size();
    Solution s;
    s.voltage = previous && previous->voltage.size() == n ? previous->voltage : QVector<double>(n, 0.0);
    s.demand = QVector<double>(n, 0.0);
    s.supplyCurrent = QVector<double>(supplies.size(), 0.0);
    s.regIn = QVector<double>(regulators.size(), 0.0);
    s.regOut = QVector<double>(regulators.size(), 0.0);

    QVector<double> base(n, 0.0);
    for (const Load& l : loads) base[l.node] += l.current;
    for (int i = 0; i < n && i < extra.size(); ++i) base[i] += extra[i];

    auto regOutput = [&](const Regulator& r, double vin, double iout) {
        if (vin <= 0.0) return 0.0;
        double v = std::max(0.0, std::min(r.vOut, vin - r.dropout));
        if (r.iLimit > 0.0 && iout > r.iLimit) v *= r.iLimit / iout;
        return v;
    };
    auto regInput = [&](const Regulator& r, double vin, double vout, double iout) {
        if (vin <= 0.0) return 0.0;
        if (r.kind == "buck" && vin - r.dropout > r.vOut && vout > 0.0)
            return vout * iout / (r.efficiency * vin) + r.iq;
        return iout + r.iq;
    };

    // A few passes: currents bottom-up with the voltages so far, then
    // voltages top-down with those currents. The chains are short (a battery,
    // a buck, a board regulator), so this settles in 2-3 passes.
    for (int pass = 0; pass < 4; ++pass) {
        // currents: from the last rail in the order back to the sources
        QVector<double> demand = base;
        for (int k = order.size() - 1; k >= 0; --k) {
            const int node = order[k];
            // The whole rail's demand goes to its winning source (the highest
            // voltage now); the other sources on the rail carry nothing.
            const QList<int> regs = regulatorsInto(node);
            const QList<int> sups = suppliesOn(node);
            int winReg = -1, winSup = -1;
            double best = -1.0;
            for (int si : sups) {
                if (supplies[si].voc > best) { best = supplies[si].voc; winSup = si; winReg = -1; }
            }
            for (int ri : regs) {
                const double v = regOutput(regulators[ri], s.voltage[regulators[ri].in], demand[node]);
                if (v > best) { best = v; winReg = ri; winSup = -1; }
            }
            for (int si : sups) s.supplyCurrent[si] = si == winSup ? demand[node] : 0.0;
            if (winReg >= 0) {
                const Regulator& r = regulators[winReg];
                s.regOut[winReg] = demand[node];
                const double vout = regOutput(r, s.voltage[r.in], demand[node]);
                s.regIn[winReg] = regInput(r, s.voltage[r.in], vout, demand[node]);
                demand[r.in] += s.regIn[winReg];
            }
            for (int ri : regs) if (ri != winReg) s.regIn[ri] = s.regOut[ri] = 0.0;
        }
        s.demand = demand;

        // voltages: sources first, then each regulator's output
        for (int node : order) {
            double v = 0.0;
            for (int si : suppliesOn(node)) {
                const Supply& sp = supplies[si];
                const double i = s.supplyCurrent[si];
                double vs = sp.voc - i * sp.r;
                if (sp.currentLimit > 0.0 && i > sp.currentLimit) vs = std::min(vs, sp.voc * sp.currentLimit / i);
                v = std::max(v, vs);
            }
            for (int ri : regulatorsInto(node)) {
                const Regulator& r = regulators[ri];
                v = std::max(v, regOutput(r, s.voltage[r.in], s.demand[node]));
            }
            s.voltage[node] = std::max(0.0, v);
        }
    }
    return s;
}
