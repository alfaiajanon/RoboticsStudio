/*
 * ESP32Servo.h: the ESP32Servo library (madhephaestus, 3.x), as it behaves
 * on a real ESP32, including what differs from the Uno's Servo library:
 *
 *   - attach() sends nothing: the servo stays limp until the first write().
 *   - Pulses come from the LEDC timer at 50 Hz with a 10-bit timer by
 *     default, so a pulse width is a whole number of 20000/1024 = 19.5 us
 *     ticks (rounded down): about 3.5 degrees on a 1000..2000 us servo.
 *     setTimerWidth(16) before attach() gives 0.3 us ticks.
 *   - write(value): below 500 the value is an angle 0..180; the attach()
 *     range is clamped to 500..2500 us (default 544..2400).
 *   - read() is the last written pulse converted back to an angle.
 *
 * Which pins can drive a servo is the board's business (ESP32: most GPIOs).
 */
#ifndef ESP32_Servo_h
#define ESP32_Servo_h

#include <stdint.h>
#include "Arduino.h"

#define DEFAULT_uS_LOW        544
#define DEFAULT_uS_HIGH       2400
#define MIN_PULSE_WIDTH       500
#define MAX_PULSE_WIDTH       2500
#define DEFAULT_PULSE_WIDTH   1500
#define DEFAULT_PULSE_WIDTH_TICKS 4825
#define REFRESH_USEC          20000
#define MAX_SERVOS            16
#define MINIMUM_TIMER_WIDTH   10
#define MAXIMUM_TIMER_WIDTH   20
#define DEFAULT_TIMER_WIDTH   10
#define DEFAULT_TIMER_WIDTH_TICKS 1024

// The class is Servo, as in the real library, but kept in its own namespace:
// the core's Servo (Servo.h, linked into every sketch) has the same name, and
// two classes of one name would clash at link time.
namespace rs_esp32servo {

// Timer bookkeeping of the real library; nothing to allocate here.
class ESP32PWM {
    public:
        static void allocateTimer(int timerNumber) { (void)timerNumber; }
};

class Servo {
    int pinNumber = -1;
    int min = DEFAULT_uS_LOW;
    int max = DEFAULT_uS_HIGH;
    int timer_width = DEFAULT_TIMER_WIDTH;
    int ticks = DEFAULT_PULSE_WIDTH_TICKS;
    int timer_width_ticks = DEFAULT_TIMER_WIDTH_TICKS;
    int REFRESH_CPS = 50;

    int usToTicks(int usec) const {
        return (int)((double)usec / ((double)REFRESH_USEC / (double)timer_width_ticks) * (((double)REFRESH_CPS) / 50.0));
    }
    int ticksToUs(int t) const {
        return (int)((double)t * ((double)REFRESH_USEC / (double)timer_width_ticks) / (((double)REFRESH_CPS) / 50.0));
    }
    // The pulse the LEDC timer sends for `t` ticks.
    void output(int t) {
        const double periodNs = 1e9 / REFRESH_CPS;
        rs_pwm_write_ns(pinNumber, (int32_t)(t * periodNs / timer_width_ticks));
    }

    public:
        Servo() {}

        int attach(int pin) { return attach(pin, DEFAULT_uS_LOW, DEFAULT_uS_HIGH); }
        int attach(int pin, int minUs, int maxUs) {
            if (pinNumber < 0) {
                ticks = DEFAULT_PULSE_WIDTH_TICKS;
                timer_width = DEFAULT_TIMER_WIDTH;
                timer_width_ticks = 1 << timer_width;
            }
            pinNumber = pin;
            min = minUs < MIN_PULSE_WIDTH ? MIN_PULSE_WIDTH : minUs;
            max = maxUs > MAX_PULSE_WIDTH ? MAX_PULSE_WIDTH : maxUs;
            rs_pwm_write_ns(pinNumber, 0);   // the channel starts at duty 0: no pulses yet
            return 1;
        }
        void detach() {
            if (attached()) rs_pwm_write_ns(pinNumber, 0);
            pinNumber = -1;
        }
        void write(int value) {
            if (value < MIN_PULSE_WIDTH) {
                if (value < 0) value = 0;
                else if (value > 180) value = 180;
                value = map(value, 0, 180, min, max);
            }
            writeMicroseconds(value);
        }
        void writeMicroseconds(int value) { writeTicks(usToTicks(value)); }
        void writeTicks(int value) {
            if (!attached()) return;
            if (value < usToTicks(min)) value = usToTicks(min);
            else if (value > usToTicks(max)) value = usToTicks(max);
            ticks = value;
            output(ticks);
        }
        void release() { if (attached()) rs_pwm_write_ns(pinNumber, 0); }
        int read() { return map(readMicroseconds(), min, max, 0, 180); }
        int readMicroseconds() { return attached() ? ticksToUs(ticks) : 0; }
        int readTicks() { return ticks; }
        bool attached() { return pinNumber >= 0; }

        void setTimerWidth(int value) {
            if (value < MINIMUM_TIMER_WIDTH) value = MINIMUM_TIMER_WIDTH;
            else if (value > MAXIMUM_TIMER_WIDTH) value = MAXIMUM_TIMER_WIDTH;
            // The real library's tick rescaling, kept as it is.
            const int widthDifference = timer_width - value;
            if (widthDifference > 0) ticks = widthDifference * ticks;
            else if (widthDifference < 0) ticks = ticks / -widthDifference;
            timer_width = value;
            timer_width_ticks = 1 << timer_width;
            // It re-attaches the LEDC channel, which restarts at duty 0.
            if (attached()) rs_pwm_write_ns(pinNumber, 0);
        }
        int readTimerWidth() { return timer_width; }
        void setPeriodHertz(int hertz) {
            REFRESH_CPS = hertz;
            setTimerWidth(timer_width);
        }
};

}   // namespace rs_esp32servo

using rs_esp32servo::ESP32PWM;
using rs_esp32servo::Servo;

#endif   // ESP32_Servo_h
