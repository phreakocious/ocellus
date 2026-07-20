#include <unity.h>
#include <cstdint>
#include "../../fb_roll.h"

// 3x2 row-major grid, values = flat index:  row0 = 0 1 2   row1 = 3 4 5
static void grid(uint16_t* fb) { for (uint16_t i = 0; i < 6; i++) fb[i] = i; }

// +dx moves content toward larger x: column c -> c+dx, last column wraps to 0.
void test_roll_right() {
  uint16_t fb[6]; grid(fb);
  rollFramebuffer(fb, 3, 2, 1, 0);
  uint16_t want[6] = {2,0,1, 5,3,4};                 // each row rotated right by one
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT16(want[i], fb[i]);
}

// -dx moves content toward smaller x (mod-normalized, no negative-index bug).
void test_roll_left() {
  uint16_t fb[6]; grid(fb);
  rollFramebuffer(fb, 3, 2, -1, 0);
  uint16_t want[6] = {1,2,0, 4,5,3};                 // each row rotated left by one
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT16(want[i], fb[i]);
}

// +dy moves content down: old row0 -> row1, last row wraps to row0.
void test_roll_down() {
  uint16_t fb[6]; grid(fb);
  rollFramebuffer(fb, 3, 2, 0, 1);
  uint16_t want[6] = {3,4,5, 0,1,2};
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT16(want[i], fb[i]);
}

// Diagonal = both rolls composed.
void test_roll_diagonal() {
  uint16_t fb[6]; grid(fb);
  rollFramebuffer(fb, 3, 2, 1, 1);                   // down then right
  uint16_t want[6] = {5,3,4, 2,0,1};
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT16(want[i], fb[i]);
}

// A full-width / full-height roll is the identity -- this is why EV_WANDER_OFF lands home
// exactly when its offset ramps to 240 (== W == H).
void test_full_wrap_is_identity() {
  uint16_t fb[6]; grid(fb);
  rollFramebuffer(fb, 3, 2, 3, 2);
  for (uint16_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT16(i, fb[i]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roll_right);
  RUN_TEST(test_roll_left);
  RUN_TEST(test_roll_down);
  RUN_TEST(test_roll_diagonal);
  RUN_TEST(test_full_wrap_is_identity);
  return UNITY_END();
}
