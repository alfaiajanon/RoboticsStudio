#pragma once

#include <QHash>
#include <QString>
#include <vector>

class IdealBoard;
class RealBoard;
class Netlist;


// What one pin carries right now. Ideal pins: no timer quantization, no
// noise (errors belong to the part at each end; board timers come later).
struct PinState {
    // What the controller last put on the pin.
    enum class Drive { None, Pulse, Level, Voltage };

    QString type;            // pwm / digital / analog, as the part declares it
    Drive drive = Drive::None;
    bool driven = false;     // false until something writes it (a servo is limp until its first pulse)
    double pulseUs = 0.0;    // pwm: pulse width
    double periodUs = 0.0;   // pwm: pulse period, when the writer knows it (analogWrite); 0 = unknown
    int level = 0;           // digital: LOW / HIGH
    int risingEdges = 0;     // digital: LOW->HIGH writes not yet taken by the part (STEP pulses)
    double voltage = 0.0;    // analog

    // The electrical side, for loads that draw real current (a motor terminal).
    bool ideal = false;        // an ideal-board pin (Prototype): the ideal driver, not a circuit
    double boardOhm = 0.0;     // Real: a board gpio pin is on this net: its output resistance ...
    double boardVolts = 0.0;   // ... its logic voltage (HIGH) ...
    double boardMaxA = 0.0;    // ... and its absolute maximum current (0 = not modelled)
    bool onBoard = false;      // a board gpio pin is on this net
    bool ground = false;       // a ground net: a 0 V source for a load wired to it
    // A motor driver's output on this net (motor_out), written every step by the driver.
    bool src = false;
    double srcV = 0.0, srcR = 0.0;   // Thevenin while connected
    double srcConn = 0.0;            // fraction of the time connected (EN PWM); 1 = always
    double amps = 0.0;               // current a load (motor) draws out of this net's source, last step
};

// What drives a net electrically: open (nothing, or a pin set as input), or a
// Thevenin source connected for a fraction of the time.
struct PinSource {
    bool open = true;
    double v = 0.0, r = 0.0;
    double conn = 0.0;   // 1 = always connected; < 1 = open the rest of the time (no current then)
};


/*
 * The pins between the robot's controller and its parts, for one run.
 * Built at Play from the ideal board; a part's emulator reads the pins it
 * receives on, the controller (JS through the emulator API, firmware through
 * its pin numbers) writes them. So JS and firmware drive a part the same way.
 *
 * Pins are addressed by handles (stable for the run): parts look theirs up by
 * (uid, pin id), firmware by its board number. Handle -1 means "no pin":
 * every read and write ignores it.
 *
 * Threading: the controller thread writes while the physics thread is
 * blocked in LockstepGate::runStep, and the physics thread reads in
 * Emulator::update(); the gate's mutex orders the two. build()/clear() run
 * between steps, while the controllers are idle.
 */
class PinBus {
    std::vector<PinState> states;
    QHash<QString, int> byPart;   // "uid:pinId" -> handle
    QHash<int, int> byNumber;     // board number -> handle

    static QString partKey(int uid, const QString& pinId) { return QString::number(uid) + ":" + pinId; }
    bool valid(int h) const { return h >= 0 && h < int(states.size()); }

    public:
        // Prototype: one undriven pin per ideal-board pin; drops everything else.
        void build(const IdealBoard& board);
        // Real: one pin per net (every pin on the net shares it), plus one per
        // board pin with a number that has no wire. A part pin without a wire
        // gets its own pin from ensurePin(): nothing drives it, it floats.
        void build(const RealBoard& board, const Netlist& netlist);
        void clear();

        int handleOf(int uid, const QString& pinId) const;   // -1 if none
        int handleForNumber(int boardNumber) const;          // -1 if no part has it
        int numberOf(int h) const;                           // the board pin number of a handle, -1 if none

        // The board pin, or an off-board pin for a part pin that isn't typed
        // (so not on the board): scripts that reach the part directly keep
        // working, firmware can't reach it.
        int ensurePin(int uid, const QString& pinId, const QString& type);

        // analogWrite(): 0 and 255 are steady levels; between, a pulse train
        // whose width is the duty. The ideal board uses Uno-style 490 Hz; a
        // real board passes its pin's PWM period.
        static constexpr double kAnalogWritePeriodUs = 1e6 / 490.0;
        void writeAnalog(int h, int value, double periodUs = kAnalogWritePeriodUs);
        // Fraction of the time the pin is HIGH, for a part that averages it
        // (a motor driver input): pulses as width / period (the pin's own
        // period when known, else periodUs), a steady HIGH 1.
        double dutyCycle(int h, double periodUs = kAnalogWritePeriodUs) const;

        void writePulseUs(int h, double us, double periodUs = 0.0);
        bool pulse(int h, double* us) const;   // false unless pulses are being sent
        void release(int h);                   // stop driving (pulses stop, input floats)
        void releaseBoardPins();               // every pin the firmware reaches (an MCU in reset: Hi-Z)

        void writeLevel(int h, int level);
        int level(int h) const;
        // Physics thread: rising edges since the last call (many can happen
        // within one physics step, e.g. stepper STEP pulses).
        int takeRisingEdges(int h);

        void writeVoltage(int h, double volts);
        double voltage(int h) const;

        const PinState* state(int h) const { return valid(h) ? &states[h] : nullptr; }

        // ---- the electrical side (Real level) ----
        // A motor driver's output: its Thevenin source this step (conn < 1:
        // open the rest of the time), or open.
        void writeSource(int h, double v, double r, double conn);
        void openSource(int h);
        void markGround(int h);   // a net with ground pins (Real)
        // What drives the net: a driver's output, else a board pin the firmware
        // drives (its duty x logic voltage, through its output resistance), else open.
        PinSource source(int h) const;
        // The load current out of the net's source (a motor writes it, A).
        void setAmps(int h, double a);
        double amps(int h) const;
        // Nets with a board gpio pin on them (for the pins' current).
        std::vector<int> boardNets() const;
};
