#pragma once
#include <stdint.h>

// CST816S capacitive touch on the Waveshare ESP32-S3-Touch-LCD-1.28. Shares the I2C bus
// (SDA=6/SCL=7) that imuBegin() brings up. The chip does gesture recognition itself; we only
// read its result -- no gesture recognizer on the MCU, no library.

enum TouchGesture { TOUCH_NONE, TOUCH_TAP, TOUCH_SWIPE_LEFT, TOUCH_SWIPE_RIGHT,
                    TOUCH_SWIPE_UP, TOUCH_SWIPE_DOWN };

constexpr int TOUCH_MAX = 239;       // panel is 240x240; coordinates are 0..239

// The display auto-flips 180 degrees from gravity (loop() -> setPanelRotation(imuRotation()),
// which returns 0 or 2), but the CST816S reports PANEL-native coordinates and gestures, which
// do not flip. Every consumer therefore has to undo it, and until 2026-08-01 only treatcat did
// -- so swipe left/right ran backwards at rotation 2. Normalizing inside touchPoll() means
// everything downstream speaks screen space, which is what each consumer actually wanted.
inline void touchNormalize(int& x, int& y, uint8_t rot) {
  if (rot != 2) return;
  x = TOUCH_MAX - x;
  y = TOUCH_MAX - y;
}

inline TouchGesture touchRotateGesture(TouchGesture g, uint8_t rot) {
  if (rot != 2) return g;
  switch (g) {
    case TOUCH_SWIPE_LEFT:  return TOUCH_SWIPE_RIGHT;
    case TOUCH_SWIPE_RIGHT: return TOUCH_SWIPE_LEFT;
    case TOUCH_SWIPE_UP:    return TOUCH_SWIPE_DOWN;
    case TOUCH_SWIPE_DOWN:  return TOUCH_SWIPE_UP;
    default:                return g;   // TAP and NONE have no handedness
  }
}

// Rotation currently applied to the panel (0 or 2), written by setPanelRotation() in main.cpp.
// Declared here because it is a property of the display that every touch consumer needs; it
// used to live in treatcat.h, which was the wrong home for it.
extern uint8_t gAppliedRot;

// Apply one raw CST816S position sample. Returns false and leaves x/y ALONE when any of the
// four axis-byte reads failed (rd() gives -1) -- the retained point is already screen space, so
// re-normalizing it would flip it back to panel space and mirror the touch at rotation 2.
// All-four-or-nothing on purpose: a fresh x paired with a stale y is a coordinate from two
// different samples. Lives here, not in touchPoll(), so it can be host-tested -- touchPoll()
// itself is compiled out off the Waveshare board.
inline bool touchApplySample(int xh, int xl, int yh, int yl, uint8_t rot, int& x, int& y) {
  if (xh < 0 || xl < 0 || yh < 0 || yl < 0) return false;
  int nx = ((xh & 0x0F) << 8) | xl, ny = ((yh & 0x0F) << 8) | yl;
  touchNormalize(nx, ny, rot);
  x = nx; y = ny;
  return true;
}

// Scoped to the Waveshare board ON PURPOSE, not declared unconditionally: GPIO5 is ENC_A on the
// S3-Zero. While this existed for every board, any ext1/touch code keyed on it would have fired on a
// knob turn. Now the symbol simply doesn't exist off-Waveshare, so that mistake won't compile.
#if defined(BOARD_WAVESHARE_128)
constexpr int TOUCH_INT_PIN = 5;     // CST816S data-ready INT (active-low, open-drain). RTC-capable on the S3 -> doubles as a deep-sleep wake source (see powerOff()).
void touchNoteInt();                 // set the INT latch from outside: loop() calls this after a GPIO-caused light-sleep wake, because edge ISRs don't fire across a nap
#endif

extern bool touchPresent;            // false if 0x15 didn't answer -> touchPoll() no-ops
extern int touchLastX, touchLastY;   // last touch position (screen-space, 0-239; touchPoll() normalizes via touchNormalize()); valid when touchPoll() returns TOUCH_TAP

bool touchBegin();                   // release RST high, probe 0x15; true if found
TouchGesture touchPoll();            // one gesture per finger-down; TOUCH_NONE when idle

// Live finger state, published as ONE packed word. The carousel reads this from loop() while
// touchPoll() writes it from buttonReadTask -- two tasks on two cores. Three separate fields
// would let a reader pair a fresh `down` with a stale x from the PREVIOUS touch, which shows up
// as a garbage first drag delta and a visible jump. A single aligned 32-bit store cannot tear.
// Same idiom as gTreatTap.
//   bit 31   : finger down
//   bits 23..12 : x   bits 11..0 : y   (screen space, already normalized)
extern volatile uint32_t gTouchSnap;

inline uint32_t touchSnapPack(bool down, int x, int y) {
  if (x < 0) x = 0; if (x > TOUCH_MAX) x = TOUCH_MAX;
  if (y < 0) y = 0; if (y > TOUCH_MAX) y = TOUCH_MAX;
  return (down ? 0x80000000u : 0u) | ((uint32_t)x << 12) | (uint32_t)y;
}
inline bool touchSnapDown(uint32_t s) { return (s & 0x80000000u) != 0; }
inline int  touchSnapX(uint32_t s)    { return (int)((s >> 12) & 0xFFF); }
inline int  touchSnapY(uint32_t s)    { return (int)(s & 0xFFF); }
