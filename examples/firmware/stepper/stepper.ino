// Stepper through an A4988-style driver: one STEP pulse per (micro)step, DIR
// sets the direction. Name the stepper part "Stepper". The driver is built in
// (always enabled, full steps unless the part's microstepping says otherwise).
// Steps too fast for the load are lost for good: the sketch can't see that
// (no encoder), it only counts what it sent.
#include "robot_pins.h"

const int STEPS_PER_REV = 200;     // 1.8 degree motor, full steps
long position = 0;                 // what the sketch believes

void stepOnce(int dir, unsigned int halfPeriodUs) {
  digitalWrite(STEPPER_DIR, dir > 0 ? HIGH : LOW);
  digitalWrite(STEPPER_STEP, HIGH);
  delayMicroseconds(halfPeriodUs);
  digitalWrite(STEPPER_STEP, LOW);
  delayMicroseconds(halfPeriodUs);
  position += dir;
}

// Accelerate from 2 ms to 0.6 ms per step, cruise, decelerate.
void move(long steps) {
  const int dir = steps > 0 ? 1 : -1;
  long n = steps > 0 ? steps : -steps;
  for (long i = 0; i < n; i++) {
    long fromEdge = i < n - 1 - i ? i : n - 1 - i;
    unsigned int periodUs = fromEdge >= 70 ? 600 : 2000 - fromEdge * 20;
    stepOnce(dir, periodUs / 2);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STEPPER_STEP, OUTPUT);
  pinMode(STEPPER_DIR, OUTPUT);
}

void loop() {
  move(STEPS_PER_REV);             // one turn forward
  delay(500);
  move(-STEPS_PER_REV);            // and back
  Serial.print("believed position: ");
  Serial.println(position);
  delay(1000);
}
