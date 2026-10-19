#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>


// One pin of one part: (component uid, pin id in its .rsdef).
struct PinRef {
    int uid = -1;
    QString pin;

    bool isValid() const { return uid >= 0 && !pin.isEmpty(); }
    bool operator==(const PinRef& o) const { return uid == o.uid && pin == o.pin; }
    bool operator!=(const PinRef& o) const { return !(*this == o); }
    bool operator<(const PinRef& o) const { return uid != o.uid ? uid < o.uid : pin < o.pin; }
};

inline size_t qHash(const PinRef& r, size_t seed = 0) { return qHashMulti(seed, r.uid, r.pin); }


// A two-point wire the user drew between two pins. Undirected: (a, b) and
// (b, a) are the same wire.
struct Wire {
    PinRef a, b;

    bool sameAs(const Wire& o) const { return (a == o.a && b == o.b) || (a == o.b && b == o.a); }
    bool touches(int uid) const { return a.uid == uid || b.uid == uid; }
};


// Pins joined by wires, directly or through other wires. The simulation and
// the design-rule check work on nets, not wires: power and ground join many
// pins, one board pin can drive two servos, and two outputs on one net are a
// short, not an overwrite.
struct Net {
    int id = -1;           // index in Netlist::nets(), stable for one call
    QList<PinRef> pins;    // sorted

    bool contains(const PinRef& r) const { return pins.contains(r); }
};


/*
 * The wiring of a project (Real level): the wires as the user drew them.
 * Nets are derived on demand (union-find), so there is nothing to keep in
 * sync when a wire is added or removed. Pure document data: undo goes
 * through commands that add/remove wires (Commands/WiringCommands.h).
 */
class Netlist {
    QList<Wire> wireList;

    public:
        const QList<Wire>& wires() const { return wireList; }
        bool isEmpty() const { return wireList.isEmpty(); }
        void clear() { wireList.clear(); }

        // False (and nothing changes) for a wire from a pin to itself or one
        // that already exists.
        bool addWire(const Wire& w);
        bool removeWire(const Wire& w);
        bool hasWire(const Wire& w) const;

        QList<Wire> wiresOf(const QSet<int>& uids) const;   // wires touching any of these parts
        void removeWiresOf(const QSet<int>& uids);

        // Every net with at least one wire, in a stable order (by first pin).
        QList<Net> nets() const;
        // pin -> index into nets()
        static QHash<PinRef, int> indexOf(const QList<Net>& nets);

        static Netlist fromJson(const QJsonArray& wires);
        QJsonArray toJson() const;
};
