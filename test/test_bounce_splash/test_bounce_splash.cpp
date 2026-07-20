#include <unity.h>
#include <cmath>
#include "../../bounce_splash.h"

using namespace bounce;

static float distToCenter(int x, int y) {
  float dx = x - 120.0f, dy = y - 120.0f;
  return sqrtf(dx * dx + dy * dy);
}

// The whole point of reverse simulation: every letter's LAST playback frame is exactly its slot,
// so a random-looking entry always lands perfectly in order. If this holds, the reveal can't cheat.
void test_lands_exactly_on_ordered_slots() {
  Trajectories T;
  compute(T, 5, 0xC0FFEE);
  for (int i = 0; i < T.count; i++) {
    int last = T.frames[i] - 1;
    TEST_ASSERT_EQUAL_INT16(T.slotX[i], T.x[i][last]);
    TEST_ASSERT_EQUAL_INT16(T.slotY[i], T.y[i][last]);
  }
}

// Slots read left-to-right in name order (i increasing => x increasing) -> "the right order".
void test_slots_left_to_right() {
  Trajectories T;
  compute(T, 8, 42);
  for (int i = 1; i < T.count; i++)
    TEST_ASSERT_TRUE(T.slotX[i] > T.slotX[i - 1]);
}

// Letters actually come in from off the screen (entry frame is beyond the round edge), and land
// inside it. Guards against a sim that never left (would enter mid-screen) or a bad cap.
void test_enters_offscreen_lands_onscreen() {
  Trajectories T;
  compute(T, 6, 12345);
  for (int i = 0; i < T.count; i++) {
    TEST_ASSERT_TRUE(distToCenter(T.x[i][0], T.y[i][0]) >= 120.0f);          // entry off-screen
    int last = T.frames[i] - 1;
    TEST_ASSERT_TRUE(distToCenter(T.x[i][last], T.y[i][last]) <= 120.0f);    // slot on-screen
  }
}

// Animation length stays in a sane window (~1.3s..~3s at 16ms/frame). Doubles as a tuning guard:
// if physics constants drift and trajectories balloon or collapse, this trips.
void test_animation_length_sane() {
  for (uint32_t seed = 1; seed <= 40; seed++) {
    Trajectories T;
    compute(T, (int)(seed % 12) + 2, seed);
    TEST_ASSERT_TRUE(T.maxFrames >= 20);
    TEST_ASSERT_TRUE(T.maxFrames <= 190);       // < MAX_FRAMES: no letter hit the runaway cap
    for (int i = 0; i < T.count; i++)
      TEST_ASSERT_TRUE(T.frames[i] < MAX_FRAMES);
  }
}

// Same seed reproduces (deterministic LCG, not Arduino random()).
void test_deterministic() {
  Trajectories A, B;
  compute(A, 7, 999);
  compute(B, 7, 999);
  for (int i = 0; i < A.count; i++) {
    TEST_ASSERT_EQUAL_INT(A.frames[i], B.frames[i]);
    TEST_ASSERT_EQUAL_INT16(A.x[i][0], B.x[i][0]);
  }
}

// Long names clamp to the buffer instead of overflowing.
void test_clamps_letter_count() {
  Trajectories T;
  compute(T, 40, 7);
  TEST_ASSERT_EQUAL_INT(MAX_LETTERS, T.count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lands_exactly_on_ordered_slots);
  RUN_TEST(test_slots_left_to_right);
  RUN_TEST(test_enters_offscreen_lands_onscreen);
  RUN_TEST(test_animation_length_sane);
  RUN_TEST(test_deterministic);
  RUN_TEST(test_clamps_letter_count);
  return UNITY_END();
}
