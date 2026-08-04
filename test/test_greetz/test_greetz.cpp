#include <unity.h>
#include <cstring>
#include <string>
#include <set>
#include "../../greetz.h"
#include "../../vga_font.h"
#include "../../animations.h"

// Deterministic LCG so the tests pin exact sequences. Same shape as the rng the device injects.
static uint32_t g_seed = 12345;
static uint32_t testRng(uint32_t n) {
  g_seed = g_seed * 1103515245u + 12345u;
  return n ? (g_seed >> 16) % n : 0;
}

static std::string buildOne(GreetzState& s) {
  char buf[GREETZ_BUF];
  size_t len = greetzBuild(s, buf, sizeof(buf), testRng);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len < GREETZ_BUF);
  TEST_ASSERT_EQUAL_size_t(len, strlen(buf));   // NUL-terminated, length agrees
  return std::string(buf);
}

// Every loop is a full permutation: all 26 handles present, each exactly once.
void test_loop_contains_every_name_once() {
  GreetzState s; greetzInit(s, testRng);
  std::string out = buildOne(s);
  for (int i = 0; i < GREETZ_NAME_COUNT; i++) {
    const char* n = GREETZ_NAMES[i];
    size_t first = out.find(n);
    TEST_ASSERT_TRUE_MESSAGE(first != std::string::npos, n);
    TEST_ASSERT_EQUAL_size_t(std::string::npos, out.find(n, first + strlen(n)));
  }
}

// Casing is load-bearing -- these are how their owners spell them.
void test_handle_casing_is_preserved() {
  GreetzState s; greetzInit(s, testRng);
  std::string out = buildOne(s);
  TEST_ASSERT_TRUE(out.find("Bitquark")    != std::string::npos);
  TEST_ASSERT_TRUE(out.find("HellGiraffe") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("Crick3t")     != std::string::npos);
  TEST_ASSERT_TRUE(out.find("bse")         != std::string::npos);
  TEST_ASSERT_TRUE(out.find("jekylzz")     != std::string::npos);
  // and NOT uppercased
  TEST_ASSERT_TRUE(out.find("BITQUARK")    == std::string::npos);
  TEST_ASSERT_TRUE(out.find("HELLGIRAFFE") == std::string::npos);
}

// The fixed lines, including the two extras the owner kept from the HTML.
void test_fixed_segments_present() {
  GreetzState s; greetzInit(s, testRng);
  std::string out = buildOne(s);
  TEST_ASSERT_TRUE(out.find("NEKORAMENGANG (NRG)")        != std::string::npos);
  TEST_ASSERT_TRUE(out.find("IN LOVING MEMORY OF BIND")   != std::string::npos);
  TEST_ASSERT_TRUE(out.find("AND ANYONE WE FORGOT!")      != std::string::npos);
  TEST_ASSERT_TRUE(out.find("OVER AND OUT ------>")       != std::string::npos);
  TEST_ASSERT_TRUE(out.find("THE WALL OF SHEEP")          != std::string::npos);
  // declined content must never appear
  TEST_ASSERT_TRUE(out.find("LAMERS")                     == std::string::npos);
}

// Leading pad is >= one screen width, which is what makes the wrap rebuild invisible.
void test_leading_pad() {
  GreetzState s; greetzInit(s, testRng);
  std::string out = buildOne(s);
  TEST_ASSERT_TRUE(GREETZ_PAD >= 15);              // 240px / 16px advance
  for (int i = 0; i < GREETZ_PAD; i++) TEST_ASSERT_EQUAL_CHAR(' ', out[i]);
  TEST_ASSERT_NOT_EQUAL(' ', out[GREETZ_PAD]);
}

void test_consecutive_loops_differ_in_order() {
  GreetzState s; greetzInit(s, testRng);
  std::string a = buildOne(s), b = buildOne(s);
  TEST_ASSERT_TRUE(a != b);
}

// The shufflebag's `last` guard: a name must not end one loop and open the next.
void test_no_name_repeats_across_the_seam() {
  GreetzState s; greetzInit(s, testRng);
  std::string prev = buildOne(s);
  for (int loop = 0; loop < 30; loop++) {
    std::string cur = buildOne(s);
    // last handle of prev's NAMES run vs first handle of cur's
    for (int i = 0; i < GREETZ_NAME_COUNT; i++) {
      const char* n = GREETZ_NAMES[i];
      bool endsPrev = prev.find(std::string(n) + "   :::   SPECIAL") != std::string::npos;
      bool startsCur = cur.find(std::string("ORDER   :::   ") + n) != std::string::npos;
      TEST_ASSERT_FALSE_MESSAGE(endsPrev && startsCur, n);
    }
    prev = cur;
  }
}

// "swaps his name for RiverDaddy every 3-5 times it plays"
void test_riverdaddy_fires_every_three_to_five_loops() {
  GreetzState s; greetzInit(s, testRng);
  int lastFire = 0;
  int fires = 0;
  for (int loop = 1; loop <= 200; loop++) {
    std::string out = buildOne(s);
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

// Every character the content can emit must have a baked glyph (ASCII 32..126).
void test_all_content_chars_are_printable_ascii() {
  GreetzState s; greetzInit(s, testRng);
  for (int loop = 0; loop < 40; loop++) {
    std::string out = buildOne(s);
    for (char c : out)
      TEST_ASSERT_TRUE_MESSAGE((unsigned char)c >= 32 && (unsigned char)c <= 126, &c);
  }
}

// A too-small buffer must truncate safely, never overrun.
void test_small_buffer_truncates_safely() {
  GreetzState s; greetzInit(s, testRng);
  char small[64];
  memset(small, 0x7F, sizeof(small));
  size_t len = greetzBuild(s, small, sizeof(small), testRng);
  TEST_ASSERT_TRUE(len < sizeof(small));
  TEST_ASSERT_EQUAL_CHAR('\0', small[len]);
  TEST_ASSERT_EQUAL_size_t(len, strlen(small));
}

void test_font_covers_printable_ascii() {
  TEST_ASSERT_EQUAL_UINT8(32,  VGA_FONT_FIRST);
  TEST_ASSERT_EQUAL_UINT8(255, VGA_FONT_LAST);
  TEST_ASSERT_EQUAL_INT(224, VGA_FONT_LAST - VGA_FONT_FIRST + 1);
  TEST_ASSERT_EQUAL_INT(8,  VGA_FONT_W);
  TEST_ASSERT_EQUAL_INT(16, VGA_FONT_H);
  TEST_ASSERT_EQUAL_INT(5,  VGA_MIP_W);
  TEST_ASSERT_EQUAL_INT(8,  VGA_MIP_H);
}

// The far-field atlas is generated from 2x2 source-dot blocks. Every result is true coverage
// (0..4), space remains empty, and CP437's synthetic ninth dot participates in the filter. The
// last cases are the generator trap: row 5 is paired with blank row 4, so its joined ninth-dot
// bucket must carry two horizontal samples (coverage 2, not 1); a solid block remains 4.
void test_font_mip_is_bounded_and_preserves_box_joins() {
  for (int c = VGA_FONT_FIRST; c <= VGA_FONT_LAST; c++)
    for (int y = 0; y < VGA_FONT_H; y++)
      for (int x = 0; x < VGA_CELL_W; x++)
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(4, vgaMipCoverage((uint8_t)c, x, y));

  for (int y = 0; y < VGA_FONT_H; y++)
    for (int x = 0; x < VGA_CELL_W; x++)
      TEST_ASSERT_EQUAL_UINT8(0, vgaMipCoverage(' ', x, y));

  TEST_ASSERT_EQUAL_UINT8(2, vgaMipCoverage(0xCD, 8, 5));  // joined double horizontal rule
  TEST_ASSERT_EQUAL_UINT8(4, vgaMipCoverage(0xDB, 8, 5));  // joined full block
  TEST_ASSERT_EQUAL_UINT8(0, vgaMipCoverage('*',  8, 7));  // ordinary glyph has no ninth dot
}

// The crawl's hot path expands a whole character row once and then mask-tests it. Keep that packed
// representation exactly equivalent to vgaCellBit for every baked glyph, row, and ninth-dot case.
void test_packed_cell_rows_match_individual_dot_lookup() {
  for (int c = VGA_FONT_FIRST; c <= VGA_FONT_LAST; c++)
    for (int y = 0; y < VGA_FONT_H; y++) {
      uint16_t packed = vgaCellRowBits((uint8_t)c, y);
      TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, packed & 0xFE00, "packed row escaped its nine-dot cell");
      for (int x = 0; x < VGA_CELL_W; x++)
        TEST_ASSERT_EQUAL_INT(vgaCellBit((uint8_t)c, x, y), (packed & (1u << (8 - x))) != 0);
    }
}

// 0xC9 is CP437 double-corner. Baked through the Unicode cmap without a CP437 map it
// would be 'E-acute' instead: an accent mark means ink in the TOP rows, and a corner
// means the top rows are empty. That difference is the whole test.
void test_cp437_box_corner_not_latin1() {
  const uint8_t* g = VGA_FONT[0xC9 - VGA_FONT_FIRST];
  uint8_t topInk = 0;
  for (int r = 0; r < 4; r++) topInk |= g[r];
  TEST_ASSERT_EQUAL_UINT8(0, topInk);            // no accent above the box
  uint8_t bodyInk = 0;
  for (int r = 7; r < 12; r++) bodyInk |= g[r];
  TEST_ASSERT_NOT_EQUAL(0, bodyInk);             // the corner itself has ink
}

// Space and CP437 0xFF are the only glyphs that may be entirely blank. 0xFF is CP437's
// no-break space (decodes to U+00A0), which this font renders identically to a space --
// a real property of the codepage, not a rasterizer miss. A blank anything else means the
// rasterizer silently missed it and that character would scroll past as a hole.
void test_only_space_is_blank() {
  for (int c = VGA_FONT_FIRST; c <= VGA_FONT_LAST; c++) {
    int ink = 0;
    for (int r = 0; r < VGA_FONT_H; r++) ink |= VGA_FONT[c - VGA_FONT_FIRST][r];
    if (c == ' ' || c == 0xFF) TEST_ASSERT_EQUAL_INT_MESSAGE(0, ink, "expected-blank glyph is inked");
    else                       TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ink, "non-space glyph is blank");
  }
}

// Pin the bit order. LSB-first would mirror every glyph inside its byte -- a subtle horizontal
// flip that is easy to miss on a moving scroller. 'A' is symmetric, so use 'F', which is not.
void test_bit_order_is_msb_first() {
  const uint8_t* F = VGA_FONT['F' - VGA_FONT_FIRST];
  int firstRow = -1;
  for (int r = 0; r < VGA_FONT_H; r++) if (F[r]) { firstRow = r; break; }
  TEST_ASSERT_TRUE(firstRow >= 0);
  // 'F' has its stem on the left, so the leftmost column (bit 7) is set on every inked row.
  TEST_ASSERT_TRUE_MESSAGE(F[firstRow] & 0x80, "top row of 'F' should start at the left column");
}

// Descenders must live inside the cell, not fall off the bottom row.
void test_descenders_reach_the_lower_rows() {
  for (char c : {'g', 'p', 'y', 'q', 'j'}) {
    const uint8_t* G = VGA_FONT[c - VGA_FONT_FIRST];
    int lastRow = -1;
    for (int r = 0; r < VGA_FONT_H; r++) if (G[r]) lastRow = r;
    TEST_ASSERT_TRUE_MESSAGE(lastRow >= 12, "descender should extend below the baseline");
  }
}

// The registry's three coupled constants have no other cross-check. Both failure modes are
// silent at compile time, and both ship a mode you cannot actually reach.
void test_registry_is_self_consistent() {
  int playable = 0;
  for (int i = 0; i < REGISTRY_COUNT; i++) {
    TEST_ASSERT_NOT_NULL_MESSAGE(ANIMS[i].name, "zero-filled catalog slot");
    TEST_ASSERT_TRUE_MESSAGE(ANIMS[i].name[0] != '\0', "empty catalog name");
    bool isDebug = (strcmp(ANIMS[i].group, "debug") == 0);
    if (!isDebug) {
      playable++;
      TEST_ASSERT_TRUE_MESSAGE(isPlayableId(ANIMS[i].id), "catalog entry missing its mask bit");
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(PLAYABLE_ENTRY_COUNT, playable, "count vs catalog disagree");
}

void test_greetz_id_is_reachable() {
  TEST_ASSERT_EQUAL_UINT8(47, GREETZ_ID);
  TEST_ASSERT_TRUE(isPlayableId(GREETZ_ID));
  // Greetz is no longer the last id -- the ported lab effects (49..55) are. ANIM_COUNT tracks whatever is.
  TEST_ASSERT_EQUAL_INT(ATLAS_BASE + ATLAS_COUNT, ANIM_COUNT);
  TEST_ASSERT_TRUE(isPlayableId(GIF_ID));
  TEST_ASSERT_TRUE(isPlayableId(ATLAS_BASE + ATLAS_COUNT - 1));  // 55 = Fermat Spiral, the new last playable
  // ids already in the field must not have moved
  TEST_ASSERT_EQUAL_UINT8(38, AUDIO_BASE);
  TEST_ASSERT_EQUAL_UINT8(42, DEBUG_ID);
  TEST_ASSERT_EQUAL_UINT8(45, SWIRL_ID);
  TEST_ASSERT_EQUAL_UINT8(46, TREATCAT_ID);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_contains_every_name_once);
  RUN_TEST(test_handle_casing_is_preserved);
  RUN_TEST(test_fixed_segments_present);
  RUN_TEST(test_leading_pad);
  RUN_TEST(test_consecutive_loops_differ_in_order);
  RUN_TEST(test_no_name_repeats_across_the_seam);
  RUN_TEST(test_riverdaddy_fires_every_three_to_five_loops);
  RUN_TEST(test_all_content_chars_are_printable_ascii);
  RUN_TEST(test_small_buffer_truncates_safely);
  RUN_TEST(test_font_covers_printable_ascii);
  RUN_TEST(test_font_mip_is_bounded_and_preserves_box_joins);
  RUN_TEST(test_packed_cell_rows_match_individual_dot_lookup);
  RUN_TEST(test_cp437_box_corner_not_latin1);
  RUN_TEST(test_only_space_is_blank);
  RUN_TEST(test_bit_order_is_msb_first);
  RUN_TEST(test_descenders_reach_the_lower_rows);
  RUN_TEST(test_registry_is_self_consistent);
  RUN_TEST(test_greetz_id_is_reachable);
  return UNITY_END();
}
