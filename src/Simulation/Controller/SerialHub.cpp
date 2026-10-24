#include "SerialHub.h"

#include <algorithm>

// Output kept per board until the monitor takes it (bytes); older text is dropped.
static constexpr int kMaxPendingOutput = 1 << 20;



void SerialHub::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    byUid.clear();
    order.clear();
    ++gen;
}



void SerialHub::addBoard(int uid, const QString& name, bool ideal) {
    std::lock_guard<std::mutex> lock(mutex);
    Board b;
    b.info = {uid, name, ideal};
    b.monitorBaud = savedBauds.value(uid, 9600.0);
    byUid.insert(uid, b);
    order.append(uid);
    ++gen;
}



QList<SerialHub::BoardInfo> SerialHub::boards() const {
    std::lock_guard<std::mutex> lock(mutex);
    QList<BoardInfo> out;
    for (int uid : order) out.append(byUid[uid].info);
    return out;
}



void SerialHub::setMonitorBaud(int uid, double baud) {
    std::lock_guard<std::mutex> lock(mutex);
    savedBauds.insert(uid, baud);
    auto it = byUid.find(uid);
    if (it != byUid.end()) it->monitorBaud = baud;
}



double SerialHub::monitorBaud(int uid) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = byUid.constFind(uid);
    return it != byUid.constEnd() ? it->monitorBaud : savedBauds.value(uid, 9600.0);
}



void SerialHub::send(int uid, const QByteArray& bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = byUid.find(uid);
    if (it != byUid.end()) it->pending.append(bytes);
}



QList<SerialHub::Chunk> SerialHub::takeOutput(int uid) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = byUid.find(uid);
    if (it == byUid.end()) return {};
    QList<Chunk> out;
    out.swap(it->out);
    return out;
}



void SerialHub::stamp(double now) {
    std::lock_guard<std::mutex> lock(mutex);
    for (Board& b : byUid) {
        if (b.pending.isEmpty()) continue;
        const double byteTime = b.info.ideal ? 0.0 : 10.0 / b.monitorBaud;
        for (char c : b.pending) {
            UartByte u;
            u.start = std::max(now, b.lineFree);
            u.arrival = u.start + byteTime;
            u.baud = b.info.ideal ? 0.0 : b.monitorBaud;
            u.sender = -3;   // the monitor
            u.value = uint8_t(c);
            b.lineFree = u.arrival;
            b.line.push_back(u);
        }
        b.pending.clear();
    }
}



bool SerialHub::takeInput(int uid, double now, UartByte* out) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = byUid.find(uid);
    if (it == byUid.end() || it->line.empty() || it->line.front().arrival > now) return false;
    *out = it->line.front();
    it->line.pop_front();
    return true;
}



void SerialHub::output(int uid, double time, double baud, const char* data, int len) {
    if (len <= 0) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = byUid.find(uid);
    if (it == byUid.end()) return;
    QList<Chunk>& out = it->out;
    if (!out.isEmpty() && out.last().baud == baud && out.last().bytes.size() < 4096) out.last().bytes.append(data, len);
    else out.append(Chunk{time, baud, QByteArray(data, len)});
    int total = 0;
    for (const Chunk& c : out) total += int(c.bytes.size());
    while (total > kMaxPendingOutput && out.size() > 1) {
        total -= int(out.first().bytes.size());
        out.removeFirst();
    }
}
