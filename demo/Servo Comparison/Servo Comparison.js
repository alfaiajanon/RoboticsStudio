// RoboticsStudio Controller Script
let time = 20000;   // Duration of ONE full period in milliseconds (-90 -> 90 -> -90)

let servo1 = comp_2;
let servo2 = comp_3;

const MIN_ANGLE = -45;
const MAX_ANGLE = 45;
const STEP = 1;    // Degrees moved per step (smaller = smoother)

function setup() {
  // Called once when simulation starts
  servo1.write(MIN_ANGLE);
  servo2.write(MIN_ANGLE);
}

function loop() {
  // Called every physics step
  const steps = (MAX_ANGLE - MIN_ANGLE) / STEP;   // steps per half-period
  const stepDelay = (time / 2) / steps;           // ms to wait between steps

  // Sweep up: -90 -> 90 (first half of the period)
  for (let i = 1; i <= steps; i++) {
    const angle = MIN_ANGLE + i * STEP;
    servo1.write(angle);
    servo2.write(angle);
    delay(stepDelay);
  }

  // Sweep down: 90 -> -90 (second half of the period)
  for (let i = 1; i <= steps; i++) {
    const angle = MAX_ANGLE - i * STEP;
    servo1.write(angle);
    servo2.write(angle);
    delay(stepDelay);
  }
}