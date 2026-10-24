#include "PinBus.h"

#include <algorithm>
#include <functional>
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
        s.boardUid = -1;
        const int h = int(states.size());
        states.push_back(s);
        byPart.insert(partKey(p.uid, p.pinId), h);
        byNumber.insert(numberKey(-1, p.number), h);
    }
}



void PinBus::build(const QList<RealBoard>& boards, const Netlist& netlist, const QList<Join>& joins) {
    clear();
    const QList<Net> nets = netlist.nets();
    const QHash<PinRef, int> netOf = Netlist::indexOf(nets);

    // Nets joined through a level shifter's channels are one signal.
    std::vector<int> parent(nets.size());
    for (int i = 0; i < int(parent.size()); ++i) parent[i] = i;
    std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
    QHash<int, double> rootBitrate;   // net root -> lowest rated bit rate of the shifters on it
    for (const Join& j : joins) {
        const int a = netOf.value(j.a, -1), b = netOf.value(j.b, -1);
        if (a < 0 || b < 0) continue;
        const int ra = find(a), rb = find(b);
        parent[ra] = rb;
        double rate = j.maxBitrate;
        for (int r : {ra, rb}) {
            const double old = rootBitrate.value(r, 0.0);
            if (old > 0.0 && (rate <= 0.0 || old < rate)) rate = old;
        }
        rootBitrate.insert(rb, rate);
    }
    QHash<int, int> handleOfRoot;
    for (int i = 0; i < int(nets.size()); ++i) {
        const int root = find(i);
        int h = handleOfRoot.value(root, -1);
        if (h < 0) {
            PinState s;
            s.type = "net";
            h = int(states.size());
            states.push_back(s);
            handleOfRoot.insert(root, h);
        }
        for (const PinRef& r : nets[i].pins) byPart.insert(partKey(r.uid, r.pin), h);
    }
    for (auto it = handleOfRoot.begin(); it != handleOfRoot.end(); ++it) {
        auto rate = rootBitrate.constFind(find(it.key()));
        if (rate != rootBitrate.constEnd()) states[it.value()].maxBitrate = rate.value();
    }

    for (const RealBoard& board : boards) {
        ComponentInstance* comp = board.instance();
        if (!comp || !comp->getBlueprint()) continue;
        const BoardDef& def = comp->getBlueprint()->board;
        for (const PinDef& p : comp->getBlueprint()->pins) {
            if (p.number < 0) continue;
            const int h = ensurePin(comp->uid, p.id, p.type);
            byNumber.insert(numberKey(comp->uid, p.number), h);
            if (p.type == "gpio" && !states[h].onBoard) {
                states[h].onBoard = true;
                states[h].boardUid = comp->uid;
                states[h].boardOhm = def.pinOutputOhm;
                states[h].boardVolts = def.logicVoltage;
                states[h].boardMaxA = def.pinAbsMaxA;
            }
        }
    }
}



void PinBus::clear() {
    states.clear();
    byPart.clear();
    byNumber.clear();
    nextReader = 0;
}



int PinBus::handleOf(int uid, const QString& pinId) const {
    return byPart.value(partKey(uid, pinId), -1);
}



int PinBus::handleForNumber(int boardUid, int boardNumber) const {
    return byNumber.value(numberKey(boardUid, boardNumber), -1);
}



int PinBus::numberOf(int h, int boardUid) const {
    for (auto it = byNumber.begin(); it != byNumber.end(); ++it)
        if (it.value() == h && int(it.key() >> 32) - 1 == boardUid) return int(quint32(it.key()));
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



void PinBus::releaseBoardPins(int boardUid) {
    for (auto it = byNumber.begin(); it != byNumber.end(); ++it) {
        if (int(it.key() >> 32) - 1 != boardUid) continue;
        release(it.value());
        if (states[it.value()].writerUid == boardUid) states[it.value()].writerUid = -2;
    }
}



bool PinBus::claim(int h, int boardUid) {
    if (!valid(h)) return true;
    int& w = states[h].writerUid;
    if (w == -2) w = boardUid;
    return w == boardUid;
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



std::vector<int> PinBus::boardNets(int boardUid) const {
    std::vector<int> out;
    for (int h = 0; h < int(states.size()); ++h) if (states[h].onBoard && states[h].boardUid == boardUid) out.push_back(h);
    return out;
}



void PinBus::uartSend(int h, const UartByte& b) {
    if (!valid(h)) return;
    PinState& s = states[h];
    if (!s.uartSenders.contains(b.sender)) s.uartSenders.append(b.sender);
    // Kept even while nobody listens: a board later in the step order may
    // start listening at an earlier sim time than this byte. Unread bytes
    // older than kUartUnheard are gone (no receiver was there for them).
    if (s.uartReaders.isEmpty()) {
        size_t n = 0;
        while (n < s.uart.size() && s.uart[n].arrival < b.start - kUartUnheard) ++n;
        s.uart.erase(s.uart.begin(), s.uart.begin() + std::ptrdiff_t(n));
        s.uartBase += n;
    }
    s.uart.push_back(b);
}



int PinBus::uartOpen(int h, double now) {
    if (!valid(h)) return -1;
    PinState& s = states[h];
    size_t first = s.uartBase + s.uart.size();
    for (size_t i = 0; i < s.uart.size(); ++i) {
        if (s.uart[i].start >= now) { first = s.uartBase + i; break; }
    }
    const int id = nextReader++;
    s.uartReaders.insert(id, first);
    return id;
}



void PinBus::uartClose(int h, int reader) {
    if (!valid(h)) return;
    states[h].uartReaders.remove(reader);
    pruneUart(states[h]);
}



bool PinBus::uartTake(int h, int reader, double now, UartByte* out) {
    if (!valid(h)) return false;
    PinState& s = states[h];
    auto it = s.uartReaders.find(reader);
    if (it == s.uartReaders.end()) return false;
    const size_t i = it.value();
    if (i < s.uartBase || i - s.uartBase >= s.uart.size()) return false;
    const UartByte& b = s.uart[i - s.uartBase];
    if (b.arrival > now) return false;
    *out = b;
    it.value() = i + 1;
    if (i == s.uartBase) pruneUart(s);
    return true;
}



// Drops the bytes every reader has taken (none while nobody listens: see uartSend).
void PinBus::pruneUart(PinState& s) {
    if (s.uartReaders.isEmpty()) return;
    size_t lowest = s.uartBase + s.uart.size();
    for (size_t c : s.uartReaders) lowest = std::min(lowest, c);
    const size_t n = lowest - s.uartBase;
    if (n == 0) return;
    s.uart.erase(s.uart.begin(), s.uart.begin() + std::ptrdiff_t(n));
    s.uartBase = lowest;
}



const QList<int>& PinBus::uartSenders(int h) const {
    static const QList<int> none;
    return valid(h) ? states[h].uartSenders : none;
}
