# Levels, boards and wiring

A project has a **level**. It decides what runs your firmware and how much of the electronics you deal with.

| Level | What runs the firmware | Pins | Electrical |
|---|---|---|---|
| **Prototype** (default) | One ideal controller, built in | Unlimited; every part is wired automatically | Ideal: buses take no time, no power limits |
| **Real** | A board you add from the library (Arduino Uno, ESP32-CAM) | The board's real pins; you wire them | The board's limits: which pins have PWM or an ADC, PWM frequency, ADC resolution, bus speed, logic voltage; and [power](#power): supplies sag under load, parts without enough supply are dead, the board resets on a brownout |

The firmware API is the same at both levels. The pins differ. At the Real level a sketch uses the board's own pins, as on the real board (`servo.attach(9)`, `analogRead(A0)`). At the Prototype level, which has no real pins, it uses the names of the generated `robot_pins.h` ([firmware.md](firmware.md#2-pins)).

## Choosing the level

- **New Project** asks for the level, and the project keeps it. A Real project starts with **no board**.
- **Prototype → Real** is one way, into a new project: select nothing in the robot tree (or click the level badge in the top-left of the 3D view) and press **Create Real copy...** in the Inspector. The copy opens next to the Prototype project, which stays as it is. Then:
  1. Add a board from the library (**Components → Boards**). The first board you add takes over the project's firmware sketch.
  2. Wire the parts to it (Wiring page, below).
  3. Set the pins in the sketch. The copy has a `robot_pins.h` of its own with the Prototype's names set to `-1` (not wired yet): put the board pins there, or write the numbers into the sketch ([firmware.md](firmware.md#2-pins)).
- There is no way back to Prototype: keep the Prototype project for that.

## Boards

A board is a component like any other: drag it from **Components → Boards** onto a free connector (the stand, a bracket). Select it to see, in the Inspector:

- **Firmware**: the sketch this board runs (its *firmware slot*). Pick any `.ino` of the project, or create one with **+** in the Script view.
- **Pins**: what is wired to each of its pins (`D9 (pin 9): Emulated Servo signal`); the sketch uses the pin numbers.

| Board | Logic | PWM (`analogWrite`) | ADC | I2C | SPI |
|---|---|---|---|---|---|
| Arduino Uno R3 | 5 V | D3, D5, D6, D9, D10, D11 (`~` on the schematic); 490 Hz, 980 Hz on D5/D6 | A0-A5, 10-bit on 5 V | A4 (SDA), A5 (SCL) | D11 MOSI, D12 MISO, D13 SCK, up to 8 MHz |
| ESP32-CAM (AI-Thinker) | 3.3 V | every header GPIO except 1, 3, 16; 1 kHz | 12-bit on 3.3 V | IO14 (SDA), IO15 (SCL), as `Wire.begin(14, 15)` | IO14 SCK, IO13 MOSI, IO12 MISO |

The **ESP32-CAM has its camera on the board**: its OV2640 is wired to the ESP32 on the PCB, so it needs no wires. The sketch reaches it through the GPIOs it is wired to on the PCB, the usual AI-Thinker list (`SIOD_GPIO_NUM 26`, `Y2_GPIO_NUM 5`, ...; see [create_camera.md](create_camera.md#firmware-esp32-camera) and the Robot arm (ESP32-CAM) demo). A separate camera module (the OV7670) is a part you wire like any other.

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
| A part's power pin on a rail nothing feeds, or its GND not common with the board's GND | The part is **dead**: a servo goes limp, an IMU doesn't answer, a display stays dark |
| A signal or bus pin not wired | It floats: the part gets nothing |
| An I2C part's SDA/SCL not on the board's I2C pins (an SPI part's SCK/MOSI not on its SPI pins) | The part doesn't answer on the bus (`endTransmission()` returns 2) |
| A PWM input on a pin without hardware PWM | `Servo` still works (the library makes its own pulses); `analogWrite()` there acts as `digitalWrite()`: HIGH from 128 |
| An analog input on a pin without an ADC | `analogRead()` returns 0 |
| Two board pins, a board pin and a supply, or a supply and ground, on one net | A short: fix the wiring |
| Two supplies driving one rail (a battery on the Uno's 5V while VIN is also fed) | A short: they back-feed each other |
| A 5 V board driving a 3.3 V part's signal pin | Flagged: add a level shifter |
| A supply outside the part's voltage range (a servo on 3V3, a 9 V battery on a servo) | Flagged; the part runs at that voltage (a servo is weaker on less) |
| A regulator's input too low for its output (a 4×AA pack at half charge on the Uno's VIN) | Flagged; its output follows the input down |
| More current than a supply is rated for: at rest, or with the motors stalled | Flagged with the numbers; see [Power](#power) |
| A board's supply rail that would fall below its brownout threshold with the motors stalled | Flagged: the board would reset |

## What changes at the Real level

- **Pins**: the sketch uses the board's own pin numbers and names (`9`, `A0`, `LED_BUILTIN`), from the real Arduino core's variant for that board, along with its macros (`ARDUINO_AVR_UNO`, `ESP32`). The sketch is the same file you upload to the real board ([firmware.md](firmware.md#8-move-it-to-a-real-board)).
- **PWM**: `analogWrite()` runs at the board's frequency for that pin; on a pin without PWM it is a plain HIGH/LOW.
- **ADC**: `analogRead()` gives the board's counts (0..1023 on an Uno, 0..4095 on an ESP32).
- **Bus time**: an I2C transaction takes its bit time (9 bits per byte at 100 kHz, or the `Wire.setClock()` value); an SPI transfer 8 bits per byte at its clock. Reading the MPU6050's 14 data bytes takes about 1.5 ms at 100 kHz.
- **Servo libraries** keep their own habits at every level: the Uno's `Servo` starts pulsing at 1500 µs on `attach()`; `ESP32Servo` sends nothing until the first `write()`, and its default 10-bit timer gives 19.5 µs steps (about 3.5° on a 1000..2000 µs servo) until `setTimerWidth(16)` is called after `attach()`.

## Power

At the Real level every supply has a voltage and an internal resistance, and every part draws current from the rail it is wired to. The simulation solves the rails once per physics step (2 ms), so a motor that pulls hard makes its rail sag, and everything on that rail sees it. At the Prototype level power is ideal.

**Sources** (**Components → Power**):

| Part | What it is | Set in the Inspector |
|---|---|---|
| A board's **USB feed** | The board's own 5 V (Uno: through its 0.5 A polyfuse; ESP32-CAM: through its programmer). Dropped as soon as another source drives that rail | — |
| **Battery pack 4×AA**, **Battery 9V**, **LiPo 2S** | Voltage and internal resistance follow the battery's discharge curve (per cell, from its datasheet) | **Charge** (%), **Drains during the run**; LiPo: **Protection circuit (BMS)** |
| **Bench power supply** | Constant voltage up to its current limit, then the voltage drops | **Voltage**, **Current limit** |
| **Buck converter LM2596** | Steps a higher input down (≈ 85 % efficient); needs ~1.5 V of headroom; folds back above its current limit (4.5 A, rated 3 A) | **Output** |

Boards carry their own regulators: the Uno makes 5V from VIN (needs about 6.1 V or more) and 3V3 from 5V; the ESP32-CAM makes its 3V3 from 5V (its AMS1117 loses about 1.1 V). Wire a battery to the Uno's **VIN**, or a 5 V supply straight to a board's **5V** pin; either way connect its **−** to the board's **GND** (a common ground).

**What the run does:**
- **Sag**: a supply's voltage drops by its internal resistance times the current. Servos and steppers get the voltage of their rail: a servo is weaker and slower on less.
- **Too little supply**: a part switches off below its minimum voltage (an MG996R's electronics stop around 3.5 V) and starts again from its power-on state when the rail comes back.
- **Brownout**: when a board's MCU rail falls below its brownout threshold (Uno 2.7 V on 5V, ESP32 2.43 V on 3V3), the board resets. Its pins let go (servos go limp), and once the rail is back the sketch starts again from `setup()` (`millis()` from 0). Output: `MCU: ESP32-CAM brownout reset at 0.348 s (2.37 V on its supply rail)`. Too many servos on one small supply give exactly the real failure: the board keeps resetting whenever they start moving.
- **USB**: more than 0.5 A from a board's USB feed for over a second gets a warning (a real polyfuse cuts out; that cut itself isn't simulated).
- **Batteries**: with **Drains during the run** on, the charge goes down with the current drawn and the voltage follows the discharge curve (at Max speed you can watch a pack run down). Every Play starts from the charge set in the Inspector. They behave like the real chemistry:
  - **The shape**: an alkaline pack has no flat part, its voltage slopes down all the way; a LiPo stays flat, then falls steeply in its last few %.
  - **Heavy loads empty it sooner**: the voltage reaches the cut-off before the rated charge is used. A 4×AA pack at 0.5 A is done with about half its charge left on the gauge; a LiPo loses only a few %. After the load stops the voltage recovers a little.
  - **0 % is not the end**: 0 % is the rated cut-off, where a fuel gauge would read empty. Past it the pack keeps discharging and its voltage collapses (the gauge stays at 0 %). Output says when a pack reads 0 %, when a LiPo is over-discharged (below 3.0 V per cell: a real one is damaged) and when it is completely flat.
  - **Protection circuit**: a hobby LiPo pack has none, so it runs down until the electronics give up. Tick **Protection circuit (BMS)** to add one (DW01A-class: cuts the pack off below 2.4 V per cell for 40 ms, until Stop).
  - A draining Robot arm (ESP32-CAM) ends as a real one does: the LiPo sags below the buck's ~6.5 V headroom, the 5V rail follows it down, the servos get weaker, the pack becomes over-discharged, and finally the ESP32-CAM goes into a brownout loop (it resets every time the servos start).
- At **Stop**, Output lists each supply's lowest voltage and highest current, each battery's charge left and each board's brownout resets.

**Plots**: in a scalar plot's target list, each part has a **Power (simulation-only)** group: `power.supply_v` and `power.supply_a` for a part, every rail (`power.5v_v`, ...) for a board, `power.charge`, `power.charge_available` (what the cell can give right now), `power.battery_v` and `power.battery_a` for a battery.

**The budget check** works with datasheet numbers: what each supply carries at rest, and with every motor stalled (the worst case, e.g. all servos starting at once). The stalled case is rare but real: if the check says a board's rail would brown out, give the motors their own supply, with a common ground.

**Simplifications:** the rails are solved with the currents of the step before (a 2 ms lag, like a rail's capacitors); no ripple or transients; a battery's quick voltage rebound in the first seconds after a load stops isn't modelled (only the slower recovery); temperature doesn't change a battery; the DC motor still runs on its built-in ideal driver at a fixed voltage until motor driver parts arrive.

## Not modelled yet

- Several boards talking to each other (UART), and interface boards (PCA9685, motor drivers, level shifters).
- A real polyfuse's cut-out, and damage (a pin or a part driven past its absolute maximum, a LiPo over-discharged: only a warning).
- A protection circuit's over-current and short-circuit cut-outs (only its under-voltage cut-out is modelled), and charging.
- A guided Prototype → Real migration that maps the ideal wiring onto a board for you.

## Demos

`demo/Servo Comparison (Uno)` (an Uno on the stand, servos on D9 and D10, powered by USB) and `demo/Robot arm (ESP32-CAM)` (the ESP32-CAM on the arm's tip, servos on IO13-15, everything fed by a 2S LiPo through an LM2596 at 5 V with a common ground) are Real projects. Their Prototype twins (`demo/Servo Comparison`, `demo/Robot arm`) run the same firmware on the ideal board.
