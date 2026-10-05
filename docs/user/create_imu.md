# Creating an IMU (accelerometer + gyro)

A breakout board like the MPU-6050: 3-axis acceleration and 3-axis angular rate. Read [component_basics.md](component_basics.md) first. Reference: `models/imu/imu_mpu6050`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the PCB, one geom (mesh or thin box) of a few grams |
| site `imu` | where the chip is. **Its axes are the sensor axes**: rotate the site to match the X/Y/Z arrows printed on the board |
| connectors | the mounting holes, Z pointing out of the board's back face. `snap_angles` `[0, 180]` |

## Devices

```json
"sensors": [{ "id": "imu_accel", "type": "accelerometer", "target": {"kind": "site", "id": "imu"} },
            { "id": "imu_gyro",  "type": "gyro",          "target": {"kind": "site", "id": "imu"} }]
```

Both give 3 values in the site's frame. The accelerometer includes gravity: about +9.81 m/s² on the axis pointing up when the board is at rest.

## Interface (fixed names)

| Signal | Direction | Target | Unit | componentLabels |
|---|---|---|---|---|
| `acceleration` | output | sensor `imu_accel` | `m/s^2` | `x`, `y`, `z` |
| `gyroscope` | output | sensor `imu_gyro` | `degrees/s` (converted from MuJoCo's rad/s) | `x`, `y`, `z` |

With unit `degrees/s` (or `deg/s`, `dps`) the gyro reads in degrees per second. With any other unit, such as `rad/s`, you get MuJoCo's raw rad/s.

## Emulator

`"type": "imu_emulator"`. Script: `getAcceleration()` and `getRotation()` return `{x, y, z}`.

| Parameter | Status |
|---|---|
| `noise_stddev` | declared, **not applied yet**: values are clean |

## Workarounds

- **Noise and bias now:** add them in the script, for example `a.x + (Math.random() - 0.5) * 0.2`, plus a constant offset per axis, which is what real MPU-6050s have.
- **Mounting orientation:** check in a Vector plot. Tilt the robot in edit mode or let it fall in play mode, and confirm the expected axis reads ±9.81. If not, rotate the site, not the board.
- **Vibration:** a stiff, light mount on a jittery servo makes noisy readings, just like real life. Average several samples in the script.
