/*
 * Servo.h: the Arduino Servo library, with the real library's behaviour
 * (including its surprises), so a sketch acts the same on the real board:
 *
 *   - attach() starts sending pulses at once, at 1500 us: the servo jumps to
 *     its centre when attached, before the first write().
 *   - write(value): below 544 the value is an angle 0..180 (90 = centre),
 *     mapped onto the attach() range; from 544 up it is microseconds.
 *   - The default range is 544..2400 us, wider than most hobby servos are
 *     specified for (an SG90 uses about 1000..2000 us): pass the part's range
 *     to attach(pin, min, max).
 *   - read() returns the last angle written, not the servo's position. A
 *     hobby servo has no feedback wire.
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#define MIN_PULSE_WIDTH     544    // shortest pulse sent to a servo, us
#define MAX_PULSE_WIDTH     2400   // longest pulse sent to a servo, us
#define DEFAULT_PULSE_WIDTH 1500   // pulse sent when attached, us
#define REFRESH_INTERVAL    20000  // pulse period, us
#define INVALID_SERVO       255

class Servo {
    int pin = -1;
    int minUs = MIN_PULSE_WIDTH;
    int maxUs = MAX_PULSE_WIDTH;
    int widthUs = DEFAULT_PULSE_WIDTH;
    uint8_t channel = INVALID_SERVO;

    public:
        Servo();
        uint8_t attach(int pin);
        uint8_t attach(int pin, int min, int max);
        void detach();
        void write(int value);
        void writeMicroseconds(int value);
        int read();
        int readMicroseconds();
        bool attached();
};

#endif   // SERVO_H
