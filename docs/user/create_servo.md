# Creating a servo

A hobby servo (SG90, MG996R, …): the script sends an angle, and the motor holds it with limited torque, some deadband and some jitter. Read [component_basics.md](component_basics.md) first.

Quick start: **Project → Add Component → Servo template**, then adjust. References: `models/servo/servo_sg90`, `models/servo/servo_mg996r`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the case. Mesh or box geom, most of the mass (SG90 ≈ 8 g, MG996R ≈ 52 g) |
| body `horn` | the output arm, a few grams. Positioned at the output shaft |
| joint `servo_joint` | `hinge`, `body_a: base`, `body_b: horn`. Its frame sits on the shaft with **Z along the shaft**. `range: [-90, 90]` (or the real travel) |
| connectors | `horn` on the horn (Z pointing out of the arm), `base` on the bottom face, plus screw tabs. Give tabs `snap_angles` `[0, 180]` |

Set `collision: false` on the joint if the horn overlaps the case.

## Devices

```json
"actuators": [{ "id": "servo_motor", "type": "position",
                "target": {"kind": "joint", "id": "servo_joint"},
                "ctrlrange": [-90, 90], "forcerange": [-0.176, 0.176],
                "kp": 25.0, "kv": 2.5 }],
"sensors":   [{ "id": "servo_pos", "type": "jointpos",
                "target": {"kind": "joint", "id": "servo_joint"} }]
```

## Interface (names are fixed: the emulator looks them up)

| Signal | Direction | Target | Unit | Notes |
|---|---|---|---|---|
| `target_angle` | input | joint `servo_joint` | `degrees` | `domain` ranged −90..90 gives the edit-mode slider |
| `current_angle` | output | sensor `servo_pos` | `degrees` | `physical: false`: a hobby servo has no feedback wire, so reading it is "cheating" |

## Emulator

`"type": "servo_emulator"`

| Parameter | Unit | Effect |
|---|---|---|
| `deadband_threshold` | degrees | new commands closer than this to the held target are ignored, like the servo's dead band. 1–3° is typical for cheap servos |
| `noise_stddev` | degrees | Gaussian jitter added to the target on every physics step, so the horn buzzes around the setpoint. 0.3–1° is typical |

Script: `comp_N.write(angle)`, `comp_N.read()`.

## Tuning the feel

| Setting | Raise it → | Lower it → |
|---|---|---|
| `forcerange` (N·m) | holds heavier loads. Use the real stall torque: SG90 0.18, MG996R 1.08 | sags and stalls under load |
| `kp` | snappier and stiffer; too high oscillates or explodes on light horns | soft and slow, sags |
| `kv` | less overshoot, slower settling | overshoot and wobble |
| joint `damping` | slower and smoother | faster, more ringing |
| joint `frictionloss` | stops short of the target under small errors (like gear friction) | glides exactly |
| joint `armature` | more stable with a light horn, slightly sluggish | jitter with very light bodies |

A good start: `kp` ≈ 25, `kv` ≈ `kp / 10`, `forcerange` = the stall torque. Check in a Scalar plot of `target_angle` vs `current_angle`.

**Top speed:** MuJoCo's position actuator has no speed limit. The speed comes from `kp`, `kv` and `forcerange`. To match the datasheet (for example 0.1 s / 60°), plot a 60° step and lower `kp` or `forcerange` until it takes that long.

## Workarounds

- **Backlash:** add a gap joint and a tendon, as in [component_basics.md](component_basics.md#common-workarounds). Point `current_angle` at the `tendonpos` sensor.
- **Continuous-rotation servo:** use a `velocity` actuator instead, name the signals like a stepper (`target_velocity`, …) and use `stepper_emulator`, which takes `set_velocity()` / `set_rpm()`.
- **Servo jumps on Play:** the edit-mode pose and the first `write()` differ. Call `write()` with the start angle at the top of `loop()`, or lower `kp`.
- **Limit the command, not the joint:** `ctrlrange` is in degrees, like `target_angle`. To keep a 180° servo within ±60° in software, use `ctrlrange: [-60, 60]` and keep the joint `range` at the mechanical limits.
