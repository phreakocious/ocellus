#include <unity.h>
#include "../../nfo_crawl.h"

// Scale rises monotonically down the screen: far/small at the top, near/large at the bottom.
// halfWidth is the observable proxy for scale, and srcY must advance in the same direction.
void test_scale_increases_downward() {
  NfoRow t[NFO_SCREEN]; nfoBuildTable(t);
  for (int y = 1; y < NFO_SCREEN; y++) {
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(t[y-1].halfWidth, t[y].halfWidth);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(t[y-1].srcYQ8,    t[y].srcYQ8);
  }
  // 0.45 far, 1.21 near against a 288px full-scale row -> 64px and 174px half-widths.
  TEST_ASSERT_INT_WITHIN(4,  64, t[0].halfWidth);
  TEST_ASSERT_INT_WITHIN(4, 174, t[NFO_SCREEN-1].halfWidth);
}

// Source Y advances down the screen and spans ~197px, NOT 240. A 240 here means someone
// reintroduced a 1:1 vertical mapping, which silently corrupts every timing number.
void test_source_span_is_not_screen_height() {
  NfoRow t[240]; nfoBuildTable(t);
  int32_t span = (t[239].srcYQ8 - t[0].srcYQ8) >> 8;
  TEST_ASSERT_INT_WITHIN(6, 197, span);
}

// Traversal must use the source span, and must scale with the line count.
void test_traversal_uses_source_span() {
  TEST_ASSERT_INT_WITHIN(6, 512 + 197, nfoTraversal(32));
  TEST_ASSERT_INT_WITHIN(6, 560 + 197, nfoTraversal(35));
  TEST_ASSERT_GREATER_THAN_INT32(nfoTraversal(32), nfoTraversal(35));
}

// The reciprocal is what keeps division out of the per-pixel loop.
void test_inv_col_is_reciprocal_of_cell_width() {
  NfoRow t[240]; nfoBuildTable(t);
  for (int y = 0; y < 240; y += 17) {
    int32_t cols = (t[y].halfWidth * 2 * t[y].invColQ16) >> 16;
    TEST_ASSERT_INT_WITHIN(1, NFO_COLS, cols);
  }
}

// vgaCellBit's 9th-column rule is "the rule that fails silently" -- a wrong range (e.g.
// 0xB0..0xDF instead of 0xC0..0xDF) compiles clean and every other test still passes; it only
// shows up as broken box joins on the panel. Pin it on the host instead.
//
// 0xCD is '=' (a horizontal double box rule): row bytes are 0x00 except rows 5 and 7, which are
// 0xFF -- bit 0 (column 7, the one that gets replicated into column 8) is set on exactly those
// two rows.
void test_box_code_replicates_column_seven_into_column_eight() {
  for (int r = 0; r < VGA_FONT_H; r++) {
    bool expect = (r == 5 || r == 7);
    TEST_ASSERT_EQUAL_INT(expect, vgaCellBit(0xCD, 8, r));
  }
}

// 0x2A is '*', outside the 0xC0..0xDF join range, and its row 7 byte (0xFF) has bit 0 set --
// so a range check widened to include 0x2A (or any code below 0xC0) would make column 8 light
// up here. It must not: column 8 only ever exists for the box-drawing range.
void test_non_box_code_never_gets_a_ninth_column() {
  TEST_ASSERT_FALSE(vgaCellBit(0x2A, 8, 7));
}

void setUp() {} void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scale_increases_downward);
  RUN_TEST(test_source_span_is_not_screen_height);
  RUN_TEST(test_traversal_uses_source_span);
  RUN_TEST(test_inv_col_is_reciprocal_of_cell_width);
  RUN_TEST(test_box_code_replicates_column_seven_into_column_eight);
  RUN_TEST(test_non_box_code_never_gets_a_ninth_column);
  return UNITY_END();
}
