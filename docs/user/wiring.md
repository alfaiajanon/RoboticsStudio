# Levels, boards and wiring

A project has a **level**. It decides what runs your firmware and how much of the electronics you deal with.

| Level | What runs the firmware | Pins | Electrical |
|---|---|---|---|
| **Prototype** (default) | One ideal controller, built in | Unlimited; every part is wired automatically | Ideal: buses take no time, no power limits |
| **Real** | A board you add from the library (Arduino Uno, ESP32-CAM) | The board's real pins; you wire them | The board's limits: which pins have PWM or an ADC, PWM frequency, ADC resolution, bus speed, logic voltage; parts without power or ground are dead |

The firmware API is the same at both levels, and so are the pin **names** in `robot_pins.h`. A sketch written at the Prototype level keeps working at the Real level; only the numbers behind the names change.

## Choosing the level

- **New Project** asks for the level. A Real project starts with **no board**.
- Later: select nothing in the robot tree (or click the level badge in the top-left of the 3D view) and change **Level** in the Inspector. Both directions are one undo step.
  - **Prototype → Real**: add a board from the library (**Components → Boards**). The first board you add takes over the project's firmware sketch.
  - **Real → Prototype**: the ideal board wires everything again. Your wiring stays in the project, unused, until you switch back.

## Boards

A board is a component like any other: drag it from **Components → Boards** onto a free connector (the stand, a bracket). Select it to see, in the Inspector:

- **Firmware**: the sketch this board runs (its *firmware slot*). Pick any `.ino` of the project, or create one with **+** in the Script view.
- **Pins in robot_pins.h**: what is wired to its pins, under the names the sketch uses.

| Board | Logic | PWM (`analogWrite`) | ADC | I2C | SPI |
|---|---|---|---|---|---|
| Arduino Uno R3 | 5 V | D3, D5, D6, D9, D10, D11 (`~` on the schematic); 490 Hz, 980 Hz on D5/D6 | A0-A5, 10-bit on 5 V | A4 (SDA), A5 (SCL) | D11 MOSI, D12 MISO, D13 SCK, up to 8 MHz |
| ESP32-CAM (AI-Thinker) | 3.3 V | every header GPIO except 1, 3, 16; 1 kHz | 12-bit on 3.3 V | IO14 (SDA), IO15 (SCL), as `Wire.begin(14, 15)` | IO14 SCK, IO13 MOSI, IO12 MISO |

The **ESP32-CAM has its camera on the board**: its OV2640 is wired to the ESP32 on the PCB, so it needs no wires. The sketch reaches it through the board's own pin names (`ESP32_CAM_SDA`, `ESP32_CAM_D0`, ...; see [create_camera.md](create_camera.md#firmware-esp32-camera)). A separate camera module (the OV7670) is a part you wire like any other.

One board runs at a time for now. A second board shows up, but stays idle until boards can talk to each other (UART, a later step).

## The Wiring page

At the Real level the editor gets a third page, **Wiring** (**Ctrl+3**, or the wire icon under the page switch): a schematic of the boards and of every part with typed pins.

- **Wire**: drag from one pin to another. A pin can take several wires (5V to two servos, for example). All pins joined by wires, directly or through each other, form one **net**.
- **Unwire**: click a wire (or drag a box around several) and press **Delete**.
- **Arrange**: drag the blocks. A part's pins move to the side facing the board.
- **Look**: hover a pin to light up its whole net, with a tooltip (type, board pin number, voltage range, problems). Wheel zooms; right-drag pans; **Fit** shows everything.
- Every change is undoable (**Ctrl+Z**) and saved with the project.
- The page is read-only while the simulation runs.

Wire colours follow the net: red for power, grey for ground, orange PWM, blue digital, green analog, purple I2C, teal SPI.

## Checks

The side panel lists what a real board would do wrong with this wiring. A red or amber badge marks the part and the pin on the schematic; click a line to show the part. The same list goes to the **Output** panel on Play. The checks never stop Play: the run does what the hardware would.

| Check | What happens on Play |
|---|---|
| No board | The physics runs; nothing drives the parts |
| A part's power pin not on a 5V/3V3 net, or its GND not on the board's GND | The part is **dead**: a servo goes limp, an IMU doesn't answer, a display stays dark |
| A signal or bus pin not wired | It floats: the part gets nothing |
| An I2C part's SDA/SCL not on the board's I2C pins (an SPI part's SCK/MOSI not on its SPI pins) | The part doesn't answer on the bus (`endTransmission()` returns 2) |
| A PWM input on a pin without hardware PWM | `Servo` still works (the library makes its own pulses); `analogWrite()` there acts as `digitalWrite()`: HIGH from 128 |
| An analog input on a pin without an ADC | `analogRead()` returns 0 |
| Two board pins, or a supply and ground, on one net | A short: fix the wiring |
| A 5 V board driving a 3.3 V part's signal pin | Flagged: add a level shifter |
| A supply outside the part's voltage range (a servo on 3V3) | Flagged; the voltage itself isn't modelled yet |

## What changes at the Real level

- **`robot_pins.h`** comes from the wiring: the same names (`EMULATED_SERVO_SIGNAL`), the board's numbers (9). On every save and Play it is also written to the project's board folder (`uno/robot_pins.h`, `esp32cam/robot_pins.h`), ready for the real board ([firmware.md](firmware.md#8-move-it-to-a-real-board)).
- **PWM**: `analogWrite()` runs at the board's frequency for that pin; on a pin without PWM it is a plain HIGH/LOW.
- **ADC**: `analogRead()` gives the board's counts (0..1023 on an Uno, 0..4095 on an ESP32).
- **Bus time**: an I2C transaction takes its bit time (9 bits per byte at 100 kHz, or the `Wire.setClock()` value); an SPI transfer 8 bits per byte at its clock. Reading the MPU6050's 14 data bytes takes about 1.5 ms at 100 kHz.
- **Servo libraries** keep their own habits at every level: the Uno's `Servo` starts pulsing at 1500 µs on `attach()`; `ESP32Servo` sends nothing until the first `write()`, and its default 10-bit timer gives 19.5 µs steps (about 3.5° on a 1000..2000 µs servo) until `setTimerWidth(16)` is called after `attach()`.

## Not modelled yet

- Power: supply voltage, current, sag and brownout (a stalled servo doesn't pull the 5V rail down yet).
- Several boards talking to each other (UART), and interface boards (PCA9685, motor drivers, level shifters, battery packs).
- A guided Prototype → Real migration that maps the ideal wiring onto a board for you.

## Demos

`demo/Servo Comparison (Uno)` (an Uno on the stand, servos on D9 and D10) and `demo/Robot arm (ESP32-CAM)` (the ESP32-CAM on the arm's tip, servos on IO13-15) are Real projects. Their Prototype twins (`demo/Servo Comparison`, `demo/Robot arm`) run the same firmware on the ideal board.
