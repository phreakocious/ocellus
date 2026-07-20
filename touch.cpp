#include <Arduino.h>
#include <Wire.h>
#include "touch.h"

bool touchPresent = false;

#if !defined(BOARD_WAVESHARE_128)
// No CST816S off the Waveshare board, and the whole implementation is compiled out rather than merely
// short-circuited at runtime: TOUCH_INT_PIN is GPIO5, which is ENC_A on the S3-Zero and a display pin
// elsewhere. Keeping the body out of the build is what makes it impossible to poke those pins by accident.
bool touchBegin() { return false; }
TouchGesture touchPoll() { return TOUCH_NONE; }

#else
// CST816S, I2C addr 0x15 on the shared SDA=6/SCL=7 bus (already begun by imuBegin()).
// Gesture register 0x01: 0x03=swipe left, 0x04=swipe right, 0x05=single tap. Finger-count reg 0x02.
// RST is driven high at boot so the controller leaves reset and answers -- pin from the schematic.
#include "driver/gpio.h"
#include "esp_sleep.h"

#define TOUCH_SDA  6
#define TOUCH_SCL  7
#define TOUCH_RST  13          // confirmed working (chip answers, gestures fire)
#define TOUCH_INT  TOUCH_INT_PIN   // active-low data-ready (pin lives in touch.h; shared with the sleep-wake path)
#define TOUCH_ADDR 0x15

// INT is a short low pulse per data report, not a level held for the touch, so an instantaneous
// digitalRead in touchPoll() misses any pulse that lands between 10ms polls -- and ALL pulses that
// land inside the frame-gap light-sleep nap, which masks GPIO interrupts (the same failure that ate
// the S3-Zero's encoder detents). The ISR latches pulses seen while awake; touchNoteInt() latches
// the asleep case from loop() after a GPIO-caused wake, where the edge ISR can't have fired.
static volatile bool g_intLatch = false;
static void IRAM_ATTR touchIsr() { g_intLatch = true; }
void touchNoteInt() { g_intLatch = true; }

static int rd(uint8_t reg) {
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)TOUCH_ADDR, 1) != 1) return -1;
  return Wire.read();
}

bool touchBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);   // idempotent -- imuBegin() usually ran first
  Wire.setClock(400000);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(50);   // CST816S needs a moment out of reset before it answers
  pinMode(TOUCH_INT, INPUT_PULLUP);           // INT is active-low/open-drain: pull-up holds it high when idle
  touchPresent = (rd(0x01) >= 0);             // a successful read == the chip ACKs at 0x15
  if (touchPresent) {
    attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touchIsr, FALLING);
    // Arm INT as a light-sleep wake source (the CLAUDE.md-prescribed fix for edge inputs on a
    // battery board): a touch mid-nap now ends the nap instead of vanishing. Level-triggered on a
    // pin that idles HIGH -> no spurious wakes, no sleep-current cost. Light-sleep-only source;
    // powerOff()'s ext1 deep-sleep arming is untouched.
    gpio_wakeup_enable((gpio_num_t)TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  return touchPresent;
}

// Capture the best gesture seen across one finger-down and emit it on finger-up. Emitting on
// release (not on the first in-touch sample) is what makes swipes reliable: the gesture reg is
// 0/stale at touch-down and only resolves as the finger moves. A swipe wins over an early tap flag.
TouchGesture touchPoll() {
  if (!touchPresent) return TOUCH_NONE;
  static bool down = false;
  static uint8_t captured = 0;
  // Idle short-circuit: when no touch is in progress and INT is de-asserted (HIGH), the CST816S has no
  // new data and NAKs I2C reads -- reading anyway floods Wire with logged errors and starves the render
  // loop. Only touch the bus once INT asserts -- live level OR the ISR/wake latch, because the level
  // alone missed pulses that fell between 10ms polls (see the latch comment above). Once `down`, keep
  // polling regardless of INT so the emit-on-release logic still catches finger-up even if INT
  // de-asserts first.
  if (!down) {
    if (!g_intLatch && digitalRead(TOUCH_INT) == HIGH) return TOUCH_NONE;
    g_intLatch = false;
  }
  int fingers = rd(0x02);
  if (fingers < 0) return TOUCH_NONE;   // I2C read error: hold state, don't mistake it for finger-up (would double-fire a gesture)
  if (fingers > 0) {
    down = true;
    int g = rd(0x01);
    if (g == 0x03 || g == 0x04) captured = (uint8_t)g;    // a swipe wins over an early tap flag
    else if (g == 0x05 && captured == 0) captured = 0x05; // tap only if no swipe seen yet
    return TOUCH_NONE;                                     // decide on finger-up, not mid-touch
  }
  if (!down) return TOUCH_NONE;
  down = false;
  uint8_t c = captured; captured = 0;
  if (c == 0x03) return TOUCH_SWIPE_LEFT;
  if (c == 0x04) return TOUCH_SWIPE_RIGHT;
  if (c == 0x05) return TOUCH_TAP;
  return TOUCH_NONE;
}
#endif
