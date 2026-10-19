#pragma once

#include <QList>
#include <QString>

class ComponentInstance;
struct PinDef;


// One signal pin of one part, as the robot's firmware sees it.
struct BoardPin {
    int number = -1;     // what the sketch passes to attach(), digitalWrite(), ...
    int uid = -1;        // the part
    QString pinId;       // the part's pin ("signal")
    QString type;        // pwm / digital / analog / i2c_sda / ...
    QString constant;    // COMP_6_SIGNAL
    QString alias;       // SHOULDER_SIGNAL, empty when the part's name is not unique
    QString partName;
    QString modelId;
    QString boardPinId;  // Real level: the board's pin it is on ("d9", or the board's own internal pin)
};


// Shared by IdealBoard and RealBoard, so robot_pins.h has the same names at
// both levels and a sketch survives the move from Prototype to Real.
namespace BoardPins {
    // "Emulated Servo 2" -> "EMULATED_SERVO_2"; empty when it can't be a C++ name.
    QString identifier(const QString& name);
    // Every instance under root, depth first.
    QList<ComponentInstance*> collect(ComponentInstance* root);
    // Parts whose name gives a unique alias prefix (exactly one part with
    // signal pins has that name).
    QList<int> aliasedUids(const QList<ComponentInstance*>& parts);
    // COMP_<uid>_<PIN> and the alias; number and type left to the caller.
    BoardPin make(ComponentInstance* comp, const PinDef& pin, bool aliased);
    // The robot_pins.h text: `intro` (comment lines), then the pins grouped by part.
    QString header(const QList<BoardPin>& pins, const QString& intro);
}


/*
 * The ideal controller of the Prototype level: unlimited pins, every part
 * wired to it automatically. Each typed pin except power/ground (signal
 * pins and bus lines: pwm, digital, analog, i2c_*, spi_*)
 * of each part gets a pin number derived from the part's uid and the pin's
 * index in its .rsdef, so adding or removing other parts never renumbers it.
 * The sketch reaches the pins through generated constants (robot_pins.h).
 *
 * Built on demand from the instance tree; it has no state of its own. When
 * the netlist arrives (Phase 2) nets replace the automatic wiring and the
 * constants stay the same.
 */
class IdealBoard {
    QList<BoardPin> boardPins;

    public:
        static constexpr int kPinsPerPart = 100;   // number = uid * 100 + pin index

        static IdealBoard fromTree(ComponentInstance* root);

        const QList<BoardPin>& pins() const { return boardPins; }
        QList<BoardPin> pinsOf(int uid) const;
        const BoardPin* find(int number) const;    // null if no part has it

        // The robot_pins.h the sketch includes.
        QString header(const QString& projectName) const;
};
