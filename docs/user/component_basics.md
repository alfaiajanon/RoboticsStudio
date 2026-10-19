# Component basics

Every part is one `.rsdef` file (JSON), built in the **Component Editor** or by hand. These rules apply to every part type; the `create_*.md` guides build on them. The full format reference is [../dev/schema2.md](../dev/schema2.md).

## Units and frames

- Lengths in **meters**, masses in **kg**, joint ranges in **degrees**. Quaternions are `[w, x, y, z]`; the editor shows rotations as roll/pitch/yaw in degrees.
- **Bodies, joints and connectors** are placed in the **component's own frame** (its origin and axes).
- **Geoms and sites** are placed in their **body's frame**.
- The first body is `default_body`: the part's origin when it is the robot's base.

## Meta

`meta` holds the part's `name`, `version`, `author` and `icon_path` (the image in the Components panel). The optional `hint_class` says what kind of part it is, and the robot tree shows a matching icon:

| `hint_class` | For |
|---|---|
| `actuator` | servos, DC motors, steppers |
| `sensor` | IMUs, encoders and other sensors |
| `vision` | cameras |
| `display` | screens |
| `mcu` | microcontrollers and boards |
| `weight` | dummy masses |
| `battery` | batteries |
| `part` | structural parts: brackets, plates, wheels |

Leave it out for a generic icon. Any other value is a load error. In the Component Editor it is the **Class** field.

## The building blocks

| Block | What it is | Notes |
|---|---|---|
| body | a rigid piece | holds geoms and sites |
| geom | shape and mass of a body | `box`, `cylinder`, `capsule` (with `size` as MuJoCo half-sizes) or `mesh` (OBJ file). `material` (texture) or `color` (`[r, g, b]` or `[r, g, b, a]`, 0..1) sets the look of any geom type; a material wins if both are set |
| site | a named point/frame on a body | where sensors and cameras sit |
| joint | lets two bodies move relative to each other | `hinge` (rotates about the joint frame's **Z**) or `slide` |
| connector | a mounting point other parts snap to | see below |
| tendon | weighted sum of joints | used for backlash, see below |
| device | actuator, sensor, camera, display | points at a joint, site or geom |
| interface signal | a value the emulator and plots see | points at a device or joint |
| emulator | the "cheap hardware" error model, reached by firmware through the pins | picked by type, tuned by parameters |

## Mass

Pick one of two ways per body:

- **Per geom** (default): give every geom a `mass`. MuJoCo derives inertia from the shapes.
- **Override** (`overrideGeom: true`): give the body one `mass` and `inertia`, and no geom masses. Use this when a mesh's shape gives a wrong inertia (hollow or odd meshes).

Very light bodies (under about 1 g) on driven joints make the simulation stiff and jittery. Keep the moving part at least a few grams, or add `armature` to the joint.

## Connectors (how parts snap together)

- A connector's **+Z axis points out of the part**, away from the surface you mount on.
- When part B is attached to part A, B is flipped so the two connectors face each other: B's +Z points into A.
- `snap_angles` (degrees) are the allowed rotations around the connector's Z axis. Use `[0, 90, 180, 270]` for a square hole pattern, `[0, 180]` for a two-screw tab.
- Put connectors on real mounting faces: servo horn, base plate, screw tabs, bracket holes.

Turn on the **Connectors** overlay in the editor preview: X red, Y green, Z blue. The blue arrow must point away from the part.

## Joints

| Field | Effect |
|---|---|
| `range` | limits in degrees (hinge) or meters (slide) |
| `damping` | resistance proportional to speed; higher means smoother and slower |
| `armature` | extra rotor inertia; makes small, light joints stable. Real gearmotors have a lot |
| `frictionloss` | dry friction: the joint needs a minimum torque to start moving |
| `collision` | `false` stops the two bodies of this joint from colliding with each other (needed when the horn overlaps the case) |

## Devices

**Actuators** (drive a joint or tendon):

| Type | Input signal means | Key fields |
|---|---|---|
| `position` | target angle | `kp` (stiffness), `kv` (damping) |
| `velocity` | target speed | `kv` (how hard it pushes toward the target speed) |
| `motor` | torque directly | |

- `forcerange` `[min, max]` is the **torque limit** (N·m for hinges). This is the most important realism setting: set it to the real stall torque, and the part stalls under load like the real one.
- `ctrlrange` limits the command. Write it in the **same unit as the input signal that drives the actuator**: a position servo whose `target_angle` is in `degrees` uses `[-90, 90]`, a torque motor driven by an `N·m` signal uses N·m, and a motor whose signal is `rad/s` uses rad/s. It's converted for MuJoCo automatically.

**Sensors**: `jointpos`, `jointvel` (target a joint), `tendonpos`, `tendonvel` (target a tendon), `accelerometer`, `gyro` (target a site, 3 values each).

**Cameras** target a site, **displays** a geom: see [create_camera.md](create_camera.md) and [create_display.md](create_display.md).

## Pins

The part's electrical pins: `{"id": "signal", "type": "pwm", "voltage_range": [4.8, 6]}`. Ids are unique. `type` is one of `pwm`, `digital`, `analog` (signal pins), `i2c_sda`, `i2c_scl`, `spi_sck`, `spi_mosi`, `spi_miso`, `uart_tx`, `uart_rx` (bus lines), `power` (a supply input) or `ground`; leave it out while you don't know yet. I2C parts share the board's I2C bus and answer at their I2C address; SPI parts share its SPI bus and listen while their chip-select (`cs`, a `digital` pin) is LOW. `voltage_range` on a `power` pin is the supply the part takes; the wiring checks compare it with the board's 5V/3V3 and the board's logic level with the part's signal pins.

Each typed pin except power and ground is wired automatically to the ideal controller of the Prototype level. The firmware reaches it through a generated name, shown in the Inspector under **Pins (ideal board)**: `COMP_6_SIGNAL` (uid 6, pin `signal`), plus `SHOULDER_SIGNAL` when the part's name ("Shoulder") is unique. The number is the part's uid × 100 + the pin's index in this list, so it doesn't change when you add or remove other parts. At the Real level you wire the same pins to a board yourself ([wiring.md](wiring.md)); the names stay, the numbers become the board's.

## Boards

A part with a `"board"` block is a board: it runs a firmware sketch at the Real level. Its pins use two more types and three more fields:

- `"type": "gpio"`: a pin the firmware drives, with `"number"` (what the sketch passes, Uno A0 = 14) and `"caps"`: what it can do (`digital`, `pwm`, `analog`, `interrupt`, `i2c_sda`, `i2c_scl`, `spi_sck`, `spi_mosi`, `spi_miso`, `uart_tx`, `uart_rx`).
- `"type": "power_out"`: a supply output (5V, 3V3), with its voltage in `voltage_range`.
- `"internal": true` (with a `number`): a pin wired on the board's PCB to an on-board part, like the ESP32-CAM's camera pins. It can't take a wire; the sketch sees it as `<BOARD NAME>_<PIN>`.

```json
"board": {
    "fqbn": "arduino:avr:uno",               // what arduino-cli compiles for
    "header_folder": "uno",                  // project folder for its robot_pins.h
    "logic_voltage": 5,
    "adc": {"bits": 10, "vref": 5},
    "analog_write": {"hz": 490, "pins": {"d5": 980, "d6": 980}},
    "i2c": {"sda": "a4", "scl": "a5", "default_hz": 100000},
    "spi": {"sck": "d13", "mosi": "d11", "miso": "d12", "max_hz": 8000000},
    "sources": {"adc": "datasheet: ATmega328P, 10-bit ADC", "...": "..."}
}
```

`i2c`/`spi`/`analog_write.pins` name gpio pins that have the matching capability. A board may also carry devices and an emulator (the ESP32-CAM's camera). See `models/board/` for the Uno and the ESP32-CAM.

## Interface signals

What the emulator, the Inspector and plots see. Each has a unique `name`.

| Field | Effect |
|---|---|
| `target` | inputs: `joint` (drives the first actuator on that joint; also gives the Inspector slider in edit mode), `actuator`, `display`, `emulator`. Outputs: `sensor`, `camera`, `emulator` |
| `unit` | angles in `degrees` (also `deg`, `degree`) and rates in `degrees/s` (also `deg/s`, `degree/s`, `dps`) are converted automatically, so the Inspector, plots and limits all stay in degrees. Any other unit (`m`, `rad/s`, …) is a label only |
| `domain` | `{"type": "ranged", "parameters": {"min": a, "max": b}}` sets the slider range. Advisory only: it doesn't clamp |
| `physical` | `true` if the real part has a wire or pin for it; `false` for simulator-only values (a hobby servo has no position feedback wire) |
| `internal` | optional, default `false`. `true` for the part's own internal state (a servo's motor drive, pot reading, gear speed). Plots and the Inspector list these under **Internal (simulation-only)**, apart from the part's inputs and outputs |

**The emulator finds signals by name.** Use exactly the names listed below, or the part silently does nothing.

## Signals by emulator

The signal names each emulator uses, and how firmware reaches the part: through its real interface (pins, the I2C or SPI bus, the camera driver), never through these signals. See [firmware.md](firmware.md).

| Emulator type | Signal names it uses | Firmware reaches it through |
|---|---|---|
| `servo_emulator` | in `target_angle`, `motor_torque`; out `current_angle_real` (or `current_angle`), `gear_velocity`, `pot_reading`, `drive` | the `signal` pin (`Servo` library). No read-back: hobby servos have no feedback |
| `stepper_emulator` | in `motor_torque`; out `current_position`, `current_velocity`, `missed_steps` | the `step` and `dir` pins. No position read-back |
| `dc_gear_emulator` | in `motor_torque`; out `current_velocity`, `drive` | the driver pins `motor_a`, `motor_b`. No speed read-back: no encoder |
| `imu_emulator` | out `acceleration`, `gyroscope` (truth), `accel_reading`, `gyro_reading` | I2C registers (`Wire`) |
| `camera_emulator` | any output with target kind `camera` | the camera driver (`esp_camera.h`), see [create_camera.md](create_camera.md) |
| `display_emulator` | the first input with target kind `display` | SPI (`Adafruit_ST7735`), see [create_display.md](create_display.md) |
| `default_emulator` | none | nothing (passive parts) |

## Common workarounds

- **Gear backlash:** add a second, passive hinge (the "gap") on the same two bodies and axis as the drive joint (both `base -> horn`; don't add an extra body for it), with a tiny `range` (for example ±2°), a little damping (gear grease; with none the horn bounces off the end of the gap when the load reverses), `collision: false` and a small `armature` (both joints need one). Add a `fixed` tendon summing drive joint + gap joint (coef 1 each) with a `tendonpos` sensor: that's the angle the horn really has. The gap size is an error parameter, so say where it comes from in the joint's `sources` (`{"range": "estimate: ..."}`).
- **Mesh collisions use the convex hull.** A U-bracket collides as if it were a solid block. Keep collision-relevant shapes convex, or split the part into several bodies or boxes.
- **Part explodes or jitters on Play:** usually overlapping geoms between bodies of different parts, a too-light moving body, or a huge `kp` on a tiny mass. Lower `kp`, raise `armature`, or move the parts apart.
- **Nothing happens when the firmware drives the part:** the pin has no `type` (so it isn't on the board), the signal name doesn't match the emulator's (see the table above), the input isn't target kind `joint` / `actuator`, or the joint has no actuator.
- **Check your file:** from the source tree, `python3 tools/validate_rsdef.py path/to/part.rsdef` reports format errors before the app does.
