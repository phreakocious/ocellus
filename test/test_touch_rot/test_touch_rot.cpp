#include <unity.h>
#include "../../touch.h"

void test_rotation_zero_is_identity() {
  int x = 10, y = 200;
  touchNormalize(x, y, 0);
  TEST_ASSERT_EQUAL_INT(10, x);
  TEST_ASSERT_EQUAL_INT(200, y);
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_LEFT, touchRotateGesture(TOUCH_SWIPE_LEFT, 0));
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_UP,   touchRotateGesture(TOUCH_SWIPE_UP, 0));
}

void test_rotation_two_flips_both_axes() {
  int x = 10, y = 200;
  touchNormalize(x, y, 2);
  TEST_ASSERT_EQUAL_INT(229, x);
  TEST_ASSERT_EQUAL_INT(39, y);
}

void test_rotation_two_is_an_involution() {
  int x = 73, y = 141;
  touchNormalize(x, y, 2);
  touchNormalize(x, y, 2);
  TEST_ASSERT_EQUAL_INT(73, x);
  TEST_ASSERT_EQUAL_INT(141, y);
}

// This is the shipped bug: at rotation 2 the panel's "left" is the screen's "right".
void test_rotation_two_swaps_horizontal_gestures() {
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_RIGHT, touchRotateGesture(TOUCH_SWIPE_LEFT, 2));
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_LEFT,  touchRotateGesture(TOUCH_SWIPE_RIGHT, 2));
}

void test_rotation_two_swaps_vertical_gestures() {
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_DOWN, touchRotateGesture(TOUCH_SWIPE_UP, 2));
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_UP,   touchRotateGesture(TOUCH_SWIPE_DOWN, 2));
}

void test_tap_and_none_are_never_rotated() {
  TEST_ASSERT_EQUAL_INT(TOUCH_TAP,  touchRotateGesture(TOUCH_TAP, 2));
  TEST_ASSERT_EQUAL_INT(TOUCH_NONE, touchRotateGesture(TOUCH_NONE, 2));
}

// imuRotation() only ever returns 0 or 2, but a stale or garbage value must not corrupt
// coordinates -- anything that is not 2 is the identity.
void test_unexpected_rotation_is_identity() {
  int x = 5, y = 6;
  touchNormalize(x, y, 1);
  TEST_ASSERT_EQUAL_INT(5, x);
  TEST_ASSERT_EQUAL_INT(6, y);
  TEST_ASSERT_EQUAL_INT(TOUCH_SWIPE_LEFT, touchRotateGesture(TOUCH_SWIPE_LEFT, 3));
}

// A partial read must leave the retained point EXACTLY as it was. If touchApplySample ever
// re-normalizes it, this fails at rotation 2 -- which is the whole defect.
void test_partial_read_leaves_retained_point_untouched() {
  int x = 60, y = 90;                       // already screen space, as if from an earlier poll
  TEST_ASSERT_FALSE(touchApplySample(-1, 0x3C, 0, 0x5A, 2, x, y));
  TEST_ASSERT_EQUAL_INT(60, x);
  TEST_ASSERT_EQUAL_INT(90, y);
  TEST_ASSERT_FALSE(touchApplySample(0, 0x3C, -1, 0x5A, 2, x, y));   // y-axis failure too
  TEST_ASSERT_EQUAL_INT(60, x);
  TEST_ASSERT_EQUAL_INT(90, y);
}

// A complete read normalizes exactly once. Raw (60,90) at rot 2 -> (179,149), not back to (60,90).
void test_full_read_normalizes_exactly_once() {
  int x = 0, y = 0;
  TEST_ASSERT_TRUE(touchApplySample(0, 60, 0, 90, 2, x, y));
  TEST_ASSERT_EQUAL_INT(179, x);
  TEST_ASSERT_EQUAL_INT(149, y);
}

void test_full_read_at_rotation_zero_is_raw() {
  int x = 0, y = 0;
  TEST_ASSERT_TRUE(touchApplySample(0, 60, 0, 90, 0, x, y));
  TEST_ASSERT_EQUAL_INT(60, x);
  TEST_ASSERT_EQUAL_INT(90, y);
}

// The 12-bit assembly: high nibble of xh is masked off, low byte is xl.
void test_sample_assembles_twelve_bit_coordinates() {
  int x = 0, y = 0;
  TEST_ASSERT_TRUE(touchApplySample(0xF0, 0xEF, 0x00, 0x2A, 0, x, y));
  TEST_ASSERT_EQUAL_INT(0x0EF, x);          // 0xF0 & 0x0F == 0 -> 0x0EF
  TEST_ASSERT_EQUAL_INT(0x02A, y);
}

void test_snap_packs_and_unpacks() {
  uint32_t s = touchSnapPack(true, 173, 41);
  TEST_ASSERT_TRUE(touchSnapDown(s));
  TEST_ASSERT_EQUAL_INT(173, touchSnapX(s));
  TEST_ASSERT_EQUAL_INT(41, touchSnapY(s));
}

void test_snap_finger_up_is_zero() {
  TEST_ASSERT_FALSE(touchSnapDown(0));
}

// 239 is the largest coordinate; both fields must survive the packing intact.
void test_snap_handles_max_coordinates() {
  uint32_t s = touchSnapPack(true, TOUCH_MAX, TOUCH_MAX);
  TEST_ASSERT_TRUE(touchSnapDown(s));
  TEST_ASSERT_EQUAL_INT(TOUCH_MAX, touchSnapX(s));
  TEST_ASSERT_EQUAL_INT(TOUCH_MAX, touchSnapY(s));
}

// Out-of-range input must not bleed x into the down bit or y into x.
void test_snap_clamps_out_of_range() {
  uint32_t s = touchSnapPack(true, 9999, -5);
  TEST_ASSERT_TRUE(touchSnapDown(s));
  TEST_ASSERT_EQUAL_INT(TOUCH_MAX, touchSnapX(s));
  TEST_ASSERT_EQUAL_INT(0, touchSnapY(s));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rotation_zero_is_identity);
  RUN_TEST(test_rotation_two_flips_both_axes);
  RUN_TEST(test_rotation_two_is_an_involution);
  RUN_TEST(test_rotation_two_swaps_horizontal_gestures);
  RUN_TEST(test_rotation_two_swaps_vertical_gestures);
  RUN_TEST(test_tap_and_none_are_never_rotated);
  RUN_TEST(test_unexpected_rotation_is_identity);
  RUN_TEST(test_partial_read_leaves_retained_point_untouched);
  RUN_TEST(test_full_read_normalizes_exactly_once);
  RUN_TEST(test_full_read_at_rotation_zero_is_raw);
  RUN_TEST(test_sample_assembles_twelve_bit_coordinates);
  RUN_TEST(test_snap_packs_and_unpacks);
  RUN_TEST(test_snap_finger_up_is_zero);
  RUN_TEST(test_snap_handles_max_coordinates);
  RUN_TEST(test_snap_clamps_out_of_range);
  return UNITY_END();
}
