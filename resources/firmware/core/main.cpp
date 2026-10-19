// Entry points the simulator calls. The firmware is a wasm "reactor": the
// simulator first calls _initialize (global constructors, e.g. Servo
// objects), then rs_setup once, then rs_loop over and over, each from its
// lockstep slot (see LockstepGate).
#include "Arduino.h"

HardwareSerial Serial;

extern "C" __attribute__((export_name("rs_setup"))) void rs_setup(void) { setup(); }
extern "C" __attribute__((export_name("rs_loop"))) void rs_loop(void) { loop(); }
