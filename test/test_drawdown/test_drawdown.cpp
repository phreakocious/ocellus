#include <unity.h>
#include "../../audio.h"

// A DrawdownState as onAnimEnter would reset it at entry time `t0`.
static DrawdownState entry(uint32_t t0) {
  return DrawdownState{ true, t0, t0, BACKOFF_BASE_MS };
}

// Fresh entry holds the first listen window open a full LISTEN_MS even though `stale` is true.
void test_entry_holds_first_window() {
  DrawdownState s = entry(1000);
  drawdownStep(s, true, 1000 + LISTEN_MS - 1);   // just before the window elapses
  TEST_ASSERT_TRUE(s.wantRadio);                 // still listening
  drawdownStep(s, true, 1000 + LISTEN_MS);       // window elapsed, no packet
  TEST_ASSERT_FALSE(s.wantRadio);                // radio closed
}

// First off-interval is the full BACKOFF_BASE_MS (not doubled), then it doubles on each reopen.
void test_first_offinterval_is_base_then_doubles() {
  DrawdownState s = entry(0);
  drawdownStep(s, true, LISTEN_MS);              // close first window -> off
  TEST_ASSERT_FALSE(s.wantRadio);
  uint32_t offAt = s.offAt;
  drawdownStep(s, true, offAt + BACKOFF_BASE_MS - 1);   // still within first off-interval
  TEST_ASSERT_FALSE(s.wantRadio);
  drawdownStep(s, true, offAt + BACKOFF_BASE_MS);       // first off-interval served -> reopen
  TEST_ASSERT_TRUE(s.wantRadio);
  TEST_ASSERT_EQUAL_UINT32(BACKOFF_BASE_MS * 2, s.backoffMs);   // doubled AFTER serving
}

// Backoff doubles up to BACKOFF_MAX_MS and clamps there.
void test_backoff_caps_at_max() {
  DrawdownState s = entry(0);
  uint32_t now = 0;
  for (int i = 0; i < 20; i++) {                 // many empty cycles
    now += LISTEN_MS; drawdownStep(s, true, now);        // close window
    now += s.backoffMs; drawdownStep(s, true, now);      // serve off-interval, reopen
  }
  TEST_ASSERT_EQUAL_UINT32(BACKOFF_MAX_MS, s.backoffMs);
}

// A packet (stale=false) snaps radio back on and resets backoff.
void test_not_stale_resets() {
  DrawdownState s = entry(0);
  drawdownStep(s, true, LISTEN_MS);              // draw down (close the first window)
  TEST_ASSERT_FALSE(s.wantRadio);
  drawdownStep(s, false, 5000);                  // packet heard
  TEST_ASSERT_TRUE(s.wantRadio);
  TEST_ASSERT_EQUAL_UINT32(BACKOFF_BASE_MS, s.backoffMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_entry_holds_first_window);
  RUN_TEST(test_first_offinterval_is_base_then_doubles);
  RUN_TEST(test_backoff_caps_at_max);
  RUN_TEST(test_not_stale_resets);
  return UNITY_END();
}
