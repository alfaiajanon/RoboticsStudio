// Component Initialization
let stage_servo = comp_4;
let up_servo = comp_7;
let mpu = comp_10;

// State Variables
let stage_angle = 90.0;
let sweep_direction = -1; // -1 for down, 1 for up
let up_angle = 0.0;       // Starting angle for the stabilizer

// Proportional Gain (Tuning parameter)
// If the wrist overcorrects and shakes, lower this number.
// If it reacts too slowly, increase it.
let Kp = 0.5; 

function loop() {
    // =========================================
    // 1. Sweep the Stage Servo (90 to -20)
    // =========================================
    stage_angle += sweep_direction * 0.5; // Move 0.5 degrees per tick
    
    // Reverse direction if we hit the limits
    if (stage_angle <= -20) {
        sweep_direction = 1;
    } else if (stage_angle >= 90) {
        sweep_direction = -1;
    }
    
    stage_servo.write_angle(stage_angle);


    // =========================================
    // 2. Read IMU & Stabilize the Up Servo
    // =========================================
    // When perfectly level, gravity pushes straight down.
    // If the bracket tilts, gravity starts pulling on the X (or Y) axis.
    // We read that horizontal acceleration as our "Error".
    let tilt_error = mpu.read_accel_x(); 
    
    // Adjust the servo to fight the tilt. 
    // (Note: If the servo tilts the WRONG way, change the '-' to a '+')
    up_angle = up_angle - (Kp * tilt_error);
    
    // Constrain the angle so we don't break the servo limits (-90 to 90)
    up_angle = Math.max(-90, Math.min(90, up_angle));
    
    up_servo.write_angle(up_angle);


    // =========================================
    // 3. Loop Delay (Control Frequency)
    // =========================================
    // 50ms delay gives us a ~20Hz control loop, 
    // which is standard for basic servo stabilization.
    delay(50);
}