# The stage

The **Stage** tab (bottom panel, next to **Script**) edits the world around the robot: sky and lights, the floor, and free-standing props. A stage script can move some of those props. Every change is undoable and is saved under `"stage"` in the `.rsproj`. The tab is locked while the simulation plays.

## Environment

- **Sky top / Sky bottom**: the gradient skybox.
- **Ambient / Headlight**: the camera-attached headlight's ambient and diffuse colors.
- **Light / Light dir / Shadows / Shadow size**: one directional light. Shadow size is the shadow map resolution.
- **Fog**: start and end are multiples of the model's size.
- **Gravity / Timestep**: physics options for the whole model.

## Floor

- **plane**: an infinite plane, either `checker` (two colors, texture repeat) or `solid`.
- **mesh**: an `.stl`, `.obj` or `.msh` file. MuJoCo collides with its **convex hull**, so a mesh floor works for ramps and platforms but not for bumpy terrain.
- **heightfield**: a grayscale `.png` (white = high). Scale is half size x, half size y (m), and max height (m). Use it for terrain.
- **none**: no floor.

Files you import are copied into `<project>/stage/` so the project stays self-contained.

**Robot spawn** moves the robot's base body. The base gets a free joint in Play and falls from there, so raise it if it starts inside the floor.

## Props

**Add** creates a box, sphere, cylinder, capsule or mesh prop. Each prop has:

- **Id**: unique, letters, digits and `_`. The stage script calls the prop `prop_<id>`.
- **Size**: MuJoCo geom size. Box: half extents. Sphere: radius. Cylinder and capsule: radius, half length. Mesh: scale.
- **Position, Rotation (deg), Color, Friction, Mass**.
- **Motion**:
  - `static`: fixed in place (walls, obstacles).
  - `dynamic`: a free rigid body that falls, rolls and gets pushed.
  - `controlled`: a character body. It stays upright (it can only turn about z), falls under gravity, collides and pushes things, and follows the velocity the stage script asks for.

A controlled prop also has:

- **Max speed** (m/s) and **Max yaw** (deg/s): limits on the commands.
- **Response** (1/s): how hard it tracks the commanded velocity. Keep response × timestep below 2.

Its own **Friction** wins its contacts. Floor drag slows it by roughly friction × 9.81 / response m/s, so lower the friction (e.g. 0.1 to 0.2) for accurate speeds.

In edit mode, every prop is shown where it was authored. Physics only applies in Play.

## The stage script

The toolbar shows the stage script. **Edit Script** opens it in the Script tab, creating `stage.js` from a template if needed. **Choose...** picks another file, and **Clear** removes it.

It works like the robot script: a `loop()` function, `delay(ms)` and `console.log()`. It runs on its own thread, so a `delay()` in one script never stalls the other. Log lines are tagged `[STAGE]`.

The two scripts are kept apart:

- The **robot script** works only through the robot's parts (`comp_<uid>`). It can't see props.
- The **stage script** sees only the controlled props and the robot's pose:

| Call | Meaning |
|---|---|
| `prop_<id>.setVelocity(vx, vy)` | Velocity in the world frame (m/s) |
| `prop_<id>.move(forward, strafe)` | Velocity in the prop's own frame (m/s) |
| `prop_<id>.setYawRate(degPerSec)` | Turn rate |
| `prop_<id>.stop()` | Zero all commands |
| `prop_<id>.teleport(x, y, z, yawDeg)` | Jump there and stop |
| `prop_<id>.getPosition()` | `{x, y, z}` (m) |
| `prop_<id>.getVelocity()` | `{x, y, z}` (m/s) |
| `prop_<id>.getYaw()` | Heading (deg) |
| `robot.getPosition()` | Robot base `{x, y, z}` (m) |
| `robot.getRotation()` | Robot base `{roll, pitch, yaw}` (deg) |

Example: follow the robot.

```js
let dog = prop_dog;

function loop() {
    let r = robot.getPosition(), p = dog.getPosition();
    dog.setVelocity(0.5 * (r.x - p.x), 0.5 * (r.y - p.y));
    delay(20);
}
```

`demo/demo3` has a rover that orbits the robot in front of its camera.
