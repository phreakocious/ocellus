#include <unity.h>
#include "../../battery.h"

void setUp() {}
void tearDown() {}

void test_no_cell_is_inert() {
  BatteryMonitor b;
  b.feed(0, 0);      TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());
  b.feed(2900, 5000);TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());
  TEST_ASSERT_FALSE(b.splashDue(5000));
}

void test_unplugging_cell_recovers_from_low() {
  BatteryMonitor b;
  b.feed(3400, 0);   TEST_ASSERT_EQUAL(BATT_LOW, b.state());
  b.feed(0, 5000);   TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());  // USB-powered, cell gone
}

void test_hysteresis_low_then_recover_then_low() {
  BatteryMonitor b;
  b.feed(3450, 0);                        // seeds EMA at 3450 -> LOW
  TEST_ASSERT_EQUAL(BATT_LOW, b.state());
  for (int i = 1; i <= 10; i++) b.feed(4000, i * 5000u);   // charger: EMA walks up past 3600
  TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());
  b.feed(3550, 60000);                    // sag below 3600 but above 3500: stays NORMAL (hysteresis)
  TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());
  for (int i = 0; i <= 10; i++) b.feed(3400, 70000u + i * 5000u);  // EMA back under 3500
  TEST_ASSERT_EQUAL(BATT_LOW, b.state());
}

void test_splash_on_entry_then_cadence() {
  BatteryMonitor b;
  b.feed(3400, 1000);
  TEST_ASSERT_TRUE (b.splashDue(1000));    // entry pulse
  TEST_ASSERT_FALSE(b.splashDue(1001));    // one-shot
  TEST_ASSERT_FALSE(b.splashDue(1000 + BATT_SPLASH_REPEAT_MS - 1));
  TEST_ASSERT_TRUE (b.splashDue(1000 + BATT_SPLASH_REPEAT_MS));   // 5 min repeat
  TEST_ASSERT_FALSE(b.splashDue(1001 + BATT_SPLASH_REPEAT_MS));
}

void test_low_reentry_rearms_entry_splash() {
  BatteryMonitor b;
  b.feed(3400, 0);
  TEST_ASSERT_TRUE(b.splashDue(0));
  for (int i = 1; i <= 10; i++) b.feed(4000, i * 5000u);          // back to NORMAL
  TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());
  for (int i = 0; i <= 10; i++) b.feed(3400, 100000u + i * 5000u); // LOW again
  TEST_ASSERT_EQUAL(BATT_LOW, b.state());
  TEST_ASSERT_TRUE(b.splashDue(200000));   // fresh entry pulse, no 5-min wait
}

void test_cutoff_needs_three_consecutive_raw_samples() {
  BatteryMonitor b;
  b.feed(3100, 0);     b.feed(3100, 5000);          // two below 3300
  TEST_ASSERT_NOT_EQUAL(BATT_CUTOFF, b.state());
  b.feed(3400, 10000);                              // raw recovery resets the run
  b.feed(3100, 15000); b.feed(3100, 20000);
  TEST_ASSERT_NOT_EQUAL(BATT_CUTOFF, b.state());
  b.feed(3100, 25000);                              // third consecutive
  TEST_ASSERT_EQUAL(BATT_CUTOFF, b.state());
}

void test_cutoff_latches() {
  BatteryMonitor b;
  for (int i = 0; i < 3; i++) b.feed(3100, i * 5000u);
  TEST_ASSERT_EQUAL(BATT_CUTOFF, b.state());
  b.feed(4200, 20000);                              // even a charger reading doesn't un-latch
  TEST_ASSERT_EQUAL(BATT_CUTOFF, b.state());
  TEST_ASSERT_FALSE(b.splashDue(20000));            // no LOW splashes from CUTOFF
}

void test_no_cell_resets_cutoff_latch() {
  BatteryMonitor b;
  for (int i = 0; i < 3; i++) b.feed(3100, i * 5000u);
  TEST_ASSERT_EQUAL(BATT_CUTOFF, b.state());
  b.feed(0, 20000);                           // cell unplugged (or a divider-less bench board)
  TEST_ASSERT_EQUAL(BATT_NORMAL, b.state());  // the ONLY way out of the latch
  b.feed(3400, 25000);                        // and a later LOW re-entry re-arms the entry splash
  TEST_ASSERT_EQUAL(BATT_LOW, b.state());
  TEST_ASSERT_TRUE(b.splashDue(25000));
}

void test_usb_detect_by_charge_voltage_with_hysteresis() {
  BatteryMonitor b;
  TEST_ASSERT_FALSE(b.usbPowered());                 // fresh: unknown = off
  b.feed(4200, 0);                                   // charger-held cell seeds EMA above ON
  TEST_ASSERT_TRUE(b.usbPowered());
  // Gentle sag into the hysteresis band (steps < BATT_CHG_STEP_MV, inside one sag window): stays on.
  b.feed(4170, 5000); b.feed(4140, 10000); b.feed(4120, 15000);
  TEST_ASSERT_TRUE(b.usbPowered());
  for (int i = 0; i < 10; i++) b.feed(4000, 20000u + i * 5000u);   // unplugged: -120 step + EMA under OFF
  TEST_ASSERT_FALSE(b.usbPowered());
  // Approach the band in sub-BATT_CHG_STEP_MV moves: a >=40mV jump is now (correctly) read as a
  // plug-in by the charge-step detector, and this test isolates the absolute latch.
  for (int mv = 4030; mv <= 4120; mv += 30) b.feed(mv, 70000u + (mv - 4030) * 200u);
  for (int i = 0; i < 10; i++) b.feed(4120, 90000u + i * 5000u);   // band from below: stays off (hysteresis)
  TEST_ASSERT_FALSE(b.usbPowered());
  for (int mv = 4140; mv <= 4200; mv += 20) b.feed(mv, 150000u + (mv - 4140) * 300u);
  for (int i = 0; i < 5; i++) b.feed(4200, 160000u + i * 5000u);   // plugged back in: EMA climbs past ON, latch re-arms
  TEST_ASSERT_TRUE(b.usbPowered());
}

void test_usb_flag_clears_on_no_cell() {
  BatteryMonitor b;
  b.feed(4200, 0);
  TEST_ASSERT_TRUE(b.usbPowered());
  b.feed(0, 5000);                                   // no-cell reset: inert, including usb
  TEST_ASSERT_FALSE(b.usbPowered());
}

// --- Differential charge detector (BATT_CHG_STEP_MV / BATT_CHG_HOLD_*) --------------------------
// Covers the absolute latch's drained-cell blind window (usb false for 10-15 min of charging).
// The 40mV step / 5mV-per-min hold values are engineering estimates pending one bench
// plug/unplug session; these tests pin the LOGIC, not the calibration.

void test_chg_plug_step_sets_usb_long_before_latch() {
  BatteryMonitor b;
  b.feed(3700, 0);                                   // drained-ish cell, discharging
  TEST_ASSERT_FALSE(b.usbPowered());
  b.feed(3760, 5000);                                // plug in: +60mV I*R step, still ~400mV under the latch
  TEST_ASSERT_TRUE(b.usbPowered());
  TEST_ASSERT_TRUE(b.charging());
}

void test_chg_unplug_step_clears() {
  BatteryMonitor b;
  b.feed(3700, 0); b.feed(3760, 5000);
  TEST_ASSERT_TRUE(b.usbPowered());
  b.feed(3700, 10000);                               // unplug: symmetric sag
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_chg_false_step_self_heals_when_ema_stops_climbing() {
  BatteryMonitor b;
  b.feed(3700, 0); b.feed(3760, 5000);               // step, then FLAT: whatever that was, it isn't charging
  TEST_ASSERT_TRUE(b.usbPowered());
  uint32_t t = 10000;
  for (int i = 0; i < 60; i++, t += 5000) b.feed(3760, t);   // 5 min flat (EMA catch-up rides out window 1)
  TEST_ASSERT_FALSE(b.usbPowered());                 // slope hold cleared it
}

void test_chg_sustained_charge_holds_until_latch_takes_over() {
  BatteryMonitor b;
  b.feed(3900, 0);
  b.feed(3960, 5000);                                // plug step
  int mv = 3960; uint32_t t = 10000;
  while (mv < 4220) {                                // ~24mV/min CC ramp all the way to the top
    mv += 2; b.feed(mv, t); t += 5000;
    TEST_ASSERT_TRUE(b.usbPowered());                // no blind window anywhere along the charge
  }
  TEST_ASSERT_FALSE(b.charging());                   // absolute latch took over at the top, chg re-armed
}

void test_chg_load_step_under_threshold_ignored() {
  BatteryMonitor b;
  b.feed(3800, 0);
  b.feed(3822, 5000);                                // ~150mA mode swing * ~0.15ohm: not a plug
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_chg_no_cell_stays_inert() {
  BatteryMonitor b;
  b.feed(2900, 0);
  b.feed(2960, 5000);                                // floating divider jumping around: under NO_CELL, inert
  TEST_ASSERT_FALSE(b.usbPowered());
  b.feed(3400, 10000);                               // cell appears: big jump but prevMv was reset -> no false plug
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_usb_unplug_step_clears_latch() {
  BatteryMonitor b;
  b.feed(4200, 0);
  TEST_ASSERT_TRUE(b.usbPowered());
  b.feed(4140, 5000);                    // -60mV in one sample: no charger does that
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_usb_soft_unplug_sag_clears_latch_inside_band() {
  // The measured field case (2026-08-04): full float ~4171 on USB, unplug is a SOFT taper (no 40mV
  // step), and the cell rests at ~4125 -- inside the 4100/4150 band, so the old latch held
  // "usb yes" on battery forever (no idle sleep, no flush skip). The flat-vs-sagging rule clears it.
  BatteryMonitor b;
  uint32_t t = 0;
  for (int i = 0; i < 5; i++, t += 5000) b.feed(4171, t);
  TEST_ASSERT_TRUE(b.usbPowered());
  int sag[] = {4160, 4150, 4143, 4137, 4133, 4130, 4128, 4127, 4126, 4125};
  for (int i = 0; i < 10; i++, t += 5000) b.feed(sag[i], t);
  for (int i = 0; i < 24; i++, t += 5000) b.feed(4125, t);   // resting plateau, 2 min
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_usb_latch_survives_flat_float() {
  BatteryMonitor b;
  int wob[] = {4171, 4168, 4174, 4171, 4177, 4168};    // the measured desk noise band (+/-9mV raw)
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += 5000) b.feed(wob[i % 6], t);
  TEST_ASSERT_TRUE(b.usbPowered());                    // 5 min on the desk: no false unplug
}

void test_rise_detects_charging_with_no_step_history() {
  // The measured field case: a unit flashed/booted while already on the cable reads usb:false at
  // 3926mV and no step is ever seen. Two windows of real charge climb (~34mV/min measured) set chg.
  BatteryMonitor b;
  uint32_t t = 0;
  int mv = 3900;
  b.feed(mv, t); t += 5000;
  TEST_ASSERT_FALSE(b.usbPowered());                 // boot: blind, as observed
  for (int i = 0; i < 36; i++, t += 5000) { mv += 3; b.feed(mv, t); }   // ~36mV/min; needs 2 closed windows
  TEST_ASSERT_TRUE(b.usbPowered());
  TEST_ASSERT_TRUE(b.charging());
}

void test_rise_ignores_one_window_of_relaxation() {
  // Cell relaxation after leaving a heavy mode: climbs once, then flat. Must NOT read as charging.
  BatteryMonitor b;
  uint32_t t = 0;
  int mv = 3700;
  b.feed(mv, t); t += 5000;
  for (int i = 0; i < 12; i++, t += 5000) { mv += 3; b.feed(mv, t); }   // one window of climb
  for (int i = 0; i < 36; i++, t += 5000) b.feed(mv, t);                // then flat for 3 min
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_rise_ignores_discharge() {
  BatteryMonitor b;
  uint32_t t = 0;
  int mv = 3900;
  for (int i = 0; i < 60; i++, t += 5000) { mv -= 2; b.feed(mv, t); }   // steady discharge, 5 min
  TEST_ASSERT_FALSE(b.usbPowered());
}

void test_chg_survives_the_window_after_a_deep_unplug_sag() {
  // Field trace 2026-08-04 (puck 5B5F027308): unplugged at low SoC the cell sagged 3956->3565; the
  // replug step set chg, then the very next hold window compared the recovering EMA against its
  // PRE-UNPLUG anchor, read a net fall, and cleared chg ~6 SECONDS after it fired. A step must
  // re-anchor the slope window -- the old regime's voltage says nothing about the new one.
  BatteryMonitor b;
  uint32_t t = 0;
  for (int i = 0; i < 12; i++, t += 5000) b.feed(3956, t);          // on the charger, flat
  for (int i = 0; i < 10; i++, t += 5000) b.feed(3565, t);          // unplugged: deep sag
  b.feed(3860, t); t += 5000;                                        // replug: ~+295 step
  TEST_ASSERT_TRUE(b.charging());
  // Only far enough to cross the ONE window boundary that the stale anchor poisons. Running longer
  // would let the sustained-rise rule set chg again ~2 min later and hide the regression entirely
  // (this test passed for exactly that reason before the assertion was tightened).
  for (int i = 0; i < 5; i++, t += 5000) b.feed(3863 + i * 3, t);    // keeps charging
  TEST_ASSERT_TRUE(b.charging());                                    // must not have been cleared
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_cell_is_inert);
  RUN_TEST(test_unplugging_cell_recovers_from_low);
  RUN_TEST(test_hysteresis_low_then_recover_then_low);
  RUN_TEST(test_splash_on_entry_then_cadence);
  RUN_TEST(test_low_reentry_rearms_entry_splash);
  RUN_TEST(test_cutoff_needs_three_consecutive_raw_samples);
  RUN_TEST(test_cutoff_latches);
  RUN_TEST(test_no_cell_resets_cutoff_latch);
  RUN_TEST(test_usb_detect_by_charge_voltage_with_hysteresis);
  RUN_TEST(test_usb_flag_clears_on_no_cell);
  RUN_TEST(test_chg_plug_step_sets_usb_long_before_latch);
  RUN_TEST(test_chg_unplug_step_clears);
  RUN_TEST(test_chg_false_step_self_heals_when_ema_stops_climbing);
  RUN_TEST(test_chg_sustained_charge_holds_until_latch_takes_over);
  RUN_TEST(test_chg_load_step_under_threshold_ignored);
  RUN_TEST(test_chg_no_cell_stays_inert);
  RUN_TEST(test_usb_unplug_step_clears_latch);
  RUN_TEST(test_usb_soft_unplug_sag_clears_latch_inside_band);
  RUN_TEST(test_usb_latch_survives_flat_float);
  RUN_TEST(test_rise_detects_charging_with_no_step_history);
  RUN_TEST(test_rise_ignores_one_window_of_relaxation);
  RUN_TEST(test_rise_ignores_discharge);
  RUN_TEST(test_chg_survives_the_window_after_a_deep_unplug_sag);
  return UNITY_END();
}
