// The Arduino core functions, on the simulator's imports (rs_abi.h).
#include "Arduino.h"

extern "C" {

unsigned long millis(void) { return rs_millis(); }
unsigned long micros(void) { return rs_micros(); }

void delay(unsigned long ms) {
    // rs_delay_us takes 32-bit microseconds: long delays go in chunks.
    while (ms > 4000000UL) { rs_delay_us(4000000000UL); ms -= 4000000UL; }
    rs_delay_us(uint32_t(ms * 1000UL));
}

void delayMicroseconds(unsigned int us) { rs_delay_us(us); }
void yield(void) { rs_yield(); }

}   // extern "C"

void pinMode(int pin, uint8_t mode) { rs_pin_mode(pin, mode); }
void digitalWrite(int pin, uint8_t level) { rs_digital_write(pin, level); }
int digitalRead(int pin) { return rs_digital_read(pin); }
int analogRead(int pin) { return rs_analog_read(pin); }
void analogWrite(int pin, int value) { rs_analog_write(pin, constrain(value, 0, 255)); }

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// random(): xorshift32 inside the firmware, so a run repeats exactly.
// randomSeed(analogRead(...)) works as on a real board, but on the ideal
// board the reading is the same every run.
static uint32_t rngState = 2463534242u;

void randomSeed(unsigned long seed) {
    if (seed != 0) rngState = uint32_t(seed);
}

static uint32_t nextRandom() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
}

long random(long howbig) {
    if (howbig <= 0) return 0;
    return long(nextRandom() % uint32_t(howbig));
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    return random(howbig - howsmall) + howsmall;
}
