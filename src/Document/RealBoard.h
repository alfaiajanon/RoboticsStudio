#pragma once

#include <QList>
#include <QString>
#include "Document/IdealBoard.h"

class ComponentInstance;
class Netlist;
struct BoardDef;


/*
 * One real board of the Real level (a component with a "board" block), seen
 * through the project's wiring: which part pin is on which of its pins.
 * Same BoardPin entries and constant names as the ideal board (COMP_6_SIGNAL,
 * SHOULDER_SIGNAL), but the numbers are the board's real pin numbers, so the
 * sketch is unchanged and its robot_pins.h now matches the real board.
 *
 * Lists:
 *   - every part signal/bus pin on a net with one of the board's gpio pins;
 *   - the board's own internal pins (ESP32-CAM camera), named after the board.
 * Built on demand, like IdealBoard; no state of its own.
 */
class RealBoard {
    ComponentInstance* boardComp = nullptr;
    QList<BoardPin> boardPins;

    public:
        static RealBoard build(ComponentInstance* board, ComponentInstance* root, const Netlist& netlist);

        bool isValid() const { return boardComp != nullptr; }
        ComponentInstance* instance() const { return boardComp; }
        const BoardDef& def() const;
        const QList<BoardPin>& pins() const { return boardPins; }
        QList<BoardPin> pinsOf(int uid) const;
        const BoardPin* find(int number) const;     // first part pin on that board pin

        QString header(const QString& projectName) const;
};
