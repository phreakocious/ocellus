#include <unity.h>
#include "../../yinyang.h"

// A bit-order flip (LSB-first instead of MSB-first) mirrors the image inside every byte -- an
// 8px-banded smear that is easy to miss by eye. (142,37) is set MSB-first and clear LSB-first,
// so it alone pins the order; the row scans below pin it again from the other direction.
void test_bit_order_is_msb_first() {
  TEST_ASSERT_EQUAL_UINT8(1, yyBit(142, 37));
}

void test_row_extents() {
  // first/last lit pixel on two rows. LSB-first order would give (8,150) and (0,183).
  int first = -1, last = -1;
  for (int x = 0; x < 240; x++) if (yyBit(x, 100)) { if (first < 0) first = x; last = x; }
  TEST_ASSERT_EQUAL_INT(9,   first);
  TEST_ASSERT_EQUAL_INT(148, last);

  first = last = -1;
  for (int x = 0; x < 240; x++) if (yyBit(x, 120)) { if (first < 0) first = x; last = x; }
  TEST_ASSERT_EQUAL_INT(7,   first);
  TEST_ASSERT_EQUAL_INT(183, last);
}

void test_total_lit_pixels() {
  int n = 0;
  for (int y = 0; y < 240; y++) for (int x = 0; x < 240; x++) n += yyBit(x, y);
  TEST_ASSERT_EQUAL_INT(19028, n);   // guards against a truncated or padded generation
}

// The render loop samples inverse-rotated points that can land outside the sprite. It relies on
// this returning 0 rather than reading off the end of the array.
void test_out_of_bounds_reads_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, yyBit(-1, 120));
  TEST_ASSERT_EQUAL_UINT8(0, yyBit(120, -1));
  TEST_ASSERT_EQUAL_UINT8(0, yyBit(240, 120));
  TEST_ASSERT_EQUAL_UINT8(0, yyBit(120, 240));
  TEST_ASSERT_EQUAL_UINT8(0, yyBit(9999, 9999));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bit_order_is_msb_first);
  RUN_TEST(test_row_extents);
  RUN_TEST(test_total_lit_pixels);
  RUN_TEST(test_out_of_bounds_reads_zero);
  return UNITY_END();
}
