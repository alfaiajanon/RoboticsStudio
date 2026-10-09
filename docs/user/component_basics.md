# Component basics

Every part is one `.rsdef` file (JSON), built in the **Component Editor** or by hand. These rules apply to every part type; the `create_*.md` guides build on them. The full format reference is [../dev/schema2.md](../dev/schema2.md).

## Units and frames

- Lengths in **meters**, masses in **kg**, joint ranges in **degrees**. Quaternions are `[w, x, y, z]`; the editor shows rotations as roll/pitch/yaw in degrees.
- **Bodies, joints and connectors** are placed in the **component's own frame** (its origin and axes).
- **Geoms and sites** are placed in their **body's frame**.
- The first body is `default_body`: the part's origin when it is the robot's base.

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
| interface signal | a value scripts and plots see | points at a device or joint |
| emulator | the "cheap hardware" error model + script API | picked by type, tuned by parameters |

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

| Type | Script value means | Key fields |
|---|---|---|
| `position` | target angle | `kp` (stiffness), `kv` (damping) |
| `velocity` | target speed | `kv` (how hard it pushes toward the target speed) |
| `motor` | torque directly | |

- `forcerange` `[min, max]` is the **torque limit** (N·m for hinges). This is the most important realism setting: set it to the real stall torque, and the part stalls under load like the real one.
- `ctrlrange` limits the command. Write it in the **same unit as the input signal that drives the actuator**: a servo whose `target_angle` is in `degrees` uses `[-90, 90]`, and a motor whose signal is `rad/s` uses rad/s. It's converted for MuJoCo automatically.

**Sensors**: `jointpos`, `jointvel` (target a joint), `tendonpos`, `tendonvel` (target a tendon), `accelerometer`, `gyro` (target a site, 3 values each).

**Cameras** target a site, **displays** a geom: see [create_camera.md](create_camera.md) and [create_display.md](create_display.md).

## Interface signals

What scripts, the Inspector and plots see. Each has a unique `name`.

| Field | Effect |
|---|---|
| `target` | inputs: `joint` (drives the first actuator on that joint; also gives the Inspector slider in edit mode), `actuator`, `display`, `emulator`. Outputs: `sensor`, `camera`, `emulator` |
| `unit` | angles in `degrees` (also `deg`, `degree`) and rates in `degrees/s` (also `deg/s`, `degree/s`, `dps`) are converted automatically, so scripts, plots and limits all stay in degrees. Any other unit (`m`, `rad/s`, …) is a label only |
| `domain` | `{"type": "ranged", "parameters": {"min": a, "max": b}}` sets the slider range. Advisory only: it doesn't clamp |
| `physical` | `true` if the real part has a wire or pin for it; `false` for simulator-only values (a hobby servo has no position feedback wire) |

**The emulator finds signals by name.** Use exactly the names listed below, or the part silently does nothing.

## Script API by emulator

| Emulator type | Signal names it uses | Script calls |
|---|---|---|
| `servo_emulator` | in `target_angle`, out `current_angle` | `write(deg)`, `read()` |
| `stepper_emulator` | in `target_velocity`, out `current_position`, `current_velocity` | `set_velocity(rad_s)`, `set_rpm(rpm)`, `stop()`, `read_position()`, `read_velocity()` |
| `dc_gear_emulator` | in `target_velocity`, out `current_velocity` | `set_pwm(-255..255)`, `brake()`, `read_velocity()` |
| `imu_emulator` | out `acceleration`, `gyroscope` | `getAcceleration()`, `getRotation()` (objects `{x, y, z}`) |
| `camera_emulator` | any output with target kind `camera` | `capture()`, `available()`, `toGray()`, `getPixel()`, `getDepth()`, `regionMean()`, … ([create_camera.md](create_camera.md)) |
| `display_emulator` | the first input with target kind `display` | `clear()`, `fillRect()`, `drawText()`, `drawGray()`, `display()`, … ([create_display.md](create_display.md)) |
| `default_emulator` | none | none (passive parts) |

## Common workarounds

- **Gear backlash:** add a second, passive hinge (the "gap") on the same two bodies and axis as the drive joint (both `base -> horn`; don't add an extra body for it), with a tiny `range` (for example ±2°), low damping, `collision: false` and a small `armature` (both joints need one). Add a `fixed` tendon summing drive joint + gap joint (coef 1 each) with a `tendonpos` sensor: that's the angle the horn really has.
- **Mesh collisions use the convex hull.** A U-bracket collides as if it were a solid block. Keep collision-relevant shapes convex, or split the part into several bodies or boxes.
- **Part explodes or jitters on Play:** usually overlapping geoms between bodies of different parts, a too-light moving body, or a huge `kp` on a tiny mass. Lower `kp`, raise `armature`, or move the parts apart.
- **Nothing happens when the script writes:** the signal name doesn't match the emulator's (see the table above), the input isn't target kind `joint` / `actuator`, or the joint has no actuator.
- **Check your file:** from the source tree, `python3 tools/validate_rsdef.py path/to/part.rsdef` reports format errors before the app does.
