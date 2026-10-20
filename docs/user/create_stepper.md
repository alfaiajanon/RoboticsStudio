# Creating a stepper motor

A stepper (NEMA 17 and friends) behind a STEP/DIR driver: each pulse on STEP moves the shaft one step, as long as the motor has the torque to follow. Steps it can't follow are lost for good. Read [component_basics.md](component_basics.md) first.

Reference: `models/stepper/nema17` (start from a copy of it).

## Structure

| Piece | Setup |
|---|---|
| body `base` | the motor can. Heavy (NEMA 17 ≈ 280 g) |
| body `shaft` | the shaft, a small cylinder of about 10 g |
| joint `stepper_joint` | `hinge`, base → shaft, Z along the shaft. **No `range`**: it spins freely. `armature` ≥ the minimum below; `frictionloss` stands in for the detent torque |
| connectors | `shaft_tip` (Z out of the shaft, for wheels and pulleys), `face_mount` (the screw face, `snap_angles` `[0, 90, 180, 270]`), `back_mount` |

## Pins

`{"id": "step", "type": "digital"}`, `{"id": "dir", "type": "digital"}`, plus `vmot` (`power`) and `gnd` (`ground`). At the Prototype level an ideal A4988-style driver is built in and always enabled.

## Devices

```json
"actuators": [{ "id": "stepper_motor", "type": "motor",
                "target": {"kind": "joint", "id": "stepper_joint"},
                "ctrlrange": [-0.6, 0.6], "forcerange": [-0.6, 0.6] }],
"sensors":   [{ "id": "stepper_pos", "type": "jointpos", "target": {"kind": "joint", "id": "stepper_joint"} },
              { "id": "stepper_vel", "type": "jointvel", "target": {"kind": "joint", "id": "stepper_joint"} }]
```

## Interface (fixed names)

| Signal | Direction | Target | Unit | Notes |
|---|---|---|---|---|
| `motor_torque` | input | actuator `stepper_motor` | `N·m` | written by the emulator; `internal: true` |
| `current_position` | output | sensor `stepper_pos` | `radians` | `physical: false` (no encoder) |
| `current_velocity` | output | sensor `stepper_vel` | `rad/s` | `physical: false` |
| `missed_steps` | output | `emulator`, scalar | full steps | lost so far, positive = behind the command; `internal: true` |

## How it works

1. **STEP/DIR:** every rising edge on STEP moves the commanded angle by `step_angle_deg / microstepping` in DIR's direction (HIGH = positive). Pulses faster than the physics step are all counted.
2. **Torque-angle curve:** the rotor is pulled toward the commanded position along the real curve, a sinusoid that peaks (the holding torque) one full step away and repeats every 4 full steps.
3. **Missed steps:** pushed more than 2 full steps behind (too much load, too fast an acceleration), the rotor falls into the next equilibrium: 4 full steps lost, for good. At power-on the rotor snaps to the nearest equilibrium, as a real one does.
4. **Speed:** the available torque falls with speed, `holding / (1 + rpm / pullout_corner_rpm)`, so a stepper started too fast, or run too fast for its load, stalls.

**Numerical limit:** the real curve is stiff (≈ 20 N·m/rad for a NEMA 17). To stay stable at the 2 ms physics step, the driven inertia must be at least stiffness × (2 × timestep)² (≈ 3.2e-4 kg·m²). The part's `armature` provides that (3.6e-4), more than a real rotor's ~5e-6, so an almost unloaded stepper accelerates more slowly than a real one. Holding torque, stiffness and missed steps stay real. With less inertia the app warns once.

## Emulator

`"type": "stepper_emulator"`.

| Parameter | Unit | NEMA 17 | Meaning |
|---|---|---|---|
| `holding_torque_nm` | N·m | 0.40 | datasheet holding torque (17HS4401) |
| `step_angle_deg` | ° | 1.8 | full step |
| `microstepping` | | 1 | the driver's MS pins (1, 2, 4, 8, 16) |
| `pullout_corner_rpm` | rpm | 300 | speed where the pull-out torque has halved (estimate), at `pullout_voltage` |
| `pullout_voltage` | V | 12 | supply the corner was estimated at: at the Real level the corner scales with the `vmot` rail's voltage |
| `rated_current_a`, `phase_resistance_ohm` | A, Ω | 1.7, 1.5 | the chopper keeps this phase current; the driver draws the coils' power (2·I²·R) from `vmot` at the Real level |

**Power** (Real level): `vmot` needs a 9-24 V source (a bench supply, a 2S/3S battery); the built-in driver stops below 8 V (`min_v`, the A4988's minimum). The holding torque doesn't depend on the supply (the driver regulates the current), the top speed does.

**Firmware:** pulse STEP with `digitalWrite` and `delayMicroseconds`, ramping the rate up and down (see `examples/firmware/stepper`). The sketch counts its own steps: there is no position feedback.

## Tuning

| Setting | Effect |
|---|---|
| `holding_torque_nm` | the load it holds, and how hard it accelerates |
| `pullout_corner_rpm` | how quickly torque fades with speed |
| joint `armature` | must stay ≥ the minimum; more means a heavier rotor |

## Workarounds

- **Lost steps:** plot `missed_steps` while you tune the ramp: start slower, accelerate gently, or reduce the load.
- **Homing:** a real build homes against a limit switch at startup; until switch parts exist, start from the edit-mode pose.
