// Stage script: a rover orbits the robot at 0.4 m so the camera sees it pass.
// It only knows where the robot is (robot.getPosition), not what it does.
let rover = prop_rover;

const RADIUS = 0.4;
const SPEED = 0.25;   // m/s along the orbit
const PULL = 1.5;     // 1/s, radial correction

function loop() {
    let r = robot.getPosition();
    let p = rover.getPosition();
    let dx = p.x - r.x, dy = p.y - r.y;
    let dist = Math.max(Math.hypot(dx, dy), 1e-3);
    let ux = dx / dist, uy = dy / dist;

    // tangent (counter-clockwise) + pull back onto the circle
    let k = PULL * (RADIUS - dist);
    rover.setVelocity(-uy * SPEED + ux * k, ux * SPEED + uy * k);

    // face the direction of travel
    let heading = Math.atan2(ux, -uy) * 180 / Math.PI;
    let err = ((heading - rover.getYaw()) % 360 + 540) % 360 - 180;
    rover.setYawRate(3 * err);

    delay(20);
}
