#include <unity.h>
#include <cstring>
#include "../../nfo.h"

static uint32_t seq = 0;
static uint32_t fakeRng(uint32_t n) { return n ? (seq++ % n) : 0; }

// Every emitted line is exactly NFO_COLS and carries its border bytes. The generator fills
// rather than truncates -- two clipped lines got through the design draft precisely because
// nothing checked this.
void test_every_line_is_full_width() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  for (int trial = 0; trial < 40; trial++) {
    seq = trial;
    memset(grid, 0, sizeof grid);
    int n = nfoBuild(grid, NFO_MAX_LINES, fakeRng);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    for (int r = 0; r < n; r++) {
      for (int c = 0; c < NFO_COLS; c++)
        TEST_ASSERT_NOT_EQUAL_UINT8(0, grid[r][c]);   // 0 = a cell nobody wrote
      const uint8_t l = grid[r][0], rr = grid[r][NFO_COLS - 1];
      TEST_ASSERT_TRUE(l == NFO_V || l == NFO_TL || l == NFO_ML || l == NFO_BL);
      TEST_ASSERT_TRUE(rr == NFO_V || rr == NFO_TR || rr == NFO_MR || rr == NFO_BR);
    }
  }
}

// "[ a ] [ b ]" occupies len(a) + len(b) + 9 columns against a 30-column interior,
// so the pair fits iff len(a) + len(b) <= 21. Not 29 -- the brackets and the
// separating space are eight characters that are easy to forget.
void test_pair_predicate_accounts_for_brackets() {
  TEST_ASSERT_TRUE (nfoPairFits("phreakocious", "kitsune"));   // 12 + 7  = 19
  TEST_ASSERT_TRUE (nfoPairFits("Bitquark", "tense future"));  //  8 + 12 = 20
  TEST_ASSERT_TRUE (nfoPairFits("doc", "buttersnatcher"));     //  3 + 14 = 17
  TEST_ASSERT_FALSE(nfoPairFits("buttersnatcher", "flyingtoasters"));  // 14 + 14 = 28
  TEST_ASSERT_FALSE(nfoPairFits("preterition", "phreakocious"));       // 11 + 12 = 23
}

// The block is 5-7 rows in practice and up to 9 in principle; the grid must never
// exceed NFO_MAX_LINES for ANY shuffle, or nfoBuild overruns the caller's buffer.
void test_line_count_within_bounds() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  for (int trial = 0; trial < 500; trial++) {
    seq = trial * 7;
    int n = nfoBuild(grid, NFO_MAX_LINES, fakeRng);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(31, n);
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_MAX_LINES, n);
  }
}

// Every handle must fit a solo bracket row, or a name silently vanishes.
void test_every_name_fits_a_solo_row() {
  for (int i = 0; i < GREETZ_NAME_COUNT; i++)
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_COLS - 2, (int)strlen(GREETZ_NAMES[i]) + 4);
}

void setUp() {} void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_line_is_full_width);
  RUN_TEST(test_pair_predicate_accounts_for_brackets);
  RUN_TEST(test_line_count_within_bounds);
  RUN_TEST(test_every_name_fits_a_solo_row);
  return UNITY_END();
}
