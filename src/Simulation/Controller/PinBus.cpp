#include "PinBus.h"

#include <algorithm>
#include "Document/IdealBoard.h"
#include "Document/RealBoard.h"
#include "Document/Netlist.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"



void PinBus::build(const IdealBoard& board) {
    clear();
    for (const BoardPin& p : board.pins()) {
        PinState s;
        s.type = p.type;
        const int h = int(states.size());
        states.push_back(s);
        byPart.insert(partKey(p.uid, p.pinId), h);
        byNumber.insert(p.number, h);
    }
}



void PinBus::build(const RealBoard& board, const Netlist& netlist) {
    clear();
    for (const Net& net : netlist.nets()) {
        PinState s;
        s.type = "net";
        const int h = int(states.size());
        states.push_back(s);
        for (const PinRef& r : net.pins) byPart.insert(partKey(r.uid, r.pin), h);
    }
    ComponentInstance* comp = board.instance();
    if (!comp || !comp->getBlueprint()) return;
    for (const PinDef& p : comp->getBlueprint()->pins) {
        if (p.number < 0) continue;
        byNumber.insert(p.number, ensurePin(comp->uid, p.id, p.type));
    }
}



void PinBus::clear() {
    states.clear();
    byPart.clear();
    byNumber.clear();
}



int PinBus::handleOf(int uid, const QString& pinId) const {
    return byPart.value(partKey(uid, pinId), -1);
}



int PinBus::handleForNumber(int boardNumber) const {
    return byNumber.value(boardNumber, -1);
}



int PinBus::ensurePin(int uid, const QString& pinId, const QString& type) {
    const int existing = handleOf(uid, pinId);
    if (existing >= 0) return existing;
    PinState s;
    s.type = type;
    const int h = int(states.size());
    states.push_back(s);
    byPart.insert(partKey(uid, pinId), h);   // no board number: firmware can't reach it
    return h;
}



void PinBus::writeAnalog(int h, int value, double periodUs) {
    if (value <= 0) writeLevel(h, 0);
    else if (value >= 255) writeLevel(h, 1);
    else writePulseUs(h, value / 255.0 * periodUs, periodUs);
}



double PinBus::dutyCycle(int h, double periodUs) const {
    if (!valid(h)) return 0.0;
    const PinState& s = states[h];
    if (s.drive == PinState::Drive::Level) return s.level ? 1.0 : 0.0;
    if (s.periodUs > 0.0) periodUs = s.periodUs;
    if (s.drive == PinState::Drive::Pulse && periodUs > 0.0) return std::clamp(s.pulseUs / periodUs, 0.0, 1.0);
    return 0.0;
}



void PinBus::writePulseUs(int h, double us, double periodUs) {
    if (!valid(h)) return;
    states[h].pulseUs = us;
    states[h].periodUs = periodUs;
    states[h].drive = PinState::Drive::Pulse;
    states[h].driven = true;
}



bool PinBus::pulse(int h, double* us) const {
    if (!valid(h) || states[h].drive != PinState::Drive::Pulse) return false;
    if (us) *us = states[h].pulseUs;
    return true;
}



void PinBus::writeLevel(int h, int level) {
    if (!valid(h)) return;
    const bool wasHigh = states[h].drive == PinState::Drive::Level && states[h].level == 1;
    if (level && !wasHigh) ++states[h].risingEdges;
    states[h].level = level ? 1 : 0;
    states[h].drive = PinState::Drive::Level;   // a steady level: no pulses
    states[h].driven = true;
}



int PinBus::level(int h) const {
    return valid(h) ? states[h].level : 0;
}



int PinBus::takeRisingEdges(int h) {
    if (!valid(h)) return 0;
    const int n = states[h].risingEdges;
    states[h].risingEdges = 0;
    return n;
}



void PinBus::writeVoltage(int h, double volts) {
    if (!valid(h)) return;
    states[h].voltage = volts;
    states[h].drive = PinState::Drive::Voltage;
    states[h].driven = true;
}



void PinBus::release(int h) {
    if (!valid(h)) return;
    states[h].drive = PinState::Drive::None;
    states[h].driven = false;
}



double PinBus::voltage(int h) const {
    return valid(h) ? states[h].voltage : 0.0;
}
