# Using RoboticsStudio

The whole workflow on one page: build a robot from parts, program it, run it, and watch the data.

## 1. Start a project

The **Hub** opens first.

- **New Project** creates a project folder (`.rsproj` + a firmware sketch) and asks for the **level**: **Prototype** (an ideal controller wires every part for you; the default) or **Real** (you add a real board, an Uno or an ESP32-CAM, and wire it). See [wiring.md](wiring.md).
- **Open / Import Project** opens an existing `.rsproj`. Recent projects are listed on the right.
- **Fetch Component Models** downloads or updates the standard part library.

A project folder holds:

| File | What it is |
|---|---|
| `<name>.rsproj` | the assembly (which parts, how they are connected), level and wiring, script list, plot layout |
| `script/<sketch>/<sketch>.ino` + `.h`/`.cpp` | robot firmware: one Arduino sketch folder per sketch, see [firmware.md](firmware.md) |
| `script/<sketch>/robot_pins.h` | in a Real copy of a Prototype project: your own file with the sketch's pin names on the board's pins ([firmware.md](firmware.md#2-pins)) |
| `script/*.js` | stage scripts (JavaScript, they move props), see [stage.md](stage.md) |
| `Components/<id>/<id>.rsdef` | parts you created yourself ("Local Components") |

## 2. Assemble the robot

The editor has a central area that switches between the **3D viewport**, the **Script** editor and, at the Real level, the **Wiring** schematic (the small vertical switch next to **▶ PLAY** at the bottom-left, or **Ctrl+1** / **Ctrl+2** / **Ctrl+3**), plus the **Components** library, the **Scene Tree**, the **Inspector**, the **Output** log and the **Plot** panel.

1. Select the project in the **Scene Tree**. The Inspector shows a `root` slot with a **+**. Drag a part from the **Components** panel onto that **+**. This is your base part.
2. Select a part. The Inspector lists its **connectors** (mounting points such as `horn`, `base`, `side_screw_left1`). Drag another part onto a connector's **+** to attach it.
3. For each attachment you can pick which connector of the **new** part to use, and a **snap angle**: the rotation around the mounting axis.
4. Joint controls in the Inspector pose the parts in edit mode (for example a servo angle). This is only for posing: the firmware drives the robot in play mode.
5. Every edit can be undone (**Ctrl+Z**). **F** frames the scene. Save with **Ctrl+S**: on the Script page it saves the open script, elsewhere the project. Unsaved changes show as `*` in the title bar, and closing the project or the app asks before discarding them.

Each part has a name (shown in the Scene Tree, renamed in the Inspector) and a uid. The firmware reaches a part through its pins, named after it: `SHOULDER_SIGNAL` for a part named "Shoulder", always also `COMP_6_SIGNAL` (uid 6). The Inspector lists a part's pins under **Pins (ideal board)**, or at the Real level the board pins it is wired to ([wiring.md](wiring.md)).

## 3. Write the firmware

The robot is programmed like a real microcontroller: an **Arduino sketch** in C++ (`.ino`), compiled when you press Play. It reaches parts through their real interfaces (pins, I2C, SPI, the camera driver) and compiles unchanged for a real board. The full guide is **[firmware.md](firmware.md)**; `examples/firmware` has one sketch per part.

```cpp
#include <Servo.h>
#include "robot_pins.h"     // Prototype: the project's pin names, generated on Play

Servo shoulder;

void setup() {
  shoulder.attach(SHOULDER_SIGNAL, 1000, 2000);   // the part's pulse range, in microseconds
}

void loop() {
  shoulder.write(45);       // 0..180 degrees, 90 = centre
  delay(1000);              // milliseconds, simulated time
  shoulder.write(135);
  delay(1000);
}
```

The Script view lists every project script on the left: a blue **M** marks a firmware sketch (it drives the robot), a green **S** a stage script (JavaScript; it drives props, see [stage.md](stage.md)). The active script of each kind, the one that runs on Play, is shown in bold. **+** creates a new firmware sketch or stage script. A project can hold several sketches; pick the active one in the Inspector (project selected, **Firmware**).

- `Serial.print(...)` prints to the Output panel.
- `delay()`, `millis()` and the other time functions use **simulated time**, so they mean the same at any speed, even when your computer can't keep up.
- What a part offers the firmware is what the real part offers: a servo has no position feedback, a DC motor no encoder. See the part's guide in this folder.

JavaScript robot scripts (`comp_<uid>`) were removed. An old project whose robot script is a `.js` file still opens: its script shows a red **!** in the Script view, and on Play the physics runs while the robot stays idle. [firmware.md](firmware.md#9-porting-a-javascript-robot-script) shows how to port it.

## 4. Run it

Press **▶ PLAY** at the bottom-left of the central area. Unsaved script edits are saved first, the sketch is compiled (the button shows "Compiling..."), and the view switches to 3D. If it doesn't compile, the run doesn't start and the errors are marked in the Script view.

- The model is rebuilt for simulation. The base part gets a free joint and falls onto the floor, so place it so it can stand.
- The firmware runs in lockstep with the physics, like a microcontroller wired to the robot: the physics waits while your code runs, and your code waits during `delay()`. Heavy code makes the whole simulation run slower, never changes what the firmware sees.
- Runs are repeatable: noise, dropped frames and other random effects come from the project's **Random Seed** (Inspector, project selected). The same seed gives exactly the same run; change it to try different luck.
- Parts behave like cheap hardware: servos have deadband and noise, cameras drop frames and have latency, and so on.
- At the Real level the active board runs its firmware slot with its own limits, the wiring checks go to the Output panel, and a part without power or ground stays dead. Supplies sag under load and a board resets on a brownout ([wiring.md](wiring.md#power)).
- **⏸ PAUSE** freezes physics, the firmware and the stage script (a running `delay()` resumes where it left off); **▶ RESUME** continues.
- **⬛ STOP** returns to edit mode, resets the physics and the emulators, and stops the firmware and the stage script. Firmware crashes and stage script errors appear in the Output panel and stop that code until the next Play.

The overlay in the viewport shows the project's level (click it for the project settings), sim time, the **real-time factor** (simulated seconds per real second; red when the simulation falls behind the selected speed), contacts and FPS. The speed selector next to **▶ PLAY** sets how fast the simulation plays: 0.25×, 0.5×, 1×, 2×, or **Max** (as fast as your computer allows). The speed never changes the results.

The world around the robot (sky, floor, props, and a stage script that moves props) is set up in the **Stage** tab. See [stage.md](stage.md).

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
| Interface | the signals the emulator, the Inspector and plots see |
| Emulator | the hardware-error model and its parameters |

**Create / Save** writes `Components/<id>/<id>.rsdef` into the project; the part then shows under **Local Components**. **Project → Edit Component** reopens it.

Step-by-step guides:

- [component_basics.md](component_basics.md): frames, connectors, mass, signals. Read this first.
- [create_servo.md](create_servo.md): hobby servo (position control).
- [create_stepper.md](create_stepper.md): stepper motor (STEP/DIR pulses).
- [create_dc_motor.md](create_dc_motor.md): geared DC motor.
- [create_driver.md](create_driver.md): motor driver module (L298N, TB6612FNG).
- [create_imu.md](create_imu.md): accelerometer and gyro board.
- [create_structural.md](create_structural.md): brackets, plates, wheels (no electronics).
- [create_camera.md](create_camera.md): RGB or depth camera.
- [create_display.md](create_display.md): screen the firmware draws on.

Ready-made parts in the `models/` folder of the app are good references. Open their `.rsdef` files next to these guides.
