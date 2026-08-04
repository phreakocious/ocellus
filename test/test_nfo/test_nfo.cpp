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

// The bracket block is 13-26 rows depending on how the shuffle pairs up (26 emitted names --
// GREETZ_NAME_COUNT less the two the header credits -- so ceil(26/2)=13 if everything pairs,
// 26 if nothing does); the grid must never exceed NFO_MAX_LINES for ANY shuffle, or nfoBuild
// overruns the caller's buffer.
void test_line_count_within_bounds() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  int minSeen = NFO_MAX_LINES, maxSeen = 0;
  for (int trial = 0; trial < 500; trial++) {
    seq = trial * 7;
    GreetzState s; greetzInit(s, fakeRng);
    int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(30 + (GREETZ_NAME_COUNT - 2 + 1) / 2, n);   // 30 fixed + >=13 bracket
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_MAX_LINES, n);
    if (n < minSeen) minSeen = n;
    if (n > maxSeen) maxSeen = n;
  }
  printf("[test_nfo] observed line count range over 500 shuffles: %d..%d (bound %d)\n",
         minSeen, maxSeen, NFO_MAX_LINES);
}

// Every handle must fit a solo bracket row, or a name silently vanishes.
void test_every_name_fits_a_solo_row() {
  for (int i = 0; i < GREETZ_NAME_COUNT; i++)
    TEST_ASSERT_LESS_OR_EQUAL_INT(NFO_COLS - 2, (int)strlen(GREETZ_NAMES[i]) + 4);
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
// Scoped to start after "for their support:" (the line right before the bracket block) so the
// header's own "Supply :" / "Cracked :" credits are outside the search.
void test_every_name_appears_exactly_once() {
  uint8_t grid[NFO_MAX_LINES][NFO_COLS];
  GreetzState s; greetzInit(s, fakeRng);
  int n = nfoBuild(s, grid, NFO_MAX_LINES, fakeRng);
  std::string full = flattenGrid(grid, n);
  size_t scopeStart = full.find("for their support");
  TEST_ASSERT_TRUE(scopeStart != std::string::npos);
  std::string out = full.substr(scopeStart);
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
  RUN_TEST(test_pair_predicate_accounts_for_brackets);
  RUN_TEST(test_line_count_within_bounds);
  RUN_TEST(test_every_name_fits_a_solo_row);
  RUN_TEST(test_zero_max_lines_returns_zero);
  RUN_TEST(test_every_name_appears_exactly_once);
  RUN_TEST(test_bag_advances_by_the_full_name_count);
  RUN_TEST(test_riverdaddy_fires_on_schedule);
  return UNITY_END();
}
