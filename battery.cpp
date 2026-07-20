#include "battery.h"

void BatteryMonitor::feed(int mv, uint32_t nowMs) {
  if (mv < BATT_NO_CELL_MV) {            // no cell (USB-powered bench board): reset to inert
    st = BATT_NORMAL; ema = 0; cutRun = 0; entryPulse = false; usb = false;
    return;
  }
  ema = ema ? (ema * 3 + mv) / 4 : mv;
  // USB presence by voltage ALONE: the ETA6096 status line isn't wired to a GPIO, and the CH343's
  // TX idles high off the 3.3V rail even with USB unplugged (measured 2026-07-16 -- killed the
  // GPIO44 idle-level approach on the bench). The signal that survives: the charger holds/floats
  // the cell at ~4.17-4.26V across units, while the board's own load sags even a full cell below
  // ~4.13V the moment VBUS is gone. Hysteresis keeps a read near the threshold from flapping the
  // stayAwakeUsb idle-timer bump. Known consequence: a drained cell mid-charge reads below the
  // threshold, so a drained unit on a charger still sleeps after sleepMin -- fine, it charges
  // faster asleep; the desk-toy case (held near full) is the one that stays awake.
  if (!usb && ema >= BATT_USB_ON_MV) usb = true;
  else if (usb && ema < BATT_USB_OFF_MV) usb = false;
  if (st == BATT_CUTOFF) return;         // latched: the glue is already powering off
  // Cutoff debounce runs on the RAW sample -- a transient sag is exactly what we're debouncing,
  // and the EMA would double-filter it (and make the 3-sample rule squishy).
  cutRun = (mv < BATT_CUTOFF_MV) ? cutRun + 1 : 0;
  if (cutRun >= BATT_CUTOFF_SAMPLES) { st = BATT_CUTOFF; return; }
  if (st == BATT_NORMAL && ema < BATT_LOW_MV) {
    st = BATT_LOW; entryPulse = true;
  } else if (st == BATT_LOW && ema >= BATT_NORMAL_MV) {
    st = BATT_NORMAL; entryPulse = false;
  }
  (void)nowMs;
}

bool BatteryMonitor::splashDue(uint32_t nowMs) {
  if (st != BATT_LOW) return false;
  if (entryPulse) { entryPulse = false; lastSplashMs = nowMs; return true; }
  if (nowMs - lastSplashMs >= BATT_SPLASH_REPEAT_MS) { lastSplashMs = nowMs; return true; }
  return false;
}
