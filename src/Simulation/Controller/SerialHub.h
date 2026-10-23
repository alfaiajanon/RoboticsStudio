#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>
#include "Simulation/Controller/PinBus.h"


/*
 * The boards' USB serial ports, between the firmware and the Serial Monitor:
 * what each board prints (with the sim time and the baud rate it was sent
 * at) and the lines typed into the monitor. No widgets; the monitor polls it.
 *
 * Input: the UI queues bytes (send()); at the start of each physics step the
 * physics thread puts them on the board's USB line (stamp()): one after the
 * other at the monitor's baud rate (Real level) or at once (the ideal board).
 * The firmware takes them like bytes on a wire (takeInput()). The sim time a
 * line arrives at depends on when it was typed, so a run with typed input
 * does not repeat; everything else stays deterministic.
 *
 * Threads: send()/takeOutput()/setMonitorBaud() on the UI thread;
 * stamp() on the physics thread; takeInput()/output() on the controller
 * threads; reset()/addBoard() at Play, between steps. One mutex.
 */
class SerialHub {
    public:
        struct Chunk {
            double time = 0.0;      // sim time of the first byte
            double baud = 0.0;      // the board's rate (0: the ideal board, matches any monitor)
            QByteArray bytes;
        };
        struct BoardInfo {
            int uid = -1;
            QString name;
            bool ideal = true;
        };

        // At Play: the boards that run this time (the ideal board: uid -1).
        void reset();
        void addBoard(int uid, const QString& name, bool ideal);
        QList<BoardInfo> boards() const;
        // Changes when the board list does (the monitor rebuilds its tabs).
        int generation() const { return gen.load(); }

        // ---- UI thread ----
        void setMonitorBaud(int uid, double baud);
        double monitorBaud(int uid) const;
        void send(int uid, const QByteArray& bytes);
        QList<Chunk> takeOutput(int uid);

        // ---- physics thread, at the start of a step ----
        void stamp(double now);

        // ---- controller threads ----
        // The next typed byte that has arrived by `now`.
        bool takeInput(int uid, double now, UartByte* out);
        void output(int uid, double time, double baud, const char* data, int len);

    private:
        struct Board {
            BoardInfo info;
            double monitorBaud = 9600.0;
            QByteArray pending;             // typed, not on the line yet
            std::deque<UartByte> line;      // on the line, by arrival
            double lineFree = 0.0;          // sim time the monitor's TX is free
            QList<Chunk> out;
        };
        mutable std::mutex mutex;
        QMap<int, Board> byUid;
        QList<int> order;
        QMap<int, double> savedBauds;       // kept across runs (the monitor's setting)
        std::atomic<int> gen{0};
};
