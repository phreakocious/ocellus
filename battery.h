#pragma once
#include <stdint.h>

// Battery state machine for the LiPo boards. Pure logic, Arduino-free (host-tested in
// [env:native]) -- main.cpp feeds it readBatteryMv() every ~5s and acts on the state:
//   LOW    -> drowsy eye mood + backlight cap + battery splash (entry + every 5 min)
//   CUTOFF -> final splash + powerOff(), protecting the cell from deep discharge
// Spec: docs/superpowers/specs/2026-07-16-low-battery-ui-design.md

enum BattState : uint8_t { BATT_NORMAL, BATT_LOW, BATT_CUTOFF };

constexpr int      BATT_NO_CELL_MV       = 3000;   // below this = floating divider / no divider: feature inert
constexpr int      BATT_LOW_MV           = 3500;   // EMA below -> LOW (~15% left at our ~52mA load)
constexpr int      BATT_NORMAL_MV        = 3600;   // EMA back above -> NORMAL (hysteresis)
constexpr int      BATT_CUTOFF_MV        = 3300;   // raw sample below...
constexpr int      BATT_CUTOFF_SAMPLES   = 3;      // ...this many times consecutively (~15s) -> CUTOFF
constexpr uint32_t BATT_SPLASH_REPEAT_MS = 5 * 60 * 1000;
constexpr int      BATT_USB_ON_MV        = 4150;   // EMA at/above -> charger holds/floats the cell: USB present
constexpr int      BATT_USB_OFF_MV       = 4100;   // EMA back below -> unplugged (the board's own load sags even a full cell under this)
constexpr int      BATT_CHG_STEP_MV      = 40;     // plug/unplug I*R step between consecutive ~5s RAW samples (differential:
                                                   // per-unit ADC offset cancels, unlike the absolute latch above)
constexpr int      BATT_CHG_HOLD_MV      = 5;      // EMA must climb this much per hold window or the chg flag self-heals off
constexpr uint32_t BATT_CHG_HOLD_MS      = 60000;
constexpr int      BATT_SAG_MV           = 15;     // EMA falling this much per window while usb-latched = unplugged: a
                                                   // charger holds the cell FLAT (measured +/-9mV raw noise), so a
                                                   // sustained sag can only mean VBUS is gone -- catches the unit whose
                                                   // full cell rests INSIDE the hysteresis band off-USB (measured 4125)

struct BatteryMonitor {
  void feed(int mv, uint32_t nowMs);     // one sample; nowMs only anchors the splash cadence
  BattState state() const { return st; }
  bool splashDue(uint32_t nowMs);        // one-shot: true at LOW entry, again every repeat interval while LOW
  bool usbPowered() const { return usb || chg; }   // USB present: absolute latch OR the charge-step detector (see feed)
  bool charging() const { return chg; }      // which half of usbPowered fired (debug screen / bat reply)
  int  emaMv() const { return ema; }         // what the USB latch + LOW threshold actually decide on (debug screen)
 private:
  BattState st = BATT_NORMAL;
  int      ema = 0;                      // 0 = unseeded (readBatteryMv already averages 8 ADC reads)
  int      cutRun = 0;                   // consecutive RAW samples under BATT_CUTOFF_MV
  bool     entryPulse = false;
  bool     usb = false;                  // hysteresis-latched ON/OFF threshold pair above
  bool     chg = false;                  // charge-step detector: covers the latch's drained-cell blind window
  int      prevMv = 0;                   // last RAW sample (0 = none yet), for the step delta
  int      slopeMv = 0;                  // EMA at the start of the current hold window
  uint32_t slopeMs = 0;
  uint32_t lastSplashMs = 0;
};
