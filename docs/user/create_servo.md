# Creating a servo

A hobby servo (SG90, MG996R, …): the firmware sends an angle, and the motor holds it with limited torque, some deadband and some jitter. Read [component_basics.md](component_basics.md) first.

Quick start: **Project → Add Component → Servo template**, then adjust. References: `models/servo/servo_sg90`, `models/servo/servo_mg996r`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the case. Mesh or box geom, most of the mass (SG90 ≈ 8 g, MG996R ≈ 52 g) |
| body `horn` | the output arm, a few grams. Positioned at the output shaft |
| joint `servo_joint` | `hinge`, `body_a: base`, `body_b: horn`. Its frame sits on the shaft with **Z along the shaft**. `range: [-90, 90]` (or the real travel) |
| connectors | `horn` on the horn (Z pointing out of the arm), `base` on the bottom face, plus screw tabs. Give tabs `snap_angles` `[0, 180]` |

Set `collision: false` on the joint if the horn overlaps the case.

## How the servo works

The emulator models the servo's own controller, in the order the signal takes through the real part:

1. The controller sends a **pulse** on the servo's `signal` pin. From firmware that is the Arduino `Servo` library (`attach(pin, 1000, 2000)`, `write(0..180)`, see [firmware.md](firmware.md#4-what-you-can-use)).
2. The servo acts on **one pulse per 20 ms frame**. Its frame isn't synchronised with your firmware, so a command takes effect 0–20 ms later. Until the first pulse the servo is **limp**.
3. Each unit reads the pulse a little differently: its **centre and scale vary** from unit to unit (seeded, so the same project gives the same units).
4. The **pot** on the output shaft measures the angle, with some noise. It sits after the gearbox, so **backlash is inside the loop**: under load the servo can hunt back and forth.
5. Inside the **deadband** around the target the motor isn't driven at all: the horn can sit anywhere in the band, and sags under load until it leaves the band.
6. Outside it, the drive grows with the error and saturates beyond `proportional_band_deg`. The **DC motor** turns that drive into torque along its datasheet torque–speed line, so the top speed and the stall torque both come from the datasheet.

## Devices

The motor is a torque actuator; the emulator computes the torque.

```json
"actuators": [{ "id": "servo_motor", "type": "motor",
                "target": {"kind": "joint", "id": "servo_joint"},
                "ctrlrange": [-0.264, 0.264], "forcerange": [-0.264, 0.264] }],
"sensors":   [{ "id": "servo_pos", "type": "jointpos",
                "target": {"kind": "joint", "id": "servo_joint"} },
              { "id": "servo_vel", "type": "jointvel",
                "target": {"kind": "joint", "id": "servo_joint"} }]
```

Set `ctrlrange`/`forcerange` a bit above the stall torque (×1.5 leaves room for a supply above the rated voltage).

## Pins

`{"id": "vcc", "type": "power"}`, `{"id": "signal", "type": "pwm"}`, `{"id": "gnd", "type": "ground"}`. The `signal` pin is what the firmware drives: it gets a name like `COMP_6_SIGNAL` on the ideal board (see [component_basics.md](component_basics.md#pins)).

## Interface (names are fixed: the emulator looks them up)

| Signal | Direction | Target | Unit | Notes |
|---|---|---|---|---|
| `target_angle` | input | joint `servo_joint` | `degrees` | `domain` ranged −90..90 gives the edit-mode slider. While running it shows the target the servo latched (plots); it doesn't drive the motor |
| `motor_torque` | input | actuator `servo_motor` | `N·m` | written by the emulator; `physical: false`, `internal: true` |
| `current_angle` | output | sensor `servo_pos` | `degrees` | gear-side angle; `physical: false`, `internal: true` |
| `current_angle_real` | output | sensor on the backlash tendon | `degrees` | output shaft angle, what the pot measures (optional: without it the pot reads `current_angle`) |
| `gear_velocity` | output | sensor `servo_vel` | `deg/s` | for the motor's back-EMF; `physical: false`, `internal: true` |
| `pot_reading`, `drive` | output | `emulator`, scalar, dim 1 | `degrees` / none | what the servo's controller sees and how hard it drives (−1..1). Plot them to watch the deadband and hunting. `internal: true` |

A hobby servo has **no feedback wire**, so the firmware can't read any of these; plots can. Signals marked `internal: true` are the servo's own workings: plots and the Inspector show them under **Internal (simulation-only)**. `current_angle` is internal only on a part with a backlash gap; without one it is the horn angle itself.

## Emulator

`"type": "servo_emulator"`. Give every parameter a source in `parameter_sources` (datasheet, measurement, or `estimate: ...`); `validate_rsdef.py` warns about missing ones.

| Parameter | Unit | SG90 | MG996R | Meaning |
|---|---|---|---|---|
| `pulse_min_us`, `pulse_max_us`, `travel_deg` | µs, µs, ° | 1000, 2000, 180 | 1000, 2000, 180 | nominal pulse ↔ angle mapping (`write()` uses it too) |
| `frame_period_ms` | ms | 20 | 20 | one pulse per frame |
| `deadband_us` | µs | 10 | 5 | full dead band width, as in the datasheet (10 µs ≈ 1.8°) |
| `proportional_band_deg` | ° | 6 | 4 | error that gives full drive |
| `stall_torque_nm` | N·m | 0.176 | 1.08 | at `rated_voltage` |
| `no_load_speed_dps` | °/s | 600 | 428 | at `rated_voltage` (0.1 s/60° = 600 °/s) |
| `rated_voltage` | V | 4.8 | 6.0 | voltage the two figures above are given at |
| `supply_voltage` | V | 5.0 | 5.0 | what the servo runs at (torque and speed scale with it) |
| `pot_noise_deg` | ° | 0.3 | 0.3 | pot reading noise (stddev) |
| `center_spread_us`, `gain_spread` | µs, fraction | 10, 0.03 | 10, 0.03 | unit-to-unit spread (stddev) |

The spreads and the pot noise are estimates until measured on real units.

Firmware: `Servo.write(0..180)` (90 = centre) or `writeMicroseconds(us)`. `read()` returns the last angle written, not the horn's position: a real SG90 or MG996R can't tell you where it is. Keep track of what you commanded, or add a sensor.

## Tuning the feel

The speed and torque come from the datasheet figures; tune the mechanics, not the controller:

| Setting | Raise it → | Lower it → |
|---|---|---|
| joint `damping` | slower (adds to the motor's own back-EMF braking; keep it small: SG90 0.002, MG996R 0.015) | faster |
| joint `frictionloss` | gear friction: stops short of the target, lower top speed | glides exactly |
| joint `armature` | more inertia (rotor through the gears): more overshoot | snappier, can jitter with very light horns |
| `proportional_band_deg` | softer near the target, sags more under load | stiffer, more overshoot and buzz |
| `pot_noise_deg` vs `deadband_us` | noise near the deadband edge makes the servo buzz at rest (realistic for many MG996Rs) | quiet at rest |

Check with a Scalar plot of `target_angle`, `current_angle_real`, `pot_reading` and `drive`. A 60° step should take roughly the datasheet time; the model comes out somewhat slower because of gear friction, acceleration and the frame wait (SG90 ≈ 0.14 s vs 0.10 s, MG996R ≈ 0.19 s vs 0.14 s).

## Workarounds

- **Backlash:** add a gap joint and a tendon, as in [component_basics.md](component_basics.md#common-workarounds). Point `current_angle_real` at the `tendonpos` sensor. Give the gap joint a little damping (SG90 0.0005, MG996R 0.005): with none, the horn crosses the gap freely when the load reverses (a weight passing over the top), slams into the far end and bounces, and the impact also kicks the gears back. Put the gap size and damping in the joint's `sources`; both are estimates until measured.
- **Continuous-rotation servo:** use a `velocity` actuator instead, name the signals like a stepper (`target_velocity`, …) and use `stepper_emulator`, which takes `set_velocity()` / `set_rpm()`.
- **Arm drops on Play:** the servos are limp until the first pulse (and again after the pulses stop, e.g. firmware `detach()`). Send the start angle at the very beginning: in firmware, `attach()` already starts at 1500 µs, so `write()` the start angle right after it in `setup()`.
- **Limit the command, not the joint:** clamp the angle in your firmware; the joint `range` stays at the mechanical limits (the target is clamped to it).
