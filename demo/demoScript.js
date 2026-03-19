function* loop() {
    comp_1.write_angle(90);
    console.log("goto 90 deg");

    yield delay(1000);
    
    comp_1.write_angle(-90);
    console.log("goto -90 deg");

    yield delay(1000);
}