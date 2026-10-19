# Creating a geared DC motor

A yellow TT motor, an N20 or BO gearmotor, …: two driver inputs set the motor's voltage, and the output turns at the speed its torque-speed line and the load allow. Read [component_basics.md](component_basics.md) first. Reference: `models/motor/dc_gear_60rpm`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the gearbox case (TT motor ≈ 30 g) |
| body `axle` | the output shaft, about 5 g |
| joint `dc_joint` | `hinge`, base → axle, Z along the output shaft, no `range`, `collision: false`. `armature` = rotor inertia × gear ratio² (BO motor ≈ 0.0015 kg·m²); `frictionloss` = gearbox static friction; `damping` = bearing loss only (back-EMF is in the motor model) |
| connectors | `shaft_left` / `shaft_right` (wheel mounts, Z out of the shaft), `chassis_mount_*` (screw holes) |

## Pins

`{"id": "motor_a", "type": "pwm"}`, `{"id": "motor_b", "type": "pwm"}`: the motor's two terminals. At the Prototype level an ideal H-bridge (DRV8833-style) sits in between, driven from these two board pins (driver inputs IN1, IN2); real driver parts come with the Real level.

## Devices

```json
"actuators": [{ "id": "dc_motor", "type": "motor",
                "target": {"kind": "joint", "id": "dc_joint"},
                "ctrlrange": [-0.25, 0.25], "forcerange": [-0.25, 0.25] }],
"sensors":   [{ "id": "dc_vel", "type": "jointvel", "target": {"kind": "joint", "id": "dc_joint"} }]
```

A `motor` (torque) actuator: the emulator computes the torque. Set the ranges a bit above the stall torque.

## Interface (fixed names)

| Signal | Direction | Target | Unit | Notes |
|---|---|---|---|---|
| `motor_torque` | input | actuator `dc_motor` | `N·m` | written by the emulator; `internal: true` |
| `current_velocity` | output | sensor `dc_vel` | `rad/s` | output shaft speed; `physical: false` (no encoder) |
| `drive` | output | `emulator`, scalar | | net duty −1..1 the driver applies; `internal: true` |

## How it works

1. **Driver:** A pulsing with B LOW drives forward at A's duty; B pulsing with A LOW drives in reverse; both HIGH brakes (the terminals are shorted, so back-EMF stops the motor fast); both LOW coasts. The duty is what the pin averages: `analogWrite()` sends a 490 Hz pulse train, and a steady HIGH counts as 1.
2. **Voltage:** duty × `supply_voltage`.
3. **Motor:** the datasheet's torque-speed line at that voltage: stall torque at standstill, zero torque at the no-load speed. Speed is limited by back-EMF, not by a speed setting. Each unit's no-load speed differs a little (`speed_spread`).
4. **Gearbox friction** (`frictionloss`) keeps the motor still at low duty, as on a real cheap gearmotor.

The averaged-voltage model ignores the PWM ripple and the driver's fast/slow decay difference.

## Emulator

`"type": "dc_gear_emulator"`. Give every parameter a source in `parameter_sources`.

| Parameter | Unit | BO 60 rpm | Meaning |
|---|---|---|---|
| `stall_torque_nm` | N·m | 0.15 | at `rated_voltage` |
| `no_load_speed_rpm` | rpm | 64 | the line's no-load speed at `rated_voltage`. The datasheet's measured no-load speed includes gearbox friction, so the line sits a little higher (64 here gives the listed 60 rpm) |
| `rated_voltage` | V | 12 | voltage of the two figures above |
| `supply_voltage` | V | 12 | what the driver gets, until the power net exists |
| `speed_spread` | fraction | 0.05 | unit-to-unit spread of the no-load speed (stddev) |

**Firmware:** `analogWrite(LEFT_MOTOR_A, 180); digitalWrite(LEFT_MOTOR_B, LOW);` (see [firmware.md](firmware.md) and `examples/firmware/dc_motor`). There is no speed read-back: a cheap gearmotor has no encoder.

## Tuning

| Setting | Effect |
|---|---|
| `stall_torque_nm` | how hard it pushes; a heavy robot on small motors crawls or stalls |
| joint `armature` | spin-up and coast-down time |
| joint `frictionloss` | the duty below which the motor doesn't start (friction ÷ stall torque) |

## Workarounds

- **Wheels:** attach a wheel part to `shaft_left` / `shaft_right`. The wheel needs real mass and friction to drive the robot; see [create_structural.md](create_structural.md).
- **Speed feedback:** a real robot adds an encoder; until encoder parts exist, plot `current_velocity` to see what the motor does.
