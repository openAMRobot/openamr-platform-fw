// Minimal Arduino.h for host tests of the Teensy Gate A overlay. MIT, OpenAMRobot.
// Provides only what the code under test uses; add a symbol here only when a test needs it.
#ifndef OPENAMR_HOST_ARDUINO_STUB_H
#define OPENAMR_HOST_ARDUINO_STUB_H

#include <cmath>    // pow() in lino_base_config.h
#include <cstdint>

// Same semantics as the Arduino core macro (upstream linorobot2 pid.cpp uses it).
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#endif
