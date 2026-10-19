// Servo Comparison: two SG90s sweep together, so the emulated one (deadband,
// pot noise, backlash, unit spread) can be compared with the ideal one.
//
// Real version (Real level): an Arduino Uno wired to both servos (signals on
// D9 and D10, 5V and GND) on the Wiring page. Same sketch as the Prototype
// copy; the names in uno/robot_pins.h are generated from that wiring.

#include <Servo.h>
#include "robot_pins.h"

const unsigned long PERIOD_MS = 20000;   // one full sweep: -45 -> +45 -> -45 degrees
const int MIN_ANGLE = 45;                // Arduino angles: 90 is the centre, so 45..135
const int MAX_ANGLE = 135;               //   is -45..+45 degrees around it
const int STEP = 1;                      // degrees per step

Servo emulated;
Servo standard;

void writeBoth(int angle) {
  emulated.write(angle);
  standard.write(angle);
}

void setup() {
  // An SG90 takes 1000..2000 us for its full travel (the library's default
  // range, 544..2400 us, is wider than the part).
  emulated.attach(EMULATED_SERVO_SIGNAL, 1000, 2000);
  standard.attach(STANDARD_SERVO_SIGNAL, 1000, 2000);
  writeBoth(MIN_ANGLE);
}

void loop() {
  const int steps = (MAX_ANGLE - MIN_ANGLE) / STEP;     // per half period
  const unsigned long stepDelay = (PERIOD_MS / 2) / steps;

  for (int i = 1; i <= steps; i++) {      // up
    writeBoth(MIN_ANGLE + i * STEP);
    delay(stepDelay);
  }
  for (int i = 1; i <= steps; i++) {      // and back down
    writeBoth(MAX_ANGLE - i * STEP);
    delay(stepDelay);
  }
}
