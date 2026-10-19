# Firmware core

The Arduino core the robot's firmware is compiled against: C++ sketches are compiled to WebAssembly in the app (wasi-sdk clang) and run by the simulator's `FirmwareRuntime` in lockstep with the physics.

- `include/rs_abi.h`: the simulator's imports (time, pins, serial). Everything else is built on these.
- `include/Arduino.h`, `Print.h`, `WString.h`, `Servo.h`: the Arduino API, with the real libraries' behaviour.
- `core/*.cpp`: compiled once into `libarduino.a` (cached), then linked with each sketch.

These files are read from disk by the compiler, so they are **not** in `resources.qrc`.

Rules:
- Keep the API what the real Arduino core and libraries offer, with the same behaviour (e.g. `Servo::attach()` starts pulsing at 1500 µs). Sketches written here must also compile for a real board with `arduino-cli`.
- Firmware reaches only pins: no value the real part doesn't expose.
