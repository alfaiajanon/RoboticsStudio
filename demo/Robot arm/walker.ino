// Robot firmware: an Arduino sketch. It runs on the simulator's ideal board
// and compiles unchanged for a real board (with that board's robot_pins.h).
//
// robot_pins.h names each part's signal pin (also in the Inspector,
// "Pins (ideal board)"). In this project:
//   TOWERPRO_MG996R_1_SIGNAL = COMP_1_SIGNAL  TowerPro MG996R (1) (servo_mg996r), pwm
//   TOWERPRO_MG996R_6_SIGNAL = COMP_6_SIGNAL  TowerPro MG996R (6) (servo_mg996r), pwm
//   TOWERPRO_MG996R_11_SIGNAL = COMP_11_SIGNAL  TowerPro MG996R (11) (servo_mg996r), pwm

#include <Servo.h>
#include "robot_pins.h"

Servo towerpro_mg996r_1;

void setup() {
  Serial.begin(115200);
  towerpro_mg996r_1.attach(TOWERPRO_MG996R_1_SIGNAL, 1000, 2000);   // the part's pulse range, in microseconds
}

void loop() {
  towerpro_mg996r_1.write(90);   // 0..180 degrees, 90 = centre
  delay(20);
}
