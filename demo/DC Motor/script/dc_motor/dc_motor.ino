// DC gear motor through an H-bridge (DRV8833-style: two PWM inputs per motor).
// In RoboticsStudio the driver is built in; name the motor part "Left".
//   A pulsing, B LOW   forward        both HIGH   brake
//   B pulsing, A LOW   reverse        both LOW    coast
// The motor has no encoder: the sketch can't read its speed.
#include "robot_pins.h"

void drive(int speed) {            // -255..255
  if (speed >= 0) { analogWrite(LEFT_MOTOR_A, speed); digitalWrite(LEFT_MOTOR_B, LOW); }
  else            { digitalWrite(LEFT_MOTOR_A, LOW);  analogWrite(LEFT_MOTOR_B, -speed); }
}

void brake() { digitalWrite(LEFT_MOTOR_A, HIGH); digitalWrite(LEFT_MOTOR_B, HIGH); }
void coast() { digitalWrite(LEFT_MOTOR_A, LOW);  digitalWrite(LEFT_MOTOR_B, LOW); }

void setup() {
  Serial.begin(115200);
  pinMode(LEFT_MOTOR_A, OUTPUT);
  pinMode(LEFT_MOTOR_B, OUTPUT);
}

void loop() {
  Serial.println("ramp up (a cheap gear motor won't start below a few percent)");
  for (int s = 0; s <= 255; s += 5) { drive(s); delay(40); }
  delay(1000);
  Serial.println("brake");
  brake();
  delay(1000);
  Serial.println("reverse at half duty, then coast");
  drive(-128);
  delay(1500);
  coast();
  delay(2000);
}
