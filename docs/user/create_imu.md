# Creating an IMU (accelerometer + gyro)

A breakout board like the MPU-6050: 3-axis acceleration and 3-axis angular rate, read over I2C with its real register map and its real errors (bias, noise, filter, range, resolution). Read [component_basics.md](component_basics.md) first. Reference: `models/imu/imu_mpu6050`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the PCB, one geom (mesh or thin box) of a few grams |
| site `imu` | where the chip is. **Its axes are the sensor axes**: rotate the site to match the X/Y/Z arrows printed on the board |
| connectors | the mounting holes, Z pointing out of the board's back face. `snap_angles` `[0, 180]` |

## Pins

`{"id": "sda", "type": "i2c_sda"}`, `{"id": "scl", "type": "i2c_scl"}`, `vcc` (`power`), `gnd` (`ground`). The chip answers on the board's I2C bus at 0x68 (0x69 with `ad0_high`). `xda`, `xcl`, `ad0` and `int` stay untyped (not modelled yet).

## Devices

```json
"sensors": [{ "id": "imu_accel", "type": "accelerometer", "target": {"kind": "site", "id": "imu"} },
            { "id": "imu_gyro",  "type": "gyro",          "target": {"kind": "site", "id": "imu"} }]
```

Both give 3 values in the site's frame. The accelerometer includes gravity: about +9.81 m/s² on the axis pointing up when the board is at rest.

## Interface (fixed names)

| Signal | Direction | Target | Unit | Notes |
|---|---|---|---|---|
| `acceleration` | output | sensor `imu_accel` | `m/s^2` | the true value; `physical: false` |
| `gyroscope` | output | sensor `imu_gyro` | `degrees/s` | the true value; `physical: false` |
| `accel_reading` | output | `emulator`, vector 3 | `g` | what the chip reports |
| `gyro_reading` | output | `emulator`, vector 3 | `°/s` | what the chip reports |

Plot the true value next to the reading to see the bias, noise and filter lag.

## How it works (per axis, in the chip's order)

1. **Scale and bias:** each unit has its own sensitivity error and zero offset (datasheet tolerances), plus a slow bias drift.
2. **Noise:** white noise from the datasheet's noise density, at the ADC.
3. **Digital low-pass filter** (register `CONFIG`): one pole at the configured bandwidth (260 … 5 Hz), so the noise at the output is the density × √bandwidth, and fast motion lags.
4. **Output data rate:** the data registers update at 8 kHz (filter off) or 1 kHz, divided by `1 + SMPLRT_DIV`, at most once per physics step (500 Hz).
5. **Range and resolution:** clipped at ±2…16 g / ±250…2000 °/s (`ACCEL_CONFIG`, `GYRO_CONFIG`) and stored as 16-bit integers (16384 LSB/g, 131 LSB/(°/s) at the smallest ranges).

Registers: `SMPLRT_DIV` 0x19, `CONFIG` 0x1A, `GYRO_CONFIG` 0x1B, `ACCEL_CONFIG` 0x1C, data 0x3B–0x48 (big-endian), `PWR_MGMT_1` 0x6B, `WHO_AM_I` 0x75 (= 0x68). **The chip powers up asleep** (`PWR_MGMT_1` = 0x40): until it is woken, the data reads 0, as on the real part. The temperature reads a constant 25 °C.

## Emulator

`"type": "imu_emulator"`. Values from the MPU-6000/6050 Product Specification rev 3.4; the spreads are estimates within the datasheet's tolerances.

| Parameter | Unit | MPU-6050 | Meaning |
|---|---|---|---|
| `accel_noise_density_ug` | µg/√Hz | 400 | accelerometer noise density |
| `gyro_noise_density_dps` | °/s/√Hz | 0.005 | gyroscope noise density |
| `accel_bias_spread_mg` | mg | 25 | per-unit zero-g offset (stddev) |
| `gyro_bias_spread_dps` | °/s | 5 | per-unit zero-rate offset (stddev) |
| `accel_bias_walk_mg`, `gyro_bias_walk_dps` | per √s | 0.05, 0.01 | bias drift (estimates) |
| `scale_spread` | fraction | 0.01 | per-unit sensitivity error (stddev) |
| `ad0_high` | 0/1 | 0 | 1: address 0x69 |

**Firmware:** `Wire` with the registers, as in most tutorials: wake the chip, set the filter and ranges, read 14 bytes from 0x3B, and calibrate the gyro at rest (see `examples/firmware/imu_mpu6050`).

## Workarounds

- **Mounting orientation:** check in a Vector plot. Tilt the robot in edit mode or let it fall in play mode, and confirm the expected axis reads ±1 g. If not, rotate the site, not the board.
- **Gyro offset:** every unit reads a few °/s at rest. Average it at startup and subtract it, as on real hardware.
- **Vibration:** a stiff, light mount on a jittery servo makes noisy readings, just like real life. Lower the filter bandwidth or average in firmware.
