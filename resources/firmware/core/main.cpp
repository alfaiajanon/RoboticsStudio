// Entry points the simulator calls. The firmware is a wasm "reactor": the
// simulator first calls _initialize (global constructors, e.g. Servo
// objects), then rs_setup once, then rs_loop over and over, each from its
// lockstep slot (see LockstepGate).
#include "Arduino.h"

// The serial ports; Serial1/Serial2 are declared only for ESP32 boards (Stream.h).
HardwareSerial Serial(0);
HardwareSerial Serial1(1);
HardwareSerial Serial2(2);

extern "C" __attribute__((export_name("rs_setup"))) void rs_setup(void) { setup(); }
extern "C" __attribute__((export_name("rs_loop"))) void rs_loop(void) { loop(); }
