// Pan a camera back and forth and mirror what it sees on the TFT display.
let pan = comp_1;      // MG996R servo (pan)
let cam = comp_2;      // OV7670 camera, 160x120 rgb
let screen = comp_3;   // RGB TFT, 160x128

let angle = 0;
let step = 4;

function loop() {
    angle += step;
    if (angle >= 80 || angle <= -80) step = -step;
    pan.write(angle);

    if (cam.available()) {
        let gray = cam.toGray();
        let center = cam.regionMean(70, 50, 20, 20);

        screen.clear();
        screen.drawGray(gray, cam.width(), cam.height(), 0, 0);
        screen.drawRect(70, 50, 20, 20, 0xff0000);
        screen.drawText(2, 120, "pan " + angle + "  gray " + Math.round(center.gray), 0x00ff00, 8);
        screen.display();

        // Raw access: frame.data is an ArrayBuffer, 3 bytes per pixel.
        let frame = cam.capture();
        let px = new Uint8Array(frame.data);
        if (frame.seq % 30 === 0) {
            console.log("frame " + frame.seq + " at t=" + frame.time.toFixed(2) +
                        " first pixel " + px[0] + "," + px[1] + "," + px[2] +
                        " dropped " + cam.droppedFrames());
        }
    }
    delay(50);
}
