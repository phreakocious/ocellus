#include <unity.h>
#include <cstring>
#include "../../audio.h"

static SynthState seeded() {
  SynthState st; memset(&st, 0, sizeof st);
  st.rng = 0xC0FFEE01;                 // fixed seed -> deterministic
  return st;
}

// The spectrum changes over time (not a frozen buffer) and stays in range.
void test_spectrum_moves_and_in_range() {
  SynthState st = seeded();
  SbStreamMags a, b; bool beat, snare, spark;
  synthAudio(a, st, beat, snare, spark, 0);
  for (uint32_t t = 33; t <= 3000; t += 33) synthAudio(b, st, beat, snare, spark, t);
  bool differs = false;
  for (int i = 0; i < NUM_FREQS; i++) if (a.spectrogram[i] != b.spectrogram[i]) differs = true;
  TEST_ASSERT_TRUE(differs);           // it walked
  // uint16_t can't exceed range; assert it is not stuck all-zero either
  uint32_t sum = 0; for (int i = 0; i < NUM_FREQS; i++) sum += b.spectrogram[i];
  TEST_ASSERT_TRUE(sum > 0);
}

// Beats fire, and successive beat intervals are within [400,1200] ms and are NOT all identical.
void test_beats_nonperiodic_in_bounds() {
  SynthState st = seeded();
  SbStreamMags o; bool beat, snare, spark;
  uint32_t last = 0; int firstGap = -1; bool varies = false; int beats = 0;
  for (uint32_t t = 0; t <= 20000; t += 10) {   // 10ms granularity
    synthAudio(o, st, beat, snare, spark, t);
    if (beat) {
      if (last) {
        int gap = (int)(t - last);
        // beats fire at ~30ms tick boundaries, so a [400,1200]ms schedule reports gaps up to ~1230
        TEST_ASSERT_TRUE(gap >= 400 && gap <= 1230);
        if (firstGap < 0) firstGap = gap; else if (gap != firstGap) varies = true;
      }
      last = t; beats++;
    }
  }
  TEST_ASSERT_TRUE(beats > 10);        // it actually fires
  TEST_ASSERT_TRUE(varies);            // not metronomic
}

// FPS-independence: reaching the same `now` in coarse vs fine steps yields the SAME spectrum.
// Both runs must init at the same `now` (here 0) so their fixed-tick phase aligns.
void test_fps_independent() {
  SynthState coarse = seeded(), fine = seeded();
  SbStreamMags oc, of; bool b, s, sp;
  synthAudio(oc, coarse, b, s, sp, 0);          // common init point -> aligned tick phase
  synthAudio(of, fine,   b, s, sp, 0);
  for (uint32_t t = 100; t <= 3000; t += 100) synthAudio(oc, coarse, b, s, sp, t);  // coarse
  for (uint32_t t = 10;  t <= 3000; t += 10)  synthAudio(of, fine,   b, s, sp, t);  // fine
  for (int i = 0; i < NUM_FREQS; i++)
    TEST_ASSERT_EQUAL_UINT16(oc.spectrogram[i], of.spectrogram[i]);
}

// Dynamics: per-frame total energy must span quiet and loud moments (not sit at one level).
void test_has_dynamic_range() {
  SynthState st = seeded();
  SbStreamMags o; bool b, s, sp;
  uint32_t lo = 0xFFFFFFFF, hi = 0;
  for (uint32_t t = 0; t <= 40000; t += 33) {   // ~40s of frames
    synthAudio(o, st, b, s, sp, t);
    uint32_t sum = 0; for (int i = 0; i < NUM_FREQS; i++) sum += o.spectrogram[i];
    if (sum < lo) lo = sum;
    if (sum > hi) hi = sum;
  }
  TEST_ASSERT_TRUE(hi > lo * 3);                 // loud frames dwarf the quiet ones -> real dynamics
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_spectrum_moves_and_in_range);
  RUN_TEST(test_beats_nonperiodic_in_bounds);
  RUN_TEST(test_fps_independent);
  RUN_TEST(test_has_dynamic_range);
  return UNITY_END();
}
