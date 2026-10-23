# Levels, boards and wiring

A project has a **level**. It decides what runs your firmware and how much of the electronics you deal with.

| Level | What runs the firmware | Pins | Electrical |
|---|---|---|---|
| **Prototype** (default) | One ideal controller, built in | Unlimited; every part is wired automatically | Ideal: buses take no time, no power limits |
| **Real** | The boards you add from the library (Arduino Uno, ESP32-CAM), as many as the robot needs | The boards' real pins; you wire them | The board's limits: which pins have PWM or an ADC, PWM frequency, ADC resolution, bus speed, logic voltage; and [power](#power): supplies sag under load, parts without enough supply are dead, the board resets on a brownout |

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

| Board | Logic | PWM (`analogWrite`) | ADC | I2C | SPI | Serial |
|---|---|---|---|---|---|---|
| Arduino Uno R3 | 5 V | D3, D5, D6, D9, D10, D11 (`~` on the schematic); 490 Hz, 980 Hz on D5/D6 | A0-A5, 10-bit on 5 V | A4 (SDA), A5 (SCL) | D11 MOSI, D12 MISO, D13 SCK, up to 8 MHz | `Serial` on D0 (RX) / D1 (TX), shared with USB; `SoftwareSerial` on any pins; 64-byte buffers |
| ESP32-CAM (AI-Thinker) | 3.3 V | every header GPIO except 1, 3, 16; 1 kHz | 12-bit on 3.3 V | IO14 (SDA), IO15 (SCL), as `Wire.begin(14, 15)` | IO14 SCK, IO13 MOSI, IO12 MISO | `Serial` on IO3 (RX) / IO1 (TX), the programmer; `Serial1`, `Serial2` on any GPIO; 256-byte RX buffer |

The **ESP32-CAM has its camera on the board**: its OV2640 is wired to the ESP32 on the PCB, so it needs no wires. The sketch reaches it through the GPIOs it is wired to on the PCB, the usual AI-Thinker list (`SIOD_GPIO_NUM 26`, `Y2_GPIO_NUM 5`, ...; see [create_camera.md](create_camera.md#firmware-esp32-camera) and the Robot arm (ESP32-CAM) demo). A separate camera module (the OV7670) is a part you wire like any other.

## Several boards

A robot can have several boards, each with its own sketch: an ESP32-CAM that sees and an Uno that drives the servos, a board per leg. **Every board with a sketch in its firmware slot runs at Play**, all at once, on the same simulated clock, so a run with several boards still repeats exactly. A board without a sketch stays idle (its pins float). The 3D view's level badge reads "Real · 2 boards".

Each board prints to its own tab of the **Serial Monitor** (next to Output), and its lines also appear in Output, tagged with the board's name: `[Uno]: ...`, `[ESP32-CAM]: ...`.

Boards talk over wires, as on the bench:

- **Serial (UART)**: wire one board's TX to the other's RX, and back if both send. **Wire their GNDs together**: a signal is a voltage against ground. A 5 V board's TX into a 3.3 V board's RX needs a [level shifter](#level-shifter) (the ESP32 is not 5 V tolerant).
- Which port: on an Uno, `Serial` is D0/D1, but those pins are also its USB link to the computer, so a sketch that uses the Serial Monitor talks to another board with `SoftwareSerial` on two other pins. On an ESP32, `Serial1` or `Serial2` go to any two GPIOs: `Serial1.begin(57600, SERIAL_8N1, rxPin, txPin)`. Details: [firmware.md](firmware.md#serial-ports).
- **A plain signal**: one board's output pin on another's input (a "ready" line, an interrupt) works too. Two boards both driving the same wire are warned about at run time.

**On the wire:**
- A byte takes 10 bits at the baud rate (8N1): 0.87 ms at 115200, 1.04 ms at 9600. It reaches the other board when its stop bit ends.
- **Both ends must use the same baud rate.** A receiver more than about 2.5 % off reads garbage, as on the bench; Output names both rates.
- What comes in waits in the receiving board's buffer (64 bytes on an Uno, `Serial` and `SoftwareSerial`; 256 on an ESP32) until the sketch reads it. A sketch that doesn't read often enough loses bytes: Output says when a buffer overflowed, and `SoftwareSerial::overflow()` returns true.
- Sending is buffered too (64 bytes on an Uno, the 128-byte FIFO on an ESP32): `print()` returns at once while there is room and waits while the buffer is full. `SoftwareSerial` has no buffer: `write()` takes the whole byte's time.
- A board listens only from its `begin()` on: what was sent before is lost. Two boards that start at the same time race, as on the bench, so a robust link repeats its messages (the demo sends a full command every tick).
- Only one `SoftwareSerial` receives at a time, the one that called `listen()` last (`begin()` listens).
- Boards take turns within each physics step (in the order they were added), so bytes from a board that runs later reach one that runs earlier up to one physics step (2 ms) late. The order is fixed, so this doesn't change from run to run.

### Level shifter

**Components → Interface → Logic level shifter (BSS138, 4 channels)** connects a 5 V board to a 3.3 V board or part. Wire:

- **HV** to the 5 V side's supply (the Uno's 5V), **LV** to the 3.3 V side's (the ESP32-CAM's 3V3), and **GND** to the common ground. LV must not be above HV.
- Each channel's **HVn** to the 5 V board's pin and **LVn** to the 3.3 V pin. Channels work both ways: the same module carries TX in one direction and RX in the other, or I2C's SDA and SCL.

At run time a powered shifter passes each channel's signal through unchanged: digital levels, I2C transactions (an MPU6050 on the 3.3 V side answers an Uno on the other) and serial bytes. Without its supply nothing crosses, and the checks say so. It is rated for about 400 kbit/s (its 10 kΩ pull-ups make slow edges): I2C at 400 kHz and serial up to 400000 baud. Faster serial through it gets a warning; fast SPI needs a push-pull shifter.

## The Wiring page

At the Real level the editor gets a third page, **Wiring** (**Ctrl+3**, or the wire icon under the page switch): a schematic of the boards and of every part with typed pins.

- **Wire**: drag from one pin to another. A pin can take several wires (5V to two servos, for example). All pins joined by wires, directly or through each other, form one **net**.
- **Unwire**: click a wire (or drag a box around several) and press **Delete**.
- **Arrange**: drag the blocks. A part's pins move to the side facing the board.
- **Look**: hover a pin to light up its whole net, with a tooltip (type, board pin number, voltage range, problems). Wheel zooms; right-drag pans; **Fit** shows everything.
- Every change is undoable (**Ctrl+Z**) and saved with the project.
- The page is read-only while the simulation runs.

Wire colours follow the net: red for power, grey for ground, orange PWM, blue digital, green analog, purple I2C, teal SPI, yellow serial.

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
| Two pins of the same board, a board pin and a supply, or a supply and ground, on one net | A short: fix the wiring |
| Two boards' serial TX pins on one wire (Uno D1 to ESP32-CAM IO1) | Error: two outputs fight; wire TX to RX. Two RX pins together: nothing sends on that wire |
| Two boards wired together whose GNDs aren't | Error: the signals have no common reference |
| A 5 V board's pin wired straight to a 3.3 V board's pin | Flagged: add a level shifter |
| A level shifter without LV, HV or GND | It is dead: no signal crosses it |
| A level shifter with LV above HV, or a board on the wrong side (a 5 V pin on LVn, a 3.3 V pin on HVn) | Flagged: swap the wires |
| Two supplies driving one rail (a battery on the Uno's 5V while VIN is also fed) | A short: they back-feed each other |
| A 5 V board driving a 3.3 V part's signal pin | Flagged: add a level shifter |
| A supply outside the part's voltage range (a servo on 3V3, a 9 V battery on a servo) | Flagged; the part runs at that voltage (a servo is weaker on less) |
| A regulator's input too low for its output (a 4×AA pack at half charge on the Uno's VIN) | Flagged; its output follows the input down |
| More current than a supply is rated for: at rest, or with the motors stalled | Flagged with the numbers; see [Power](#power) |
| A board's supply rail that would fall below its brownout threshold with the motors stalled | Flagged: the board would reset |
| A DC motor wired straight to a board pin | Error: the pin gives a few tens of mA; the motor barely moves and the pin is overloaded. Use a motor driver |
| A driver output (`motor_out`) on another output, a board pin, a supply or ground | A short: fix the wiring |
| A motor that stalls above its driver's current per channel | Flagged: a real driver heats up and shuts down |
| An L298N's ENA/ENB wired to the board with its jumper still fitted | Flagged: the pin fights the jumper; untick the jumper in the Inspector |

## What changes at the Real level

- **Pins**: the sketch uses the board's own pin numbers and names (`9`, `A0`, `LED_BUILTIN`), from the real Arduino core's variant for that board, along with its macros (`ARDUINO_AVR_UNO`, `ESP32`). The sketch is the same file you upload to the real board ([firmware.md](firmware.md#8-move-it-to-a-real-board)).
- **PWM**: `analogWrite()` runs at the board's frequency for that pin; on a pin without PWM it is a plain HIGH/LOW.
- **ADC**: `analogRead()` gives the board's counts (0..1023 on an Uno, 0..4095 on an ESP32).
- **Bus time**: an I2C transaction takes its bit time (9 bits per byte at 100 kHz, or the `Wire.setClock()` value); an SPI transfer 8 bits per byte at its clock. Reading the MPU6050's 14 data bytes takes about 1.5 ms at 100 kHz.
- **Serial**: bytes take their time on the wire at the baud rate, through the board's buffers (see [Several boards](#several-boards)); printing a lot at 9600 baud slows `loop()` down as on a real Uno. The Serial Monitor has its own baud rate, which must match `Serial.begin()`.
- **Servo libraries** keep their own habits at every level: the Uno's `Servo` starts pulsing at 1500 µs on `attach()`; `ESP32Servo` sends nothing until the first `write()`, and its default 10-bit timer gives 19.5 µs steps (about 3.5° on a 1000..2000 µs servo) until `setTimerWidth(16)` is called after `attach()`.

## Motors and drivers

A DC motor at the Real level is a two-terminal part: wire its **MOTOR_A** and **MOTOR_B** to a motor driver's outputs, the driver's inputs to the board's pins, and the driver's motor supply (L298N **VS**, TB6612 **VM**) to a battery or a bench supply, with a common ground (**Components → Drivers**, guide: [create_driver.md](create_driver.md)).

| Driver | Inputs | Speed | Both inputs LOW | Drop |
|---|---|---|---|---|
| **L298N module** | IN1/IN2 (IN3/IN4), ENA/ENB | PWM on ENA with its jumper removed, or on IN1/IN2 with the jumper fitted (default) | brake (both outputs to ground) | about 2.5 V at 1 A: a 7.4 V pack gives the motor less than 5 V |
| **TB6612FNG** | AIN1/AIN2, PWMA (B the same), STBY HIGH | PWM on PWMA | coast (outputs off) | 0.5 Ω on-resistance |

The run solves each motor as a circuit: the driver's output voltages, the drops, the motor's winding resistance and its back-EMF. The motor's current comes from the driver's supply, so a battery sags and drains with the motors. Note that the Prototype level's ideal driver coasts on both LOW, but an L298N with its jumpers fitted brakes: the same sketch stops faster on the real module. To let a motor coast on an L298N, remove the jumper and drive EN LOW.

A motor wired straight to a board pin runs on what the pin can give: the pin is a source of 5 V (or 3.3 V) through its output resistance (`pin_output_ohm`, about 25 Ω on an Uno). A 12 V gearmotor barely turns, and Output warns when a pin carries more than its absolute maximum: `MCU: Uno pin 5 carries 51 mA at 0.1 s (absolute maximum 40 mA): ...`. That current also counts as load on the board's rail.

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

**Simplifications:** the rails are solved with the currents of the step before (a 2 ms lag, like a rail's capacitors); no ripple or transients; a battery's quick voltage rebound in the first seconds after a load stops isn't modelled (only the slower recovery); temperature doesn't change a battery; motors and drivers are averaged over each PWM period (no ripple, no motor inductance).

## Not modelled yet

- Interface boards beyond the level shifter (PCA9685).
- Serial at the bit level: a `digitalRead()` of an RX pin reads the idle HIGH, not the bits going by. Frame formats other than 8N1 are timed as 8N1. `SoftwareSerial` above 57600 baud is only warned about (on a real Uno it starts losing bits).
- A level shifter that loses its supply during the run keeps passing signals (it is checked at Play).
- A real polyfuse's cut-out, a driver's thermal shutdown, and damage (a pin or a part driven past its absolute maximum, a LiPo over-discharged: only a warning).
- A protection circuit's over-current and short-circuit cut-outs (only its under-voltage cut-out is modelled), and charging.
- A guided Prototype → Real migration that maps the ideal wiring onto a board for you.

## Demos

`demo/Servo Comparison (Uno)` (an Uno on the stand, servos on D9 and D10, powered by USB), `demo/Robot arm (ESP32-CAM)` (the ESP32-CAM on the arm's tip, servos on IO13-15, everything fed by a 2S LiPo through an LM2596 at 5 V with a common ground) and `demo/DC Motor (Uno)` (a 12 V gearmotor on a stand, through an L298N on pins 5 and 6, fed by a 2S LiPo that also powers the Uno through VIN) are Real projects. `demo/Robot arm (ESP32-CAM + Uno)` splits the arm between two boards: the ESP32-CAM finds the ball and sends the servo pulses at 57600 baud (`Serial1` on IO14/IO15) through a level shifter to an Uno, which drives the servos (`SoftwareSerial` on D2/D3, servos on D9-D11); each board has its own tab in the Serial Monitor. Their Prototype twins (`demo/Servo Comparison`, `demo/Robot arm`, `demo/DC Motor`) run the same firmware on the ideal board.
