let stage_servo = comp_4;
let up_servo = comp_7;
let mpu = comp_10;

let stage_angle = 90.0;
let sweep_direction = -1;
let up_angle = 0.0;
let Kp = 0.5;

// Sweeps the stage servo back and forth between -20 and 90 degrees.
// Reads the 3D acceleration vector from the IMU to calculate tilt error
// and actively stabilizes the upright servo using a proportional control loop.




function loop() {
    stage_angle += sweep_direction * 0.5;

    if (stage_angle <= -20) {
        sweep_direction = 1;
    } else if (stage_angle >= 90) {
        sweep_direction = -1;
    }

    stage_servo.write(stage_angle);

    let accel = mpu.getAcceleration();
    let tilt_error = accel.x;

    up_angle = up_angle - (Kp * tilt_error);
    up_angle = Math.max(-90, Math.min(90, up_angle));

    up_servo.write(up_angle);

    delay(50);
}
