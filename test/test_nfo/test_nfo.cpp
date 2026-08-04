#include <unity.h>
#include <cstring>
#include <cstdio>
#include <string>
#include "../../nfo.h"

static uint32_t seq = 0;
static uint32_t fakeRng(uint32_t n) { return n ? (seq++ % n) : 0; }

// Concatenate the interior (non-border) columns of every emitted row into one string, rows
// separated by '\n' so a name that happens to sit at a row edge can never fuse with the next
// row's leading text and produce a false substring match.
static std::string flattenGrid(uint8_t grid[][NFO_COLS], int n) {
  std::string out;
  for (int r = 0; r < n; r++) {
    for (int c = 1; c < NFO_COLS - 1; c++) out += (char)grid[r][c];
    out += '\n';
  }
  return out;
}

// Every emitted line is exactly NFO_COLS and carries its border bytes. The generator fills
// rather than truncates -- two clipped lines got through the design draft precisely because
// nothing checked this.
void test_every_line_is_full_width() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  for (int trial = 0; trial < 40; trial++) {
    seq = trial;
    memset(grid, 0, sizeof grid);
    GreetzState s; greetzInit(s, fakeRng);
    int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
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

// The greets run packs names comma-separated until the row is full. If the packer ever
// regressed to one name per row the box would still be well-formed and every other test would
// still pass, so assert the packing actually happened.
void test_greets_are_comma_packed() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState s; greetzInit(s, fakeRng);
  int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
  TEST_ASSERT_TRUE(flattenGrid(grid, n).find(", ") != std::string::npos);
}

// The greets run is 8-26 rows depending on how the shuffle packs (26 emitted names --
// GREETZ_NAME_COUNT less the two the header credits); the grid must never exceed
// NFO_MAX_LINES for ANY shuffle, or nfoBuild overruns the caller's buffer.
//
// The clamp is memory-safe but not content-safe, so also assert the LAST emitted row is the
// closing rule: if the roster ever outgrew the bound, row() would pile the tail onto that one
// row and the box would end mid-sentence with every width/border check still green.
void test_line_count_within_bounds() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  int minSeen = NFO_MAX_LINES, maxSeen = 0;
  for (int trial = 0; trial < 500; trial++) {
    seq = trial * 7;
    GreetzState s; greetzInit(s, fakeRng);
    int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
    TEST_ASSERT_GREATER_THAN_INT(NFO_FIXED_LINES, n);          // fixed lines plus >=1 greets row
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_MAX_LINES, n);
    TEST_ASSERT_EQUAL_UINT8(NFO_BL, grid[n - 1][0]);
    TEST_ASSERT_EQUAL_UINT8(NFO_BR, grid[n - 1][NFO_COLS - 1]);
    if (n < minSeen) minSeen = n;
    if (n > maxSeen) maxSeen = n;
  }
  printf("[test_nfo] observed line count range over 500 shuffles: %d..%d (bound %d)\n",
         minSeen, maxSeen, NFO_MAX_LINES);
}

// Every handle must fit a greets row on its own, or a name silently vanishes when the packer
// flushes and the next name still doesn't fit.
void test_every_name_fits_a_solo_row() {
  for (int i = 0; i < GREETZ_NAME_COUNT; i++)
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_GREET_W, (int)strlen(GREETZ_NAMES[i]));
}

// Both credited names must fit the release block's value column, which is narrower than the
// interior. snprintf would truncate them rather than overflow -- silently.
void test_credits_fit_the_value_column() {
  TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_VALUE_W, (int)strlen(NFO_SUPPLY));
  TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_VALUE_W, (int)strlen(NFO_CRACKED));
}

// maxLines <= 0 must return 0 lines rather than let row()'s `grid[maxLines-1]` go negative
// and write out of bounds. Nothing called nfoBuild with a non-positive bound until the crawl
// renderer landed, so this guard has no caller to protect it from regressing silently.
void test_zero_max_lines_returns_zero() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState s; greetzInit(s, fakeRng);
  TEST_ASSERT_EQUAL_INT(0, nfoBuild(s, grid, 0, fakeRng));
}

// Gap 1: the crawl used to pick only 9 of 28 handles into a local bag. Every build must now
// place every handle exactly once, EXCEPT the two the release header already credits by name
// (NFO_SUPPLY / NFO_CRACKED) -- listing those in the brackets too would print them twice in
// one box.
//
// Scoped to the greets run alone -- from the "G R E E T S" heading to the GREETZ_BEER line
// that closes the roster. Both ends matter: the release block above credits SUPPLY/CRACKED,
// and the sign-off below names the cracker again on purpose, so neither may be in the search.
void test_every_name_appears_exactly_once() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState s; greetzInit(s, fakeRng);
  int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
  std::string full = flattenGrid(grid, n);
  size_t scopeStart = full.find("G R E E T S");
  size_t scopeEnd   = full.find(GREETZ_BEER);
  TEST_ASSERT_TRUE(scopeStart != std::string::npos);
  TEST_ASSERT_TRUE(scopeEnd   != std::string::npos && scopeEnd > scopeStart);
  std::string out = full.substr(scopeStart, scopeEnd - scopeStart);
  for (int i = 0; i < GREETZ_NAME_COUNT; i++) {
    const char* name = GREETZ_NAMES[i];
    bool credited = (strcmp(name, NFO_SUPPLY) == 0) || (strcmp(name, NFO_CRACKED) == 0);
    size_t first = out.find(name);
    if (credited) {   // named in the header, so it must NOT also appear in the bracket list
      TEST_ASSERT_EQUAL_size_t_MESSAGE(std::string::npos, first, name);
      continue;
    }
    TEST_ASSERT_TRUE_MESSAGE(first != std::string::npos, name);
    TEST_ASSERT_EQUAL_size_t(std::string::npos, out.find(name, first + strlen(name)));
  }
}

// The two credited names must still be DRAWN from the shared bag even though they are not
// emitted. The bag is shared with the marquee, so drawing fewer than GREETZ_NAME_COUNT would
// leave it in a state greetzBuild would never produce and desynchronise the two renderings.
// Proxy: an identical rng stream must leave the bag at the same point either way, which shows
// up as nfoBuild and greetzBuild agreeing on how many picks they consume.
void test_bag_advances_by_the_full_name_count() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState a; greetzInit(a, fakeRng);
  GreetzState b; greetzInit(b, fakeRng);
  seq = 99; nfoBuild(a, grid, NFO_MAX_LINES, fakeRng);
  char buf[GREETZ_BUF];
  seq = 99; greetzBuild(b, buf, sizeof buf, fakeRng);
  TEST_ASSERT_EQUAL_INT_MESSAGE(b.bag.n, a.bag.n, "bag left at a different depth than the marquee");
}

// Gap 2: nfoBuild used to run its own local ShuffleBag and never touch GreetzState, so the
// crawl neither showed the RiverDaddy shout-out nor advanced its schedule. Mirrors
// test_greetz.cpp's test_riverdaddy_fires_every_three_to_five_loops, but against nfoBuild's
// grid output instead of greetzBuild's flat string.
void test_riverdaddy_fires_on_schedule() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState s; greetzInit(s, fakeRng);
  int lastFire = 0;
  int fires = 0;
  for (int loop = 1; loop <= 200; loop++) {
    int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
    std::string out = flattenGrid(grid, n);
    bool daddy = out.find("RIVERDADDY") != std::string::npos;
    bool side  = out.find("RIVERSIDE")  != std::string::npos;
    TEST_ASSERT_TRUE(daddy != side);              // exactly one of the two, always
    if (daddy) {
      int gap = loop - lastFire;
      TEST_ASSERT_TRUE_MESSAGE(gap >= 3 && gap <= 5, "gap out of [3,5]");
      lastFire = loop; fires++;
    }
  }
  TEST_ASSERT_TRUE(fires > 30);                   // it actually fires, repeatedly
}

void setUp() {} void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_line_is_full_width);
  RUN_TEST(test_greets_are_comma_packed);
  RUN_TEST(test_line_count_within_bounds);
  RUN_TEST(test_every_name_fits_a_solo_row);
  RUN_TEST(test_credits_fit_the_value_column);
  RUN_TEST(test_zero_max_lines_returns_zero);
  RUN_TEST(test_every_name_appears_exactly_once);
  RUN_TEST(test_bag_advances_by_the_full_name_count);
  RUN_TEST(test_riverdaddy_fires_on_schedule);
  return UNITY_END();
}
