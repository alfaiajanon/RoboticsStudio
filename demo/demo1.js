let base_servo = comp_2;  // Rotation (Yaw)
let servo_1 = comp_4;     // Shoulder (Pitch)
let servo_2 = comp_7;     // Elbow (Pitch)


function setArmHeight(target_z) {
    let angle = Math.asin(target_z / 0.1) * (180 / Math.PI);
    servo_1.write_angle(angle);
    servo_2.write_angle(-angle * 2); 
}


function loop() {
    console.log("Moving to Z...");
    base_servo.write_angle(0);
    setArmHeight(0.01); 
    delay(1000);

    console.log("Lifting to Z + 0.05...");
    setArmHeight(0.07); 
    delay(1000);

    console.log("Rotating Base...");
    base_servo.write_angle(90);
    delay(1000);

    console.log("Lowering back to Z...");
    setArmHeight(0.01);
    delay(1000);
    
    // now do it backwards
    console.log("Go up again...");
    setArmHeight(0.07); 
    delay(1000);

    console.log("Rotating back...");
    base_servo.write_angle(0);
    delay(1000);

    console.log("Lowering back to Z...");
    setArmHeight(0.01);
    delay(500);
}