#include <unity.h>
#include <cmath>
#include "../../carousel.h"

static const uint8_t IDS[5] = {3, 7, 11, 20, 46};

static Carousel opened(uint8_t cur = 11, int n = 5) {
  Carousel c;
  c.open(IDS, n, cur);
  return c;
}

// 40 entries, so a ~10-item coast cannot wrap and "did it travel forward?" stays meaningful.
static uint8_t LONG_IDS[40];
static Carousel openedLong(uint8_t cur) {
  for (int i = 0; i < 40; i++) LONG_IDS[i] = (uint8_t)i;
  Carousel c;
  c.open(LONG_IDS, 40, cur);
  return c;
}

// Coast to a standstill: 10 simulated seconds at 60fps is far longer than any
// flick survives, so whatever it returns is the resting state.
static void settle(Carousel& c) {
  for (int i = 0; i < 600; i++) c.tick(1.0f / 60.0f);
}

// Feed a constant-speed drag: `frames` samples of `pxPerFrame` at 60fps.
// Negative pxPerFrame drags left, which must move the list FORWARD.
static void dragBy(Carousel& c, float pxPerFrame, int frames) {
  float x = 120.0f;
  c.drag((int)x);                       // first sample only latches the origin
  for (int i = 0; i < frames; i++) {
    x += pxPerFrame;
    c.drag((int)x);
    c.tick(1.0f / 60.0f);
  }
}

void test_open_centres_on_current_id() {
  Carousel c = opened(20);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, c.pos());
  TEST_ASSERT_EQUAL_UINT8(20, c.settledId());
}

void test_open_unknown_id_falls_back_to_first() {
  Carousel c = opened(99);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.pos());
  TEST_ASSERT_EQUAL_UINT8(3, c.settledId());
}

// 120px of finger travel == exactly one item, in both directions.
void test_drag_maps_120px_to_one_item() {
  Carousel c = opened(3);               // pos 0
  dragBy(c, -12.0f, 10);                // 120px left
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, c.pos());

  Carousel d = opened(20);              // pos 3
  dragBy(d, 12.0f, 10);                 // 120px right
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 2.0f, d.pos());
}

void test_moving_while_dragging_and_settled_id_hidden() {
  Carousel c = opened(3);
  c.drag(120);
  c.drag(100);
  c.tick(1.0f / 60.0f);
  TEST_ASSERT_TRUE(c.moving());
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.settledId());
}

// A flick keeps going after release, in the direction it was thrown. Uses the 40-item list:
// on a 5-item list a ~10-item coast wraps twice and the comparison is meaningless.
void test_flick_coasts_forward_then_stops_on_an_item() {
  Carousel c = openedLong(0);
  dragBy(c, -25.0f, 6);                 // fast leftward flick
  float atRelease = c.pos();
  c.release();
  settle(c);
  TEST_ASSERT_FALSE(c.moving());
  TEST_ASSERT_TRUE(c.pos() > atRelease);            // coasted further forward
  TEST_ASSERT_FLOAT_WITHIN(0.001f, roundf(c.pos()), c.pos());   // landed on an item
  TEST_ASSERT_NOT_EQUAL(0xFF, c.settledId());
}

void test_flick_backward_coasts_backward() {
  Carousel c = openedLong(30);          // start high enough that a backward coast cannot wrap
  dragBy(c, 25.0f, 6);
  float atRelease = c.pos();
  c.release();
  settle(c);
  TEST_ASSERT_FALSE(c.moving());
  TEST_ASSERT_TRUE(c.pos() < atRelease);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, roundf(c.pos()), c.pos());
}

// Harder flick => further travel. Tests the property, not a tuned constant.
void test_faster_flick_travels_further() {
  Carousel slow = openedLong(0);
  Carousel fast = openedLong(0);
  dragBy(slow, -8.0f, 6);   slow.release();  settle(slow);
  dragBy(fast, -30.0f, 6);  fast.release();  settle(fast);
  TEST_ASSERT_TRUE(fast.pos() > slow.pos());
}

void test_velocity_is_clamped() {
  Carousel c = openedLong(0);
  dragBy(c, -600.0f, 4);                // absurd: 36000 px/s
  TEST_ASSERT_TRUE(fabsf(c.velocity()) <= Carousel::V_MAX + 0.001f);
}

void test_wraps_forward_past_end() {
  Carousel c = opened(46);              // pos 4, last of 5
  dragBy(c, -12.0f, 10);                // one item forward -> wraps to 0
  c.release();
  settle(c);
  TEST_ASSERT_EQUAL_UINT8(3, c.settledId());
}

void test_wraps_backward_past_zero() {
  Carousel c = opened(3);               // pos 0
  dragBy(c, 12.0f, 10);                 // one item back -> wraps to 4
  c.release();
  settle(c);
  TEST_ASSERT_EQUAL_UINT8(46, c.settledId());
}

void test_single_item_never_moves() {
  Carousel c;
  const uint8_t one[1] = {9};
  c.open(one, 1, 9);
  dragBy(c, -40.0f, 20);
  c.release();
  settle(c);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.pos());
  TEST_ASSERT_EQUAL_UINT8(9, c.settledId());
}

// Unreachable in the firmware (PLAYABLE_MASK is never 0) but the guard is
// cheaper than the assumption.
void test_empty_list_is_inert() {
  Carousel c;
  c.open(IDS, 0, 3);
  dragBy(c, -40.0f, 10);
  c.release();
  settle(c);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.settledId());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.pos());
}

void test_held_still_then_released_does_not_coast() {
  Carousel c = opened(3);
  c.drag(120);
  for (int i = 0; i < 10; i++) { c.drag(120); c.tick(1.0f / 60.0f); }
  c.release();
  settle(c);
  TEST_ASSERT_EQUAL_UINT8(3, c.settledId());   // stayed put
}

// The REAL firmware cadence: loop() ticks the carousel ~3x per rendered frame, but touchPoll()
// publishes a new sample only every ~10ms. Most ticks therefore carry no new motion. dragBy()
// above pairs one drag() with one tick(), which is the one cadence the device never produces --
// that gap is what let a velocity-destroying bug reach the final review.
//
// `x` is threaded through by reference (not re-latched to a fixed coordinate per call) so two
// calls back-to-back model one continuous finger motion, the same as the device: re-seeding x
// while `dragging` is still true from a prior call would inject a phantom jump on the first
// drag() of the second call (delta from the OLD internal lastX to the NEW call's start x),
// which is not what either caller below intends.
static void dragRealCadence(Carousel& c, float& x, float pxPerSample, int samples) {
  c.drag((int)x);
  for (int s = 0; s < samples; s++) {
    x += pxPerSample;                       // one new touch sample per 10ms
    for (int k = 0; k < 3; k++) {           // ...but three ticks in that time
      c.drag((int)x);                       // same coordinate on the 2nd and 3rd
      c.tick(0.0033f);
    }
  }
}

void test_flick_survives_multiple_ticks_per_sample() {
  Carousel c = openedLong(0);
  float x = 200.0f;
  dragRealCadence(c, x, -15.0f, 10);
  TEST_ASSERT_TRUE(fabsf(c.velocity()) > 1.0f);   // must NOT have been zeroed by the empty ticks
  float atRelease = c.pos();
  c.release();
  settle(c);
  TEST_ASSERT_TRUE(c.pos() > atRelease + 1.0f);   // and must actually coast
}

// A finger held still must still not coast, even though the stall rule is what allows the above.
void test_held_still_at_real_cadence_does_not_coast() {
  Carousel c = openedLong(0);
  float x = 200.0f;
  dragRealCadence(c, x, -15.0f, 5);       // move...
  dragRealCadence(c, x, 0.0f, 20);        // ...then hold still well past STALL_S
  float atRelease = c.pos();
  c.release();
  settle(c);
  // Assert POSITION, not moving()/settledId(): 600 settle() ticks drive those to a fixed point
  // regardless of whether the stall rule exists, so asserting them cannot fail. Without STALL_S
  // the held-still finger inherits the earlier motion's velocity and coasts ~10 items from here.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, atRelease, c.pos());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.velocity());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_open_centres_on_current_id);
  RUN_TEST(test_open_unknown_id_falls_back_to_first);
  RUN_TEST(test_drag_maps_120px_to_one_item);
  RUN_TEST(test_moving_while_dragging_and_settled_id_hidden);
  RUN_TEST(test_flick_coasts_forward_then_stops_on_an_item);
  RUN_TEST(test_flick_backward_coasts_backward);
  RUN_TEST(test_faster_flick_travels_further);
  RUN_TEST(test_velocity_is_clamped);
  RUN_TEST(test_wraps_forward_past_end);
  RUN_TEST(test_wraps_backward_past_zero);
  RUN_TEST(test_single_item_never_moves);
  RUN_TEST(test_empty_list_is_inert);
  RUN_TEST(test_held_still_then_released_does_not_coast);
  RUN_TEST(test_flick_survives_multiple_ticks_per_sample);
  RUN_TEST(test_held_still_at_real_cadence_does_not_coast);
  return UNITY_END();
}
