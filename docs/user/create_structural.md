# Creating structural parts (brackets, plates, wheels)

Parts without electronics: brackets, base plates and wheels hold the robot together and touch the world. Read [component_basics.md](component_basics.md) first. References: `models/joints/long_u_bracket`, `models/joints/multipurpose_bracket`, `models/wheel/wheel_D_65_25`.

## Structure

- Usually **one body** with one or more geoms and **no joints**.
- **Emulator:** leave `type` empty (or `default_emulator`). No devices, no interface.
- **Connectors are the whole point:** put one on every hole pattern you'd screw into, with +Z pointing **out** of the surface a mating part sits on. Inside and outside faces of a bracket are separate connectors with opposite Z, for example `base_inside` and `base_outside`.
- `snap_angles`: `[0, 90, 180, 270]` for square patterns, `[0, 180]` for slots.

## Mass and collision

| Concern | What to do |
|---|---|
| mass | use the real weight (aluminium bracket ≈ 10–30 g, wheel ≈ 30–100 g). Wrong masses change how the servos sag and how the robot balances |
| hollow meshes | collision uses the mesh's **convex hull**, so a U-bracket collides as a solid block. Usually fine; if parts must sit inside the U, build the bracket from several box geoms instead of one mesh |
| odd inertia | if the robot tumbles strangely, set `overrideGeom: true` with the real mass and an `inertia` estimate |

## Wheels

- **Geometry:** a `cylinder` geom (or a mesh) whose axis matches the motor shaft. The connector at the hub has Z along the axle, pointing out of the side that mounts on the motor.
- **Mass:** give it real mass; a massless wheel slips and bounces.
- **Collision:** wheel meshes collide as their convex hull, which is fine for a round wheel. A cylinder primitive is cheaper and rolls more smoothly than a faceted mesh.
- **No joint in the wheel:** the motor's joint turns it.

## Workarounds

- **A part that should only look, not collide** (decorative covers): not configurable per geom in this version. Keep it small or inside other geoms.
- **Rubber grip:** MuJoCo's default friction is used. If wheels slip, increase the robot's weight or use more wheels. Per-geom friction isn't exposed in the format yet.
