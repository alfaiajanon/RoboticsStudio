#include "Netlist.h"

#include <QJsonObject>
#include <algorithm>



bool Netlist::addWire(const Wire& w) {
    if (!w.a.isValid() || !w.b.isValid() || w.a == w.b || hasWire(w)) return false;
    wireList.append(w);
    return true;
}



bool Netlist::removeWire(const Wire& w) {
    for (int i = 0; i < wireList.size(); ++i) {
        if (wireList[i].sameAs(w)) {
            wireList.removeAt(i);
            return true;
        }
    }
    return false;
}



bool Netlist::hasWire(const Wire& w) const {
    for (const Wire& x : wireList) if (x.sameAs(w)) return true;
    return false;
}



QList<Wire> Netlist::wiresOf(const QSet<int>& uids) const {
    QList<Wire> out;
    for (const Wire& w : wireList) if (uids.contains(w.a.uid) || uids.contains(w.b.uid)) out.append(w);
    return out;
}



void Netlist::removeWiresOf(const QSet<int>& uids) {
    wireList.removeIf([&](const Wire& w) { return uids.contains(w.a.uid) || uids.contains(w.b.uid); });
}



QList<Net> Netlist::nets() const {
    // Union-find over the pins that have wires.
    QHash<PinRef, int> index;
    QList<PinRef> pins;
    auto idOf = [&](const PinRef& r) {
        auto it = index.find(r);
        if (it != index.end()) return it.value();
        index.insert(r, pins.size());
        pins.append(r);
        return int(pins.size() - 1);
    };
    QList<int> parent;
    auto find = [&](int x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (const Wire& w : wireList) {
        const int a = idOf(w.a), b = idOf(w.b);
        while (parent.size() < pins.size()) parent.append(parent.size());
        const int ra = find(a), rb = find(b);
        if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
    }

    QHash<int, int> netOfRoot;
    QList<Net> out;
    for (int i = 0; i < pins.size(); ++i) {
        const int r = find(i);
        auto it = netOfRoot.find(r);
        if (it == netOfRoot.end()) {
            it = netOfRoot.insert(r, out.size());
            out.append(Net{});
        }
        out[it.value()].pins.append(pins[i]);
    }
    for (Net& n : out) std::sort(n.pins.begin(), n.pins.end());
    std::sort(out.begin(), out.end(), [](const Net& x, const Net& y) { return x.pins.first() < y.pins.first(); });
    for (int i = 0; i < out.size(); ++i) out[i].id = i;
    return out;
}



QHash<PinRef, int> Netlist::indexOf(const QList<Net>& nets) {
    QHash<PinRef, int> out;
    for (const Net& n : nets) for (const PinRef& r : n.pins) out.insert(r, n.id);
    return out;
}



static PinRef pinRefFromJson(const QJsonObject& o) {
    return PinRef{o["uid"].toInt(-1), o["pin"].toString()};
}

static QJsonObject pinRefToJson(const PinRef& r) {
    return QJsonObject{{"uid", r.uid}, {"pin", r.pin}};
}



Netlist Netlist::fromJson(const QJsonArray& wires) {
    Netlist n;
    for (const QJsonValue& v : wires) {
        const QJsonObject o = v.toObject();
        n.addWire(Wire{pinRefFromJson(o["a"].toObject()), pinRefFromJson(o["b"].toObject())});
    }
    return n;
}



QJsonArray Netlist::toJson() const {
    QJsonArray arr;
    for (const Wire& w : wireList) arr.append(QJsonObject{{"a", pinRefToJson(w.a)}, {"b", pinRefToJson(w.b)}});
    return arr;
}
