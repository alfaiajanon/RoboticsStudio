let servo = comp_1; 
let imu = comp_21;

function loop() {
    servo.write_angle(90);
    console.log("goto 90 deg");
    delay(1000);
    
    servo.write_angle(-90);
    console.log("goto -90 deg");
    delay(2000);
}

