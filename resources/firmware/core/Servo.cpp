#include "Arduino.h"
#include "Servo.h"

// Channels are handed out in attach order, like the real library's servo
// index. The ideal board has no timer limit, so there is no MAX_SERVOS.
static uint8_t nextChannel = 0;

Servo::Servo() {}

uint8_t Servo::attach(int p) {
    return attach(p, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
}

uint8_t Servo::attach(int p, int min, int max) {
    if (channel == INVALID_SERVO) channel = nextChannel < INVALID_SERVO - 1 ? nextChannel++ : INVALID_SERVO;
    pin = p;
    minUs = min;
    maxUs = max;
    rs_pwm_write_us(pin, widthUs);   // pulses start now (1500 us until the first write)
    return channel;
}

void Servo::detach() {
    if (pin >= 0) rs_pwm_write_us(pin, 0);   // no more pulses: the servo goes limp
    pin = -1;
}

void Servo::write(int value) {
    if (value < MIN_PULSE_WIDTH) {
        value = constrain(value, 0, 180);
        value = map(value, 0, 180, minUs, maxUs);
    }
    writeMicroseconds(value);
}

void Servo::writeMicroseconds(int value) {
    widthUs = constrain(value, minUs, maxUs);
    if (pin >= 0) rs_pwm_write_us(pin, widthUs);
}

int Servo::read() {
    return map(readMicroseconds() + 1, minUs, maxUs, 0, 180);
}

int Servo::readMicroseconds() {
    return pin >= 0 ? widthUs : 0;
}

bool Servo::attached() {
    return pin >= 0;
}
