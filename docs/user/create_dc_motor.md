# Creating a geared DC motor

A yellow TT motor, an N20 gearmotor, …: the script sets a PWM duty and the output turns at a speed limited by gearbox torque. Read [component_basics.md](component_basics.md) first. Reference: `models/motor/dc_gear_60rpm`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the gearbox case (TT motor ≈ 30 g) |
| body `axle` | the output shaft, about 5 g |
| joint `dc_joint` | `hinge`, base → axle, Z along the output shaft, no `range`, `collision: false` |
| connectors | `shaft_left` / `shaft_right` (wheel mounts, Z out of the shaft), `chassis_mount_*` (screw holes) |

## Devices

```json
"actuators": [{ "id": "dc_motor", "type": "velocity",
                "target": {"kind": "joint", "id": "dc_joint"},
                "ctrlrange": [-6.28, 6.28], "forcerange": [-0.35, 0.35], "kv": 10.0 }],
"sensors":   [{ "id": "dc_vel", "type": "jointvel", "target": {"kind": "joint", "id": "dc_joint"} }]
```

`ctrlrange` is the no-load output speed in rad/s (60 rpm ≈ 6.28). `forcerange` is the stall torque at the output.

## Interface (fixed names)

| Signal | Direction | Target | Unit |
|---|---|---|---|
| `target_velocity` | input | joint `dc_joint` | `rad/s` |
| `current_velocity` | output | sensor `dc_vel` | `rad/s`, `physical: false` (most cheap motors have no encoder) |

## Emulator

`"type": "dc_gear_emulator"`. Script: `set_pwm(-255..255)`, `brake()`, `read_velocity()`.

**Current limitation:** the PWM value is passed through **as the target velocity in rad/s**, and then clipped by `ctrlrange`. So with a ±6.28 range, any `|pwm|` above about 6 means full speed, and small values give proportionally slow speeds.

| Parameter | Status |
|---|---|
| `max_rpm_at_12v`, `deadband_pwm`, `acceleration_delay_ms` | declared; the mapping and ramp code is **disabled** in this version |

Until the PWM mapping is enabled, scale in the script: `motor.set_pwm(pwm / 255 * 6.28)` gives a 0..255 feel.

## Tuning

| Setting | Effect |
|---|---|
| `forcerange` | stall torque; a heavy robot on small motors crawls or stalls |
| `kv` | how stiffly the speed is held under load; low values make the speed sag a lot under load (more like a real unregulated DC motor) |
| joint `armature` | rotor inertia through the gearbox; gives a realistic spin-up and coast-down |
| joint `frictionloss` | gearbox friction; with low commands the motor doesn't start (natural deadband) |

## Workarounds

- **Realistic deadband now:** set `frictionloss` so small commands can't overcome it. This gives the classic "doesn't move below PWM X" behaviour without the emulator.
- **Wheels:** attach a wheel part to `shaft_left` / `shaft_right`. The wheel needs real mass and friction to drive the robot; see [create_structural.md](create_structural.md).
