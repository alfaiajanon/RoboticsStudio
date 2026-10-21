# Robot firmware (C++)

The robot is programmed like a real microcontroller: an **Arduino sketch** in C++. Pressing **▶ PLAY** compiles it to WebAssembly inside the app and runs it in lockstep with the physics, on the simulator's ideal board. The same file compiles for a real Arduino board, unchanged: only the pin numbers differ.

Every part is reached through its real interface (pins, I2C, SPI, the camera driver); `examples/firmware` has one sketch per part. JavaScript is used only for stage scripts, which move the world around the robot ([stage.md](stage.md)). JavaScript robot scripts were removed: see [section 9](#9-porting-a-javascript-robot-script) to port one.

## 1. Create a sketch

1. In the **Script** view, press **+**, choose **Firmware sketch (.ino)** and give it a name.
2. It becomes the active sketch when the project has none. Otherwise pick it in the Inspector (project selected) → **Firmware**.
3. Press **▶ PLAY**.

The new sketch already uses your robot. It lists the project's pin names, attaches the first servo with that servo's own pulse range, and centres it:

```cpp
#include <Servo.h>
#include "robot_pins.h"

Servo shoulder;

void setup() {
  Serial.begin(115200);
  shoulder.attach(SHOULDER_SIGNAL, 1000, 2000);   // the part's pulse range, in microseconds
}

void loop() {
  shoulder.write(90);   // 0..180 degrees, 90 = centre
  delay(20);
}
```

The first Play compiles the Arduino core too (about a second); after that a sketch compiles in a fraction of a second, and an unchanged sketch starts at once.

## 2. Pins

**At the Real level** ([wiring.md](wiring.md)) the sketch uses the board's own pins, exactly as on the real board: `servo.attach(9)`, `analogRead(A0)`, `digitalWrite(LED_BUILTIN, HIGH)`. Nothing is generated for you. Name them in the sketch as you like (`const int SHOULDER_SIGNAL = 9;`). The board's pin names (`A0`, `LED_BUILTIN`, `SDA`, `MOSI`, ...) and macros (`ARDUINO_AVR_UNO`, `ESP32`) are the real Arduino core's, so `#ifdef ESP32` works as there. What is wired where is on the Wiring page and in the Inspector: select a part for **Pins (wired to Uno)**, or the board for the whole list. Wire a part to D9 and write to pin 10, and nothing happens, as on the bench.

The ESP32-CAM's names are the real ESP32 core's generic variant. That variant was written for a dev board, so on an ESP32-CAM `SDA`/`SCL` (21/22) and most `A`/`T` names are camera or flash pins. ESP32-CAM sketches use GPIO numbers (`Wire.begin(14, 15)`, the usual `PWDN_GPIO_NUM 32` ... list for the camera), and so do the demos.

**At the Prototype level** every part is wired automatically to an ideal controller with as many pins as needed. There are no real pins, so the generated `robot_pins.h` names them, and is regenerated on every Play:

```cpp
// Shoulder (servo_mg996r, uid 6)
constexpr int COMP_6_SIGNAL = 601;   // pwm
constexpr int SHOULDER_SIGNAL = COMP_6_SIGNAL;
```

- Every typed signal pin of every part gets `COMP_<uid>_<PIN>`. The number is the part's uid × 100 + the pin's index in its part file, so it stays the same while the part exists.
- When a part's name is unique and a valid C++ name, it also gets a readable alias (`SHOULDER_SIGNAL`). Rename the part in the Inspector to change it.
- Select a part to see its pins in the Inspector, under **Pins (ideal board)**.
- Only pins whose part file gives them a type (`pwm`, `digital`, `analog`) are on the board: a part with untyped pins can't be reached from firmware. See [component_basics.md](component_basics.md#pins).
- The ideal board has no `A0` or `LED_BUILTIN`: use the names.

**From Prototype to Real** (one way): **Create Real copy...** in the project settings makes a new Real project next to this one, with the same robot and sketches. The Prototype project stays as it is. The copy gets a `robot_pins.h` of its own, **yours to edit** (never regenerated), with the same names set to `-1` ("not wired yet"). Add a board, wire the parts, then put each board pin there (`constexpr int COMP_6_SIGNAL = 9;`), and the sketch builds unchanged. Or replace the names with the pin numbers in the sketch and delete the file.

## 3. Sketch structure

- `setup()` runs once, then `loop()` over and over, as on an Arduino.
- A `.ino` file gets `#include <Arduino.h>` added automatically, as the Arduino IDE does. Include everything else yourself: `#include <Servo.h>`, and at the Prototype level `#include "robot_pins.h"`. Headers you put next to the sketch (`#include "pins.h"`) are found, as in an Arduino sketch folder.
- **Declare a function before you use it.** The Arduino IDE generates these declarations for `.ino` files; this version does not yet.
- A `.cpp` robot script works too (PlatformIO style). It gets nothing added: write `#include <Arduino.h>` at the top.
- The whole firmware is one file.

## 4. What you can use

Everything runs on **simulated time**. Your code itself takes no simulated time, except a fixed cost per `millis()`/`micros()` call (4 µs) and per `loop()` (10 µs), so busy-waits like `while (millis() - t < 100) {}` work. Code that never calls anything (`while (true) {}`) is stopped after 2 seconds.

| Area | Available | Notes |
|---|---|---|
| Time | `millis`, `micros`, `delay`, `delayMicroseconds`, `yield` | `millis`/`micros` wrap at 2³² like on a board |
| Pins | `pinMode`, `digitalWrite`, `digitalRead`, `analogWrite`, `analogRead` | `analogWrite(pin, 1..254)` sends a pulse train (a servo on that pin reads its width, as a real one does): 490 Hz on the ideal board, the board's frequency at the Real level, where a pin without PWM gets HIGH from 128, LOW below; 0 and 255 are steady levels. `analogRead` gives the board's counts (0..1023; 0..4095 on an ESP32; 0 on a pin without an ADC). Inputs: nothing drives a board input yet, so `digitalRead` gives the pull-up (with `INPUT_PULLUP`) or LOW, and `analogRead` gives 0 |
| Servo | `attach`, `write`, `writeMicroseconds`, `read`, `readMicroseconds`, `attached`, `detach`; `ESP32Servo.h` adds `setTimerWidth`, `setPeriodHertz`, `writeTicks`, `release` | real library behaviour, see below |
| I2C | `Wire`: `begin`, `begin(sda, scl)`, `setClock`, `beginTransmission`, `write`, `endTransmission`, `requestFrom`, `available`, `read` | the board's shared I2C bus; parts answer at their address. 32-byte buffers, as on an Uno. No time per transfer at the Prototype level; at the Real level 9 bits per byte at the bus clock (100 kHz unless `setClock`) |
| SPI | `SPI`: `begin`, `beginTransaction`, `transfer`, `transfer16`, `transfer(buf, n)`, `setClockDivider` | the board's shared SPI bus; parts listen while their CS pin is LOW (drive it with `digitalWrite`). Nothing answers on MISO yet. At the Real level 8 bits per byte at the transaction's clock (4 MHz by default), capped at the board's maximum |
| Display | `Adafruit_ST7735` + `Adafruit_GFX` (subset): `initR`, `setRotation`, `fillScreen`, `drawPixel`, lines, rectangles, circles, bitmaps, text | a compatible subset of Adafruit's libraries, see [create_display.md](create_display.md#firmware-an-st7735-on-spi) |
| Camera | `esp_camera.h`: `esp_camera_init`, `esp_camera_fb_get`, `esp_camera_fb_return` | a compatible subset of the ESP32 camera driver, see [create_camera.md](create_camera.md#firmware-esp32-camera) |
| Serial | `begin`, `print`, `println`, `printf`, `available`, `read` | output appears in the **Output** panel, one entry per line, tagged `[MCU]`; nothing comes in yet (`available()` is 0). `Serial.printf` exists on ESP32 boards, not on an Uno |
| Text | `String`, `F("...")`, `PROGMEM`, `pgm_read_*` | a common subset of `String` |
| Maths | `min`, `max`, `constrain`, `map`, `sq`, `radians`, `degrees`, `PI`…, `<math.h>`, `<cmath>`, `<algorithm>` | Arduino's helpers are templates, so the standard headers still work |
| Random | `random`, `randomSeed` | repeats exactly run to run, even after `randomSeed(analogRead(...))` |
| C library | `snprintf`, `strlen`, `malloc`… | `printf` (plain C) appears in Output tagged `[MCU][stdout]`, on a real Uno it prints nothing; `time()` is simulated seconds; `clock()` is not available; there are no files |

**Servo, as the real library behaves** (so the code and its surprises transfer):

- `attach()` starts sending pulses at once, at 1500 µs: the servo jumps to its centre when attached.
- `write(value)`: below 544 the value is an angle **0..180, with 90 as the centre**, mapped onto the attach range; from 544 up it is microseconds.
- The library's default range is 544..2400 µs, wider than most hobby servos are specified for (an SG90 takes about 1000..2000 µs). Pass the part's range: `attach(pin, 1000, 2000)`.
- `read()` returns the last angle you wrote, not the servo's position: a hobby servo has no feedback wire.
- `detach()` stops the pulses and the servo goes limp.

**ESP32Servo** (`#include <ESP32Servo.h>`, for ESP32 boards) behaves like the real ESP32Servo library, which differs from the Uno's:

- `attach()` sends nothing: the servo stays limp until the first `write()`.
- Pulses are whole timer ticks, rounded down. The default 10-bit timer at 50 Hz has 19.5 µs ticks: `write(90)` on a 1000..2000 µs range gives 1484 µs, and an MG996R moves in ~3.5° steps. Call `setTimerWidth(16)` **after** `attach()` (attach resets it) for 0.3 µs ticks; the pulses then restart with the next `write()`.
- The attach range is clamped to 500..2500 µs; `write()` takes angles below 500.

**Every part, from firmware:**

| Part | How the sketch reaches it | Example |
|---|---|---|
| Servo | `Servo.attach(SHOULDER_SIGNAL, 1000, 2000)` | `demo/Servo Comparison` |
| DC gear motor | two driver pins: `analogWrite(LEFT_MOTOR_A, duty)`, the other LOW (both HIGH brakes) | `examples/firmware/dc_motor` |
| Stepper | STEP pulses and DIR with `digitalWrite` | `examples/firmware/stepper` |
| IMU (MPU6050) | `Wire`, registers at 0x68 (wake it first) | `examples/firmware/imu_mpu6050` |
| TFT display | `Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST)` | `examples/firmware/tft_display` |
| Camera | `esp_camera_init` with `config.pin_sccb_sda = CAMERA_SDA` | `examples/firmware/esp32_camera` |

The part guides ([create_dc_motor.md](create_dc_motor.md), [create_stepper.md](create_stepper.md), [create_imu.md](create_imu.md), [create_display.md](create_display.md), [create_camera.md](create_camera.md)) say what each part models. None of them has feedback the real part lacks: no motor speed, no stepper position, and a servo's `read()` is the last command.

## 5. Not available yet

- No `attachInterrupt`, `EEPROM` or `tone` yet; no I2C target (slave) mode; nothing answers on SPI MISO.
- The IMU's `INT` and `AD0` pins aren't modelled (set the address with the part's `ad0_high` parameter).
- Power is ideal at both levels for now (no supply sag or brownout); at the Real level a part without a supply or ground wire is dead. Buses take time at the Real level only.
- The processor's speed is not modelled: firmware code runs instantly in simulated time (apart from the small fixed costs above).

## 6. Compile errors and warnings

When the sketch does not compile, Play does not start. The Script view opens on the sketch, the lines with errors get a red dot (warnings: amber), and the cursor jumps to the first error. Hover a dot to read the message; all messages are also in the **Output** panel, as `MySketch.ino:12:21: expected ';' after expression`. Warnings do not stop the run.

A sketch without `setup()` or `loop()` fails to link with "the sketch must define setup() and loop()".

Builds are cached in `~/.cache/Anon Engineering/Robotics Studio/firmware` (the last 20 sketches). Deleting that folder is always safe.

## 7. Crashes and the watchdog

If the firmware does something invalid (for example reading memory through a bad pointer), it stops and the Output panel shows why, with the functions it was in:

```
MCU: firmware stopped in loop(): Exception: out of bounds memory access
MCU:   #00: 0x000e - loop
MCU:   #01: 0x000c - rs_loop
```

The physics keeps running; press **⬛ STOP** and **▶ PLAY** to start again. A `loop()` that never returns or calls `delay()` is stopped after 2 seconds of real time.

**Brownout** (Real level): when the board's supply rail sags below its brownout threshold (a stalled servo on the same 5 V, a weak battery), the board resets like a real one: the sketch stops, its pins let go, and once the rail recovers it starts again from scratch (global objects constructed again, then `setup()`, `millis()` from 0). Output shows `MCU: <board> brownout reset at ...`. See [wiring.md](wiring.md#power).

## 8. Move it to a real board

The sketch is ordinary Arduino code. To run it on a real Uno:

1. Make an Arduino sketch folder: the folder and the `.ino` share a name (letters, digits, underscores), e.g. `Sweep/Sweep.ino`. Copy any header the sketch includes from its project folder (a Real copy's `robot_pins.h`) next to it.
2. A Real project's sketch already uses the board's pins: nothing else to do. A Prototype sketch needs a `robot_pins.h` with its names on the board's pins: make a Real copy of the project (section 2), or write it by hand, as the examples do (`examples/firmware/dc_motor/uno/robot_pins.h`):
   ```cpp
   constexpr int COMP_2_MOTOR_A = 5;   // pwm
   constexpr int LEFT_MOTOR_A = COMP_2_MOTOR_A;
   ```
3. Build and upload it with the Arduino IDE, or with `arduino-cli`:
   ```bash
   arduino-cli core install arduino:avr
   arduino-cli lib install Servo
   arduino-cli compile --fqbn arduino:avr:uno Sweep
   arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 Sweep
   ```

Every demo and example is checked this way automatically (`tools/check_transfer.sh`). It compiles each Real demo's board sketch for its board (`arduino:avr:uno`, `esp32:esp32:esp32cam`), and each Prototype sketch that has a board folder (`uno/`, `esp32cam/`) with that folder's `robot_pins.h`. The demos come in pairs: a Prototype copy on the ideal board (`demo/Robot arm`, `demo/Servo Comparison`) and a Real copy with its board wired in the simulator. The Real copies are `demo/Robot arm (ESP32-CAM)`, which uses `ESP32Servo.h`, GPIO numbers and the board's own camera, and `demo/Servo Comparison (Uno)`, on pins 9 and 10. See [wiring.md](wiring.md).

## 9. Porting a JavaScript robot script

Older projects programmed the robot in JavaScript, calling each part as `comp_<uid>`. Those scripts no longer run: the project still opens, the script shows a red **!** in the Script view, and on Play the robot stays idle. Port it to a sketch:

1. Create a firmware sketch (**+** → **Firmware sketch**) and make it the active one (Inspector → **Firmware**). Its header comment lists your parts' pin names.
2. Give the parts readable names in the Inspector (`Shoulder` instead of `MG996R`) so their pins get aliases like `SHOULDER_SIGNAL`.
3. Move the code over: `function loop()` becomes `void loop()`, `let` becomes a C++ type, objects become `struct`s. Declare a function before you use it.
4. Replace each part call with the part's real interface:

| JavaScript | Firmware |
|---|---|
| `console.log(...)` | `Serial.println(...)`, or `Serial.printf(...)` (ESP32 only on a real board) |
| `delay`, `delayMicroseconds`, `millis`, `micros` | the same |
| servo `write(deg)`, −90..90, 0 = centre | `Servo s; s.attach(PIN, 1000, 2000);` then `s.write(deg + 90)` (0..180, whole degrees), or `s.writeMicroseconds(1500 + deg * 1000 / 180)` for finer steps |
| servo `writeMicroseconds(us)` | `s.writeMicroseconds(us)` |
| DC motor `set_pwm(v)`, −255..255 | `v > 0`: `analogWrite(MOTOR_A, v); digitalWrite(MOTOR_B, LOW);`, `v < 0`: the other way round, 0: both LOW (coast) |
| DC motor `brake()` | both pins HIGH |
| stepper `set_rpm(rpm)`, `set_velocity(rad_s)`, `stop()` | set `DIR`, then pulse `STEP` yourself: one rising edge per (micro)step, at `rpm / 60 × steps per turn` per second (`examples/firmware/stepper`). Count the steps if you need the position |
| IMU `getAcceleration()` (m/s²), `getRotation()` (°/s) | `Wire` at 0x68: wake it (register 0x6B = 0), read 14 bytes from 0x3B. At the default ranges, 16384 counts = 1 g and 131 counts = 1 °/s (`examples/firmware/imu_mpu6050`) |
| camera `available()`, `capture()` | `esp_camera_init()` once, then `camera_fb_t* fb = esp_camera_fb_get();` … `esp_camera_fb_return(fb);`. `fb_get` waits for the next frame, so ask for one only when it is due. Pixels are RGB565 or grayscale in `fb->buf`; `fb->timestamp` is the capture time |
| camera `getPixel`, `regionMean`, `toGray` | compute them from `fb->buf` |
| display `clear()`, `fillRect`, `drawLine`, `drawCircle`, `drawText`, … | `Adafruit_ST7735`: `fillScreen`, `fillRect`, `drawLine`, `drawCircle`, `setCursor` + `print`. Colours are RGB565 (`tft.color565(r, g, b)`). There is no `display()`: drawing shows as it happens |
| display `drawGray`, `drawRGB` | `drawGrayscaleBitmap`, `drawRGBBitmap` (RGB565) |

The firmware sees what the real part offers, as the JavaScript API did: no servo position, no motor speed, no stepper position.

The Robot arm demo was ported this way: `demo/Robot arm/Robot arm.ino` is a line-by-line port of the old JavaScript, with the same results.
