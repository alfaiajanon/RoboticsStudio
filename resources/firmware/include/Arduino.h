/*
 * Arduino.h for RoboticsStudio: the Arduino core API on the simulator's
 * ideal board (Prototype level). Written in the style of the newer
 * ArduinoCore-API: helpers are templates, not macros, so standard headers
 * (<algorithm>, <cmath>) keep working.
 *
 * Pins: at the Prototype level the ideal board's numbers from the generated
 * robot_pins.h (e.g. COMP_6_SIGNAL); at the Real level the board's own pin
 * numbers and names (9, A0, LED_BUILTIN) from its variant, pins_arduino.h,
 * as on the real core. Everything that takes time runs on simulated time.
 */
#ifndef ARDUINO_H
#define ARDUINO_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rs_abi.h"
// The board's variant (Real level, written next to the sketch's build):
// A0, LED_BUILTIN, SDA, ... as the real core's pins_arduino.h has them.
// The ideal board has none.
#if __has_include("pins_arduino.h")
#include "pins_arduino.h"
#endif

#define ROBOTICS_STUDIO 1

#define HIGH 0x1
#define LOW  0x0

#define INPUT        0x0
#define OUTPUT       0x1
#define INPUT_PULLUP 0x2

#define PI         3.1415926535897932384626433832795
#define HALF_PI    1.5707963267948966192313216916398
#define TWO_PI     6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define lowByte(w)  ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit)  (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)   ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define bit(b) (1UL << (b))

// No flash/RAM split here: program-memory helpers read plain memory.
#define PROGMEM
#define pgm_read_byte(addr)  (*(const uint8_t*)(addr))
#define pgm_read_word(addr)  (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

#ifdef __cplusplus
extern "C" {
#endif

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield(void);

// One controller, no interrupts to mask: kept so sketches compile unchanged.
static inline void interrupts(void) {}
static inline void noInterrupts(void) {}

void setup(void);
void loop(void);

#ifdef __cplusplus
}   // extern "C"

// Pins are int, not uint8_t as on AVR: ideal-board numbers go above 255.
// A uint8_t argument still converts without a warning.
void pinMode(int pin, uint8_t mode);
void digitalWrite(int pin, uint8_t level);
int digitalRead(int pin);
int analogRead(int pin);
void analogWrite(int pin, int value);

template <class T, class L>
inline auto min(const T& a, const L& b) -> decltype((b < a) ? b : a) { return (b < a) ? b : a; }
template <class T, class L>
inline auto max(const T& a, const L& b) -> decltype((b < a) ? b : a) { return (a < b) ? b : a; }
template <class T, class L, class H>
inline T constrain(const T& x, const L& low, const H& high) { return x < low ? T(low) : (x > high ? T(high) : x); }
template <class T>
inline T sq(const T& x) { return x * x; }
template <class T>
inline double radians(const T& deg) { return deg * DEG_TO_RAD; }
template <class T>
inline double degrees(const T& rad) { return rad * RAD_TO_DEG; }

long map(long x, long in_min, long in_max, long out_min, long out_max);

long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

// F("text") on AVR keeps strings in flash; here it is plain memory.
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(string_literal))

#include "WString.h"
#include "Print.h"
#endif   // __cplusplus

#endif   // ARDUINO_H
