<p align="center">
  <img src="assets/icon.png" width="100" alt="RoboticsStudio Logo" />
</p>

<h1 align="center">RoboticsStudio</h1>

<p align="center">
    <img src="https://img.shields.io/github/forks/alfaiajanon/RoboticsStudio" alt="Forks">
    <img src="https://img.shields.io/github/stars/alfaiajanon/RoboticsStudio" alt="Stars">
    <img src="https://img.shields.io/github/license/alfaiajanon/RoboticsStudio" alt="License">
    <img src="https://img.shields.io/github/v/release/alfaiajanon/RoboticsStudio?include_prereleases" alt="Latest Release">
</p>

<p align="center"><b>A zero-setup simulator for hobby-grade robots, built on MuJoCo.</b></p>

<p align="center">
  <img src="assets/demo.png" width="600" alt="RoboticsStudio Interface Demo" />
</p>

---

## Why does this exist?

Students and hobbyists build robots from cheap parts: $3 SG90 servos with gear backlash and deadband, MPU6050 IMUs with noise and drift, and an Arduino powered by a battery that sags when the motors stall. A controller that works in an ideal simulation often falls apart on that hardware.

Gazebo, Isaac Sim and Webots can model sensor noise and actuator dynamics, but you set the numbers yourself, and ready-made models of cheap hobby parts (deadband, backlash, PWM resolution) are rare. They also take real setup work before you can test a simple PID loop.

RoboticsStudio aims at that gap: drag and drop parts that come with their flaws pre-configured, write the controller, and watch it run. Today some of those models are still simple; v2 rebuilds them around how each real part works inside (see [Where it's going](#where-its-going)). The goal is that what you learn, and eventually the firmware you write, carries over to the real robot. Built with C++ and Qt; it runs on an ordinary laptop without a dedicated GPU.

## Download & Run (Zero Setup)

RoboticsStudio is packaged as a standalone AppImage for Linux. No dependencies, no complex installation, no compiling required.

1. Download the latest `.AppImage` from the [Releases page](https://github.com/alfaiajanon/RoboticsStudio/releases/latest).
2. Make it executable:
   ```bash
   chmod +x RoboticsStudio-Linux-x86_64.AppImage
   ```
3. Run it:
   ```bash
   ./RoboticsStudio-Linux-x86_64.AppImage
   ```

## Key Features

- `Visual Assembly:` Drag and drop parts (servos, DC and stepper motors, IMUs, cameras, displays, brackets, wheels) and snap them together at their connectors.
- `Component Library (.rsdef):` Each part is a JSON file mapping the real hardware to MuJoCo bodies, joints, actuators and sensors, plus its error model. Create your own in the built-in Component Editor.
- `Hardware Errors:` Physical flaws (friction, rotor inertia, torque limits, gear backlash) live in the MuJoCo model. Every part models its own mechanism along the real signal path, with sourced parameters: the servo (50 Hz pulses, deadband on the position error, backlash inside the loop, torque–speed limits), the DC gear motor (H-bridge, torque–speed line, gearbox friction), the stepper (the real torque-angle curve, so steps are lost under load), the MPU6050 (its register map, bias, noise density, filter, range and resolution), the TFT display (an ST7735 controller on SPI) and the camera (latency, noise, dropped frames), with unit-to-unit variation.
- `Firmware:` Program the robot as a real Arduino sketch in C++. It is compiled inside the app, runs in lockstep with the physics, and the same file compiles for a real Uno or ESP32-CAM. Every part is reached through its real interface: pins, Wire (I2C), SPI, the Adafruit display library and the ESP32 camera driver.
- `Levels, Boards and Wiring:` _Prototype_ projects run on an ideal controller that wires every part itself. _Real_ projects use a real board from the library (Arduino Uno, ESP32-CAM with its built-in camera), wired pin by pin on a schematic page, with the board's own limits (PWM pins and frequency, ADC resolution, bus speed, logic level) and checks for what a real build would get wrong: a missing ground, a short, a part on the wrong bus pins, 5 V logic into a 3.3 V part.
- `Cameras and Displays:` RGB and depth cameras the firmware reads through the ESP32 camera driver, and TFT screens it draws on.
- `Stage:` The world around the robot: environment, floor, props, and a separate stage script (JavaScript) that moves props.
- `Live Telemetry:` Plot any input, output or body motion in real time.

## Programming the robot

**Firmware (C++).** The robot's controller is an Arduino sketch. Parts are reached through their pins, named in a generated `robot_pins.h`:

```cpp
#include <Servo.h>
#include "robot_pins.h"

Servo shoulder;

void setup() {
  shoulder.attach(SHOULDER_SIGNAL, 1000, 2000);   // the SG90's pulse range
}

void loop() {
  shoulder.write(45);
  delay(500);
  shoulder.write(135);
  delay(500);
}
```

Press Play and the sketch is compiled to WebAssembly with a bundled clang and run in lockstep with the physics. `delay()`, `millis()` and `micros()` use simulated time, so timing is exact at any simulation speed, and runs with the same random seed repeat exactly. Give the sketch a `robot_pins.h` for your board and the **same file** compiles for a real board (Uno, ESP32-CAM): the demo sketches are checked with `arduino-cli` on every push. Details and limits: [docs/user/firmware.md](docs/user/firmware.md).

Firmware reaches every part: servos, DC motors (two driver pins), steppers (STEP/DIR), the MPU6050 (`Wire`), TFT displays (`Adafruit_ST7735`) and cameras (`esp_camera`); `examples/firmware` has one sketch per part. JavaScript is used only for stage scripts; robot scripts in JavaScript were removed (porting guide in [firmware.md](docs/user/firmware.md#9-porting-a-javascript-robot-script)).

## Demos

Each demo in `demo/` comes as a pair: a _Prototype_ copy on the simulator's ideal board, and a _Real_ copy with its board wired in the simulator, whose generated `robot_pins.h` is compiled for that board on every push.

- **Robot arm** / **Robot arm (ESP32-CAM)**: a three-servo arm with a camera that searches for an orange ball and follows it (vision, filtering, visual servoing, inverse kinematics). The Real copy carries an ESP32-CAM on the arm's tip (its own camera; servos on IO13-15) and uses `ESP32Servo`.
- **Servo Comparison** / **Servo Comparison (Uno)**: two SG90s sweep together, one emulated (deadband, noise, backlash, unit spread), one ideal. The Real copy has an Uno on the stand, servos on D9 and D10.

## Where it's going

RoboticsStudio is being reworked into a robotics simulator whose code transfers to the real robot. The plan, in order:

- **Simulated time as the only clock.** Done: the controller runs in lockstep with the physics, and runs are reproducible.
- **Device models along the real signal path.** Done for every part. Each part's errors follow its internal mechanism (for a servo: PWM latching, deadband inside its control loop, backlash inside the loop, speed and torque limits), and every parameter states its source: a datasheet or a measurement.
- **Wiring and real boards.** Done for one board: _Real_ projects add a board (Uno, ESP32-CAM) like any other part, wire it on a schematic, and run its firmware with the board's pins, PWM, ADC and bus timing; the editor checks the wiring like a real build would fail. Next: the power budget (supply sag, brownout), several boards linked by UART, interface boards (PCA9685, motor drivers), and a guided Prototype → Real migration.
- **C++ firmware with the Arduino API.** Done: the robot is programmed in C++ (`Servo`, `Serial`, `millis`, `analogWrite`), compiled to WebAssembly in the app, and the same sketch compiles for a real Uno. Every part is reachable from firmware (`Wire`, `SPI`, the Adafruit display library, the ESP32 camera driver), and the demos are ported.
- **A built-in robotics library.** Kinematics taken from the assembly, inverse kinematics, trajectories, PID and filters.
- **Validation.** Measurements of real parts plotted against the simulation.

JavaScript stays only for stage scripts. The design is in [docs/dev/architecture-v2.md](docs/dev/architecture-v2.md).

## Documentation

- [Using RoboticsStudio](docs/user/overview.md): projects, assembly, firmware, running, plotting.
- [Robot firmware (C++)](docs/user/firmware.md): sketches, pin names, the supported Arduino API, moving to a real board.
- [Levels, boards and wiring](docs/user/wiring.md): the Prototype and Real levels, boards, the Wiring page and its checks.
- [The stage](docs/user/stage.md): environment, floor, props and the stage script.
- Creating parts: [basics](docs/user/component_basics.md), [servo](docs/user/create_servo.md), [stepper](docs/user/create_stepper.md), [DC motor](docs/user/create_dc_motor.md), [IMU](docs/user/create_imu.md), [brackets & wheels](docs/user/create_structural.md), [camera](docs/user/create_camera.md), [display](docs/user/create_display.md).
- Developer docs (architecture, file format, internals): [docs/dev](docs/dev/overview.md).

## Building from Source

If you prefer to compile the application yourself:

```bash
git clone https://github.com/alfaiajanon/RoboticsStudio.git
cd RoboticsStudio
tools/fetch_wasi_sdk.sh   # once: the firmware compiler (wasi-sdk), into extern/
./run.sh
```

## Contributing

RoboticsStudio relies on community-driven hardware profiles. If you have mapped a real sensor, motor or structural part to an `.rsdef` file, please open a Pull Request to add it to the standard library. For every error parameter, say where the number comes from: a datasheet (with a link) or your own measurement (with how you measured it). A profile without sources cannot claim to be realistic.
