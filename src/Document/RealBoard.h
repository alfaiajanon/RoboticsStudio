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
 * The sketch uses the board's own pin numbers and names (9, A0, LED_BUILTIN)
 * as on the real board; nothing is generated for it except the board's
 * variant (pins_arduino.h), which the real core also has. The BoardPin
 * entries (number = the board pin, boardPinId = its label) are for the
 * Inspector, the starter sketch and the run-time messages.
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

        // The board's pins_arduino.h (board.variant): what the real core's
        // variant defines, so A0 or LED_BUILTIN mean the same here.
        QString variantHeader() const;
        // The board's macros for the compiler (ARDUINO_AVR_UNO, ...).
        QStringList defines() const;

};
