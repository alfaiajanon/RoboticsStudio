# Using RoboticsStudio

The whole workflow on one page: build a robot from parts, script it, run it, and watch the data.

## 1. Start a project

The **Hub** opens first.

- **New Project** creates a project folder (`.rsproj` + a script file).
- **Open / Import Project** opens an existing `.rsproj`. Recent projects are listed on the right.
- **Fetch Component Models** downloads or updates the standard part library.

A project folder holds:

| File | What it is |
|---|---|
| `<name>.rsproj` | the assembly (which parts, how they are connected), script list, plot layout |
| `*.js` | your scripts |
| `Components/<id>/<id>.rsdef` | parts you created yourself ("Local Components") |

## 2. Assemble the robot

The editor has a **3D viewport** (center), the **Components** library, the **Scene Tree**, the **Inspector**, the **Script** editor, the **Output** log and the **Plot** panel.

1. Select the project in the **Scene Tree**. The Inspector shows a `root` slot with a **+**. Drag a part from the **Components** panel onto that **+**. This is your base part.
2. Select a part. The Inspector lists its **connectors** (mounting points such as `horn`, `base`, `side_screw_left1`). Drag another part onto a connector's **+** to attach it.
3. For each attachment you can pick which connector of the **new** part to use, and a **snap angle**: the rotation around the mounting axis.
4. Joint controls in the Inspector pose the parts in edit mode (for example a servo angle). This is only for posing: the script drives the robot in play mode.
5. Every edit can be undone (**Ctrl+Z**). **F** frames the scene. Save with **Ctrl+S**.

Each part shows its id in the Scene Tree, for example `MG996R (1)`. In scripts that part is `comp_1`.

## 3. Write the script

Scripts are JavaScript, written like an Arduino sketch: define `loop()`, and it runs over and over.

```javascript
let servo = comp_1;     // part uid 1 from the Scene Tree
let imu   = comp_2;

function loop() {
    let a = imu.getAcceleration();          // {x, y, z}
    servo.write(a.x > 2.0 ? 45 : 0);        // degrees
    delay(100);                             // milliseconds, blocking
}
```

- `console.log(...)` prints to the Output panel.
- `delay(ms)` blocks like Arduino's `delay`. It uses wall-clock time, so at a time scale other than 1× it covers a different amount of simulated time.
- What you can call on a part depends on its emulator. See the part's guide in this folder, or [component_basics.md](component_basics.md#script-api-by-emulator).
- A project can hold several scripts; the Inspector (project selected) picks the active one.

## 4. Run it

Press **▶ PLAY** in the viewport.

- The model is rebuilt for simulation. The base part gets a free joint and falls onto the floor, so place it so it can stand.
- Physics runs on its own thread and the script on another, like a microcontroller wired to the robot.
- Parts behave like cheap hardware: servos have deadband and noise, cameras drop frames and have latency, and so on.
- **⬛ STOP** returns to edit mode, resets the physics and the emulators, and stops the script. Script errors appear in the Output panel and stop the script until the next Play.

The overlay in the viewport shows FPS, sim time and contacts.

## 5. Watch the data

The **Plot** panel, **I/O** tab:

1. **+ Add Canvas** and choose its type: **Scalar** (one value over time), **Vector** (x/y/z over time) or **Image** (camera and display frames).
2. **+ Add Target** on the canvas card picks a signal of a part, for example `servo.current_angle`.
3. **View** opens the live plot window. **Disable** pauses recording.

The **Motion** tab plots the velocity or acceleration of any body of any part.

Canvases are saved with the project. Signals only record while some canvas shows them.

## 6. Make your own parts

**Components → Add Component**, or **Project → Add Component → Servo / Stepper / Blank template**, opens the **Component Editor**:

| Tab | What you set |
|---|---|
| Meta | id, name, icon, specs, pins |
| Construction | meshes/textures, bodies, geoms, sites, joints, connectors, tendons, with a live 3D preview |
| Devices | actuators, sensors, cameras, displays |
| Interface | the signals scripts and plots see |
| Emulator | the hardware-error model and its parameters |

**Create / Save** writes `Components/<id>/<id>.rsdef` into the project; the part then shows under **Local Components**. **Project → Edit Component** reopens it.

Step-by-step guides:

- [component_basics.md](component_basics.md): frames, connectors, mass, signals. Read this first.
- [create_servo.md](create_servo.md): hobby servo (position control).
- [create_stepper.md](create_stepper.md): stepper motor (velocity control).
- [create_dc_motor.md](create_dc_motor.md): geared DC motor.
- [create_imu.md](create_imu.md): accelerometer and gyro board.
- [create_structural.md](create_structural.md): brackets, plates, wheels (no electronics).
- [create_camera.md](create_camera.md): RGB or depth camera.
- [create_display.md](create_display.md): screen the script draws on.

Ready-made parts in the `models/` folder of the app are good references. Open their `.rsdef` files next to these guides.
