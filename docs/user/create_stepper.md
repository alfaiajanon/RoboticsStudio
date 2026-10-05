# Creating a stepper motor

A stepper (NEMA 17 and friends) driven by speed: the script sets a velocity, and the motor tracks it up to its holding torque. Read [component_basics.md](component_basics.md) first.

Quick start: **Project → Add Component → Stepper template**. Reference: `models/stepper/nema17`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the motor can. Heavy (NEMA 17 ≈ 280 g) |
| body `shaft` | the shaft, a small cylinder of about 10 g |
| joint `stepper_joint` | `hinge`, base → shaft, Z along the shaft. **No `range`**: it spins freely |
| connectors | `shaft_tip` (Z out of the shaft, for wheels and pulleys), `face_mount` (the screw face, `snap_angles` `[0, 90, 180, 270]`), `back_mount` |

## Devices

```json
"actuators": [{ "id": "stepper_motor", "type": "velocity",
                "target": {"kind": "joint", "id": "stepper_joint"},
                "ctrlrange": [-20, 20], "forcerange": [-0.45, 0.45], "kv": 1.0 }],
"sensors":   [{ "id": "stepper_pos", "type": "jointpos", "target": {"kind": "joint", "id": "stepper_joint"} },
              { "id": "stepper_vel", "type": "jointvel", "target": {"kind": "joint", "id": "stepper_joint"} }]
```

## Interface (fixed names)

| Signal | Direction | Target | Unit |
|---|---|---|---|
| `target_velocity` | input | joint `stepper_joint` | `rad/s` |
| `current_position` | output | sensor `stepper_pos` | `radians` (not `degrees`, so no conversion) |
| `current_velocity` | output | sensor `stepper_vel` | `rad/s` |

## Emulator

`"type": "stepper_emulator"`. Script: `set_velocity(rad_s)`, `set_rpm(rpm)`, `stop()`, `read_position()`, `read_velocity()`.

| Parameter | Status |
|---|---|
| `step_angle_deg`, `microstepping` | read but **not used yet**: motion is smooth, with no discrete steps |
| `holding_torque_factor` | declared, not used |

## Tuning

| Setting | Effect |
|---|---|
| `forcerange` | the holding torque (NEMA 17: 0.3–0.5 N·m). Load above it and the motor "loses steps" (falls behind the commanded speed) |
| `kv` | how hard it corrects speed errors. Higher tracks tighter; too high jitters on light shafts |
| `ctrlrange` | top speed in rad/s (20 rad/s ≈ 190 rpm) |
| joint `damping`, `frictionloss` | drag and detent-like resistance |
| joint `armature` | rotor inertia; slows acceleration realistically |

## Workarounds

- **Position moves ("go to 90°"):** the stepper is velocity-driven. In the script, run `set_velocity()` toward the target and `stop()` when `read_position()` gets there, or write a small P-controller.
- **No step quantisation yet:** for a stepped feel, round the commanded position to `step_angle / microstepping` in your script.
