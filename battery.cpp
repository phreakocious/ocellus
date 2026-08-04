#include "battery.h"

void BatteryMonitor::feed(int mv, uint32_t nowMs) {
  if (mv < BATT_NO_CELL_MV) {            // no cell (USB-powered bench board): reset to inert
    st = BATT_NORMAL; ema = 0; cutRun = 0; entryPulse = false; usb = false;
    chg = false; prevMv = 0; slopeMs = 0;   // a floating divider jumps hundreds of mV; keep the step detector inert too
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
  // Charge-step detector, DIFFERENTIAL where the latch above is absolute -- it covers the latch's
  // documented blind window (a drained cell mid-charge reads under 4150 for 10-15 min, so the unit
  // said "no USB" exactly while someone plugged it in to configure it). Plugging in slams the cell
  // terminal up by charge-current * internal-R within one 5s sample; unplugging sags it the same;
  // a delta between consecutive samples cancels the per-unit ADC offset that made the absolute
  // thresholds need calibration. Load steps measure far smaller (~150mA mode swing * ~0.15ohm ~=
  // 22mV < 40). Set on the plug step only -- a sustained-slope set was considered and dropped: cell
  // relaxation after leaving a heavy mode also climbs for minutes (boot-while-charging stays blind
  // here; the UART-wake host window covers configuring in that state). Held only while the EMA
  // keeps climbing, so a false step self-heals off within ~2 windows; the absolute latch takes over
  // at the top and resets the detector for the next plug cycle.
  if (prevMv) {
    if (!usb && !chg && mv - prevMv >= BATT_CHG_STEP_MV) chg = true;
    else if ((chg || usb) && prevMv - mv >= BATT_CHG_STEP_MV) { chg = false; usb = false; }   // unplug step:
    // a charger never drops the terminal 40mV in one sample; clearing the LATCH here too frees the
    // unit whose off-USB resting voltage never falls below BATT_USB_OFF_MV (the stuck-"usb yes"
    // battery drain). A false clear on real USB self-heals: the next sample's EMA is still >= ON.
  }
  // One rolling EMA anchor drives both slope rules: chg must keep CLIMBING (a false plug step
  // self-heals), and a usb latch must not be SAGGING (float is flat; sustained sag = unplugged --
  // this is the path that catches a soft unplug from full float, where the taper current is too
  // small to make a 40mV step).
  if (!slopeMs) { slopeMv = ema; slopeMs = nowMs; }
  else if (nowMs - slopeMs >= BATT_CHG_HOLD_MS) {
    int d = ema - slopeMv;
    if (chg && d < BATT_CHG_HOLD_MV) chg = false;
    if (usb && d <= -BATT_SAG_MV) usb = false;
    slopeMv = ema; slopeMs = nowMs;
  }
  if (usb) chg = false;
  prevMv = mv;
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
}

bool BatteryMonitor::splashDue(uint32_t nowMs) {
  if (st != BATT_LOW) return false;
  if (entryPulse) { entryPulse = false; lastSplashMs = nowMs; return true; }
  if (nowMs - lastSplashMs >= BATT_SPLASH_REPEAT_MS) { lastSplashMs = nowMs; return true; }
  return false;
}
