// DC gear motor through an L298N module, on a real Arduino Uno.
// Wiring (Wiring page): Uno pin 5 -> IN1, pin 6 -> IN2, OUT1/OUT2 -> the motor,
// 2S LiPo -> L298N VS and the Uno's VIN, grounds together. ENA jumper fitted.
//   IN1 pulsing, IN2 LOW   forward        both HIGH   brake (to VS)
//   IN2 pulsing, IN1 LOW   reverse        both LOW    brake (to ground)
// With its enable jumper fitted the L298N has no "coast": to let the motor
// spin down freely, pull the jumper and drive ENA instead (LOW = outputs off).
// The bridge drops about 2 V, so on a 7.4 V pack this 12 V motor runs at
// roughly half its rated speed.
const int MOTOR_IN1 = 5;   // PWM pins on the Uno
const int MOTOR_IN2 = 6;

void drive(int speed) {            // -255..255
  if (speed >= 0) { analogWrite(MOTOR_IN1, speed); digitalWrite(MOTOR_IN2, LOW); }
  else            { digitalWrite(MOTOR_IN1, LOW);  analogWrite(MOTOR_IN2, -speed); }
}

void brake() { digitalWrite(MOTOR_IN1, LOW); digitalWrite(MOTOR_IN2, LOW); }

void setup() {
  Serial.begin(115200);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
}

void loop() {
  Serial.println("ramp up (a cheap gear motor won't start below a few percent)");
  for (int s = 0; s <= 255; s += 5) { drive(s); delay(40); }
  delay(1000);
  Serial.println("brake");
  brake();
  delay(1000);
  Serial.println("reverse at half duty, then brake");
  drive(-128);
  delay(1500);
  brake();
  delay(2000);
}
