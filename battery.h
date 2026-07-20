#pragma once
#include <stdint.h>

// Battery state machine for the LiPo boards. Pure logic, Arduino-free (host-tested in
// [env:native]) -- main.cpp feeds it readBatteryMv() every ~5s and acts on the state:
//   LOW    -> drowsy eye mood + backlight cap + battery splash (entry + every 5 min)
//   CUTOFF -> final splash + powerOff(), protecting the cell from deep discharge

enum BattState : uint8_t { BATT_NORMAL, BATT_LOW, BATT_CUTOFF };

constexpr int      BATT_NO_CELL_MV       = 3000;   // below this = floating divider / no divider: feature inert
constexpr int      BATT_LOW_MV           = 3500;   // EMA below -> LOW (~15% left at our ~52mA load)
constexpr int      BATT_NORMAL_MV        = 3600;   // EMA back above -> NORMAL (hysteresis)
constexpr int      BATT_CUTOFF_MV        = 3300;   // raw sample below...
constexpr int      BATT_CUTOFF_SAMPLES   = 3;      // ...this many times consecutively (~15s) -> CUTOFF
constexpr uint32_t BATT_SPLASH_REPEAT_MS = 5 * 60 * 1000;
constexpr int      BATT_USB_ON_MV        = 4150;   // EMA at/above -> charger holds/floats the cell: USB present
constexpr int      BATT_USB_OFF_MV       = 4100;   // EMA back below -> unplugged (the board's own load sags even a full cell under this)

struct BatteryMonitor {
  void feed(int mv, uint32_t nowMs);     // one sample; nowMs only anchors the splash cadence
  BattState state() const { return st; }
  bool splashDue(uint32_t nowMs);        // one-shot: true at LOW entry, again every repeat interval while LOW
  bool usbPowered() const { return usb; }   // USB present, by voltage alone (see feed) -- stayAwakeUsb reads this
  int  emaMv() const { return ema; }         // what the USB latch + LOW threshold actually decide on (debug screen)
 private:
  BattState st = BATT_NORMAL;
  int      ema = 0;                      // 0 = unseeded (readBatteryMv already averages 8 ADC reads)
  int      cutRun = 0;                   // consecutive RAW samples under BATT_CUTOFF_MV
  bool     entryPulse = false;
  bool     usb = false;                  // hysteresis-latched ON/OFF threshold pair above
  uint32_t lastSplashMs = 0;
};
