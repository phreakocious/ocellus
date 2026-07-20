#pragma once
#include <stdint.h>

// CST816S capacitive touch on the Waveshare ESP32-S3-Touch-LCD-1.28. Shares the I2C bus
// (SDA=6/SCL=7) that imuBegin() brings up. The chip does gesture recognition itself; we only
// read its result -- no gesture recognizer on the MCU, no library.

enum TouchGesture { TOUCH_NONE, TOUCH_TAP, TOUCH_SWIPE_LEFT, TOUCH_SWIPE_RIGHT };

// Scoped to the Waveshare board ON PURPOSE, not declared unconditionally: GPIO5 is ENC_A on the
// S3-Zero. While this existed for every board, any ext1/touch code keyed on it would have fired on a
// knob turn. Now the symbol simply doesn't exist off-Waveshare, so that mistake won't compile.
#if defined(BOARD_WAVESHARE_128)
constexpr int TOUCH_INT_PIN = 5;     // CST816S data-ready INT (active-low, open-drain). RTC-capable on the S3 -> doubles as a deep-sleep wake source (see powerOff()).
void touchNoteInt();                 // set the INT latch from outside: loop() calls this after a GPIO-caused light-sleep wake, because edge ISRs don't fire across a nap
#endif

extern bool touchPresent;            // false if 0x15 didn't answer -> touchPoll() no-ops

bool touchBegin();                   // release RST high, probe 0x15; true if found
TouchGesture touchPoll();            // one gesture per finger-down; TOUCH_NONE when idle
