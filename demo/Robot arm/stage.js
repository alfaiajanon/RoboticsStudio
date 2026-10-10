// Stage script: drives the controlled props, next to (not inside) the robot.
// Globals:
//   prop_<id>  one per prop whose motion is "controlled"
//     setVelocity(vx, vy)        world frame, m/s
//     move(forward, strafe)      prop frame, m/s
//     setYawRate(degPerSec)
//     stop()
//     teleport(x, y, z, yawDeg)
//     getPosition() -> {x, y, z}, getVelocity() -> {x, y, z}, getYaw() -> deg
//   robot      read-only robot pose
//     getPosition() -> {x, y, z}, getRotation() -> {roll, pitch, yaw} (deg)

function loop() {
	prop_sp1.move(0,0.2);
	delay(3000);
	prop_sp1.move(0,-0.2);
	delay(3000);
}