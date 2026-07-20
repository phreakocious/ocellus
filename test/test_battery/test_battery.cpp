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
  b.feed(4120, 5000);                                // sag into the hysteresis band: stays on
  TEST_ASSERT_TRUE(b.usbPowered());
  for (int i = 0; i < 10; i++) b.feed(4000, 10000u + i * 5000u);   // unplugged: EMA walks under OFF
  TEST_ASSERT_FALSE(b.usbPowered());
  for (int i = 0; i < 10; i++) b.feed(4120, 70000u + i * 5000u);   // band from below: stays off (hysteresis)
  TEST_ASSERT_FALSE(b.usbPowered());
  for (int i = 0; i < 5; i++) b.feed(4200, 130000u + i * 5000u);   // plugged back in: EMA climbs past ON, latch re-arms
  TEST_ASSERT_TRUE(b.usbPowered());
}

void test_usb_flag_clears_on_no_cell() {
  BatteryMonitor b;
  b.feed(4200, 0);
  TEST_ASSERT_TRUE(b.usbPowered());
  b.feed(0, 5000);                                   // no-cell reset: inert, including usb
  TEST_ASSERT_FALSE(b.usbPowered());
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
  return UNITY_END();
}
