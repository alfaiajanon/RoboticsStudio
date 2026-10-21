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
        s.ideal = true;
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
    const BoardDef& def = comp->getBlueprint()->board;
    for (const PinDef& p : comp->getBlueprint()->pins) {
        if (p.number < 0) continue;
        const int h = ensurePin(comp->uid, p.id, p.type);
        byNumber.insert(p.number, h);
        if (p.type == "gpio") {
            states[h].onBoard = true;
            states[h].boardOhm = def.pinOutputOhm;
            states[h].boardVolts = def.logicVoltage;
            states[h].boardMaxA = def.pinAbsMaxA;
        }
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



int PinBus::numberOf(int h) const {
    for (auto it = byNumber.begin(); it != byNumber.end(); ++it) if (it.value() == h) return it.key();
    return -1;
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



void PinBus::releaseBoardPins() {
    for (int h : byNumber) release(h);
}



double PinBus::voltage(int h) const {
    return valid(h) ? states[h].voltage : 0.0;
}



void PinBus::writeSource(int h, double v, double r, double conn) {
    if (!valid(h)) return;
    PinState& s = states[h];
    s.src = conn > 0.0;
    s.srcV = v;
    s.srcR = std::max(0.0, r);
    s.srcConn = std::clamp(conn, 0.0, 1.0);
}



void PinBus::openSource(int h) {
    if (!valid(h)) return;
    states[h].src = false;
    states[h].srcConn = 0.0;
}



void PinBus::markGround(int h) {
    if (valid(h)) states[h].ground = true;
}



PinSource PinBus::source(int h) const {
    PinSource out;
    if (!valid(h)) return out;
    const PinState& s = states[h];
    if (s.ground) {
        out.open = false;
        out.conn = 1.0;
        return out;
    }
    if (s.src) {
        out.open = false;
        out.v = s.srcV;
        out.r = s.srcR;
        out.conn = s.srcConn;
        return out;
    }
    if (s.onBoard && s.drive != PinState::Drive::None) {
        // A push-pull pin: HIGH for the duty, LOW the rest; averaged.
        out.open = false;
        out.v = s.drive == PinState::Drive::Voltage ? s.voltage : dutyCycle(h) * s.boardVolts;
        out.r = s.boardOhm;
        out.conn = 1.0;
    }
    return out;
}



void PinBus::setAmps(int h, double a) {
    if (valid(h)) states[h].amps = a;
}



double PinBus::amps(int h) const {
    return valid(h) ? states[h].amps : 0.0;
}



std::vector<int> PinBus::boardNets() const {
    std::vector<int> out;
    for (int h = 0; h < int(states.size()); ++h) if (states[h].onBoard) out.push_back(h);
    return out;
}
