# Firmware examples

One sketch per part, written against the real Arduino/ESP32 libraries' API. Each runs in RoboticsStudio and compiles, unchanged, for a real board (`tools/check_transfer.sh`, CI job `transfer-check`).

| Example | Part (rename it in the Inspector as noted) | Real board |
|---|---|---|
| `dc_motor` | 12 V DC gear motor named `Left` | Uno (`uno/robot_pins.h`) + a DRV8833-style driver |
| `stepper` | NEMA17 named `Stepper` | Uno + an A4988 driver |
| `imu_mpu6050` | MPU6050 (on the I2C bus, no pins to name) | Uno (A4/A5) |
| `tft_display` | 1.8" TFT named `TFT` | Uno |
| `esp32_camera` | OV7670 camera named `Camera` | ESP32-CAM (AI-Thinker) |
| `serial_echo` | none: type into the Serial Monitor (`hello`, `add 2 40`, `blink 3`) | any (`uno/robot_pins.h` is empty) |

These are Prototype sketches: in RoboticsStudio's Prototype level, `robot_pins.h` is generated from the project, and the names come from the parts' names (`Left` → `LEFT_MOTOR_A`). For a real board, the board folder holds a hand-written `robot_pins.h` with the same names on that board's pins: copy it next to the sketch (a Real copy of a project gets the same kind of file to fill in). At the Real level a sketch can also use the board's pin numbers directly.
