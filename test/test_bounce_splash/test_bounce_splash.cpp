#include <unity.h>
#include <cmath>
#include "../../bounce_splash.h"
#include "../../vga_font.h"

using namespace bounce;

static float distToCenter(int x, int y) {
  float dx = x - 120.0f, dy = y - 120.0f;
  return sqrtf(dx * dx + dy * dy);
}

// The whole point of reverse simulation: every letter's LAST playback frame is exactly its slot,
// so a random-looking entry always lands perfectly in order. If this holds, the reveal can't cheat.
void test_lands_exactly_on_ordered_slots() {
  Trajectories T;
  compute(T, "abcde", 5, 0xC0FFEE);
  for (int i = 0; i < T.count; i++) {
    int last = T.frames[i] - 1;
    TEST_ASSERT_EQUAL_INT16(T.slotX[i], T.x[i][last]);
    TEST_ASSERT_EQUAL_INT16(T.slotY[i], T.y[i][last]);
  }
}

// Slots read left-to-right in name order (i increasing => x increasing) -> "the right order".
void test_slots_left_to_right() {
  Trajectories T;
  compute(T, "ocellus!", 8, 42);
  for (int i = 1; i < T.count; i++)
    TEST_ASSERT_TRUE(T.slotX[i] > T.slotX[i - 1]);
}

// Letters actually come in from off the screen (entry frame is beyond the round edge), and land
// inside it. Guards against a sim that never left (would enter mid-screen) or a bad cap.
void test_enters_offscreen_lands_onscreen() {
  Trajectories T;
  compute(T, "kitten", 6, 12345);
  for (int i = 0; i < T.count; i++) {
    TEST_ASSERT_TRUE(distToCenter(T.x[i][0], T.y[i][0]) >= 120.0f);          // entry off-screen
    int last = T.frames[i] - 1;
    TEST_ASSERT_TRUE(distToCenter(T.x[i][last], T.y[i][last]) <= 120.0f);    // slot on-screen
  }
}

// Animation length stays in a sane window (~2.5s..~6s at the real ~31ms/frame -- render + the
// ~14ms framebuffer flush + delay(16), not the 16ms/frame a frame count alone would suggest).
// Doubles as a tuning guard: if physics constants drift and trajectories balloon or collapse,
// this trips. 4000 seeds so the sweep actually reaches the MAX_FRAMES cap condition (Fix 1) --
// at seed 3635 / len 13, one letter legitimately runs all the way to the cap (frames[i] ==
// MAX_FRAMES), which is exactly the case Fix 1's `m < MAX_FRAMES` guard makes safe instead of a
// one-past-the-end write. That means the old `<= 190` / `< MAX_FRAMES` bounds (calibrated against
// a 40-seed sweep that never reached the cap) no longer hold; the real invariant is the per-letter
// bound below.
// The per-letter bound is necessary but NOT sufficient as a tuning guard, and on its own it is a
// tautology: `while (m < MAX_FRAMES)` makes `1 <= frames[i] <= MAX_FRAMES` true by construction,
// so no drift in gravity, restitution or the arc geometry could ever trip it. It earns its place
// only as a regression guard for the one-past-the-end write that used to be possible here.
//
// The tuning guard is the aggregate below. Hitting the cap is legitimate but must stay RARE: a
// capped trajectory is truncated, so that letter pops in mid-screen instead of flying in.
// Measured on the shipped constants over exactly this sweep (2026-08-02): 1 cap hit in 29,988
// letters, 6 seeds past 190 frames, mean length 100.4, shortest 40. The thresholds sit well clear
// of those, so ordinary variation passes while physics that BALLOONS (cap hits and long runs
// explode) or COLLAPSES (mean and minimum fall) trips them. Re-measure before retuning them.
void test_animation_length_sane() {
  long capHits = 0, over190 = 0, letters = 0, sum = 0;
  int shortest = MAX_FRAMES;
  for (uint32_t seed = 1; seed <= 4000; seed++) {
    Trajectories T;
    compute(T, "phreakociousmeow", (int)(seed % 12) + 2, seed);
    TEST_ASSERT_TRUE(T.maxFrames >= 20);
    TEST_ASSERT_TRUE(T.maxFrames <= MAX_FRAMES);   // in-bounds: guards the OOB write, not the tuning
    if (T.maxFrames > 190) over190++;
    for (int i = 0; i < T.count; i++) {
      TEST_ASSERT_TRUE(T.frames[i] >= 1 && T.frames[i] <= MAX_FRAMES);
      letters++; sum += T.frames[i];
      if (T.frames[i] == MAX_FRAMES) capHits++;
      if (T.frames[i] < shortest) shortest = T.frames[i];
    }
  }
  double mean = (double)sum / (double)letters;
  TEST_ASSERT_TRUE(capHits  <= 10);                    // measured 1
  TEST_ASSERT_TRUE(over190  <= 40);                    // measured 6
  TEST_ASSERT_TRUE(shortest >= 25);                    // measured 40
  TEST_ASSERT_TRUE(mean >= 80.0 && mean <= 125.0);     // measured 100.4
}

// Same seed reproduces (deterministic LCG, not Arduino random()).
void test_deterministic() {
  Trajectories A, B;
  compute(A, "seventy", 7, 999);
  compute(B, "seventy", 7, 999);
  for (int i = 0; i < A.count; i++) {
    TEST_ASSERT_EQUAL_INT(A.frames[i], B.frames[i]);
    TEST_ASSERT_EQUAL_INT16(A.x[i][0], B.x[i][0]);
  }
}

// Long names clamp to the buffer instead of overflowing.
void test_clamps_letter_count() {
  Trajectories T;
  compute(T, "phreakociousmeow", 40, 7);
  TEST_ASSERT_EQUAL_INT(MAX_LETTERS, T.count);
}

// The ladder: bigger glyphs for short names, shrinking only as far as a long name forces.
// Boundaries are the point -- midpoints alone would not catch an off-by-one in the loop bound.
// Ink-kerned spacing makes the ladder name-dependent, so it is pinned for reference-name
// prefixes. The kerning bought back every rung the cell-box interim fix cost: these are the
// same boundaries the original width-spaced ladder had, now without the overlap that ladder
// hid.
void test_scale_ladder_matches_name_length() {
  const char* ref = "phreakociousmeow";
  const int expect[17] = { 0, 4,4,4,4,4,4, 3,3, 2,2,2,2,2,2, 1,1 };
  //  index:                  1 2 3 4 5 6  7 8  9 ...    14  15,16
  for (int len = 1; len <= 16; len++)
    TEST_ASSERT_EQUAL_INT(expect[len], scaleFor(ref, len));
  // Wide ink packs looser than lowercase: capitals may drop a rung sooner, never climb higher.
  TEST_ASSERT_TRUE(scaleFor("WWWWWWWWWWWWWWWW", 16) <= scaleFor(ref, 16));
}

// The GLYPH BOX may never cross the round rim. The glow halo is allowed to, by owner decision
// (2026-08-02): reserving room for it would cost an 8-letter name a whole size step, and the
// worst overshoot is 1.37 px at scale 4 (0.03 px at 3x) -- a sliver of halo cut by the bezel.
// So this asserts the box, not the box+halo.
//
// This deliberately does NOT assert `slotDistance + glyphR <= 120`. That form is a TAUTOLOGY:
// geometryFor defines rArc = R - glyphR - 4 and computeSlots puts every slot at exactly rArc,
// so slotDistance + glyphR == 116.0 for ANY glyphR at all -- it would happily pass with the old
// GFX 5.0f*ts constant left in by mistake. Deriving the corners from the font dimensions
// instead breaks that circularity.
void test_glyph_corners_stay_inside_the_rim() {
  const char* ref = "phreakociousmeow";
  for (int len = 1; len <= MAX_LETTERS; len++) {
    Trajectories T;
    compute(T, ref, len, 4242);
    const int s    = geometryFor(ref, len).scale;
    const float hw = VGA_FONT_W * 0.5f * s;   // glyph box half-width  (halo excluded, see above)
    const float hh = VGA_FONT_H * 0.5f * s;   // glyph box half-height
    for (int i = 0; i < T.count; i++)
      for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
          TEST_ASSERT_TRUE(distToCenter((int)(T.slotX[i] + sx * hw),
                                        (int)(T.slotY[i] + sy * hh)) <= 120.0f);
  }
}

// Adjacent glyph INK may never overlap, and letters may never drift far apart either -- the
// two failure modes seen on glass the same day (2026-08-04): width-only spacing piled "Phre"
// into a heap up the left side; the cell-box interim fix then exiled the end letters behind a
// glyph-height of empty ascender/descender whitespace ("the p and s are far from the other
// letters"). Ink rects come from the same bitmaps the blitter draws, placed exactly as
// drawLetter places them (cell centered on the slot), so this asserts what the eye sees:
// no touching, no exile. The far bound is cell height + margin + rounding -- the worst a
// descender-meets-ascender pair (p over h) can legitimately need.
void test_adjacent_glyphs_snug_but_never_overlapping() {
  const char* names[] = { "phreakociousmeow", "phreakocious", "ocellus", "WWWWWWWWWWWWWWWW" };
  for (const char* name : names) {
    int full = 0; while (name[full]) full++;
    for (int len = 2; len <= full; len++) {
      Trajectories T;
      compute(T, name, len, 77);
      const int s = geometryFor(name, len).scale;
      for (int i = 1; i < T.count; i++) {
        InkBox A = inkBoxFor(name[i - 1]), B = inkBoxFor(name[i]);
        // ink rect edges in px, [x0,x1) x [y0,y1), cell centered on the slot
        float aL = T.slotX[i-1] + (A.x0 - 4) * s, aR = T.slotX[i-1] + (A.x1 + 1 - 4) * s;
        float aT = T.slotY[i-1] + (A.y0 - 8) * s, aB = T.slotY[i-1] + (A.y1 + 1 - 8) * s;
        float bL = T.slotX[i]   + (B.x0 - 4) * s, bR = T.slotX[i]   + (B.x1 + 1 - 4) * s;
        float bT = T.slotY[i]   + (B.y0 - 8) * s, bB = T.slotY[i]   + (B.y1 + 1 - 8) * s;
        TEST_ASSERT_TRUE_MESSAGE(aR <= bL || bR <= aL || aB <= bT || bB <= aT,
                                 "adjacent glyph ink overlaps");
        float dx = (float)(T.slotX[i] - T.slotX[i-1]), dy = (float)(T.slotY[i] - T.slotY[i-1]);
        TEST_ASSERT_TRUE_MESSAGE(dx * dx + dy * dy <=
                                 (float)((VGA_FONT_H + 3) * s) * ((VGA_FONT_H + 3) * s),
                                 "adjacent letters drifted apart");
      }
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lands_exactly_on_ordered_slots);
  RUN_TEST(test_slots_left_to_right);
  RUN_TEST(test_enters_offscreen_lands_onscreen);
  RUN_TEST(test_animation_length_sane);
  RUN_TEST(test_deterministic);
  RUN_TEST(test_clamps_letter_count);
  RUN_TEST(test_scale_ladder_matches_name_length);
  RUN_TEST(test_glyph_corners_stay_inside_the_rim);
  RUN_TEST(test_adjacent_glyphs_snug_but_never_overlapping);
  return UNITY_END();
}
