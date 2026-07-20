#include <unity.h>
#include <cstring>
#include <cmath>
#include "../../audio.h"

static SbStreamMags mk(uint16_t lo, uint16_t hi) {   // fill lows (bins 0-7) / highs (46-63)
  SbStreamMags m; memset(&m, 0, sizeof m);
  for (int i = 0;  i < 8;  i++) m.spectrogram[i]  = lo;
  for (int i = 46; i < 64; i++) m.spectrogram[i]  = hi;
  return m;
}

void test_bass_heavy_spectrum() {
  BloomParams p = bloomParamsFromMags(mk(60000, 0));
  TEST_ASSERT_TRUE(p.bass > p.high);
  TEST_ASSERT_TRUE(p.bass > 100);
}
void test_treble_heavy_spectrum() {
  BloomParams p = bloomParamsFromMags(mk(0, 60000));
  TEST_ASSERT_TRUE(p.high > p.bass);
}
void test_loud_bands_keep_travel() {
  // The bug the console's peak-hold exposed: the old avg/200 curve pinned any band averaging
  // >=51000 to a flat 255, so held bass bins had no dynamics left (exaggerated, motionless core)
  // and `high` cleared every level threshold. Loud must stay below max AND stay ordered.
  BloomParams loud = bloomParamsFromMags(mk(52000, 52000));
  BloomParams max  = bloomParamsFromMags(mk(65535, 65535));
  TEST_ASSERT_TRUE(loud.bass < 255);          // headroom left at the level the old curve clipped at
  TEST_ASSERT_TRUE(loud.bass < max.bass);     // ...and still travels above it
  TEST_ASSERT_EQUAL_UINT8(255, max.bass);     // full scale still reaches max
}
void test_beat_fires_once_then_refractory() {
  BeatDetector b;
  TEST_ASSERT_TRUE (b.update(200, 1000));   // first transient -> beat
  TEST_ASSERT_FALSE(b.update(10,  1010));   // drop, no beat
  TEST_ASSERT_FALSE(b.update(200, 1050));   // rise again but 50ms < 120ms refractory -> blocked
  TEST_ASSERT_FALSE(b.update(10,  1130));   // drop
  TEST_ASSERT_TRUE (b.update(200, 1150));   // rise, 150ms > 120ms -> beat
}
void test_rumble_does_not_beat() {
  // Breakdown rumble: loud, sustained, jittering by more than `rise` frame to frame (the squared
  // curve's slope is large up here). It must NOT machine-gun beats -- the baseline catches up to
  // sustained energy. Real kicks, arriving from a quiet floor, still must.
  BeatDetector b;
  int beats = 0;
  for (uint32_t t = 0; t < 3000; t += 27)                       // ~3s at the device's ~37fps
    if (b.update((uint8_t)(190 + (t / 27 % 2) * 25), t)) beats++;  // 190/215 jitter = +25 rise every frame
  TEST_ASSERT_TRUE(beats <= 3);                                 // a couple at onset, then silence

  BeatDetector k;                                               // same detector, real kicks: 2/s from a quiet floor
  int kicks = 0;
  for (uint32_t t = 0; t < 3000; t += 27)
    if (k.update((t % 500 < 60) ? 200 : 10, t)) kicks++;        // 60ms kick every 500ms
  TEST_ASSERT_TRUE(kicks >= 5);                                 // ~6 in 3s, not suppressed by the baseline
}
void test_limited_treble_still_sparks() {
  // Brickwalled 2000s-techno treble: the band SITS at 180 and hats only poke to 205. Bass's harsh
  // /4 margin wants a ~45 jump up there, so it silences every hat -- that's what dropped sparks to
  // 2-3% of frames. Treble's gentler /10 must still catch them. Warm up first so we measure steady
  // state, not the onset transient both detectors are entitled to fire on.
  BeatDetector spark{SPARK_HIGH_FLOOR, SPARK_HIGH_RISE, SPARK_REFRACTORY_MS, SPARK_MARGIN_DIV};
  BeatDetector bassRule;                                       // default (bass) margin divisor
  auto hat = [](uint32_t t) { return (uint8_t)(t % 200 < 30 ? 205 : 180); };   // a hat every 200ms
  for (uint32_t t = 0; t < 1000; t += 27) { spark.update(hat(t), t); bassRule.update(hat(t), t); }

  int sparks = 0, bassHits = 0;
  for (uint32_t t = 1000; t < 4000; t += 27) {
    if (spark.update(hat(t), t))    sparks++;
    if (bassRule.update(hat(t), t)) bassHits++;
  }
  TEST_ASSERT_TRUE(sparks >= 8);       // ~15 hats in 3s -- they land
  TEST_ASSERT_EQUAL_INT(0, bassHits);  // ...and the bass rule would have caught none of them
}
void test_stale_timeout() {
  TEST_ASSERT_FALSE(audioStale(1000, 1400));   // 400ms < 500
  TEST_ASSERT_TRUE (audioStale(1000, 1600));   // 600ms > 500
  TEST_ASSERT_FALSE(audioStale(1400, 1000));   // lastRx AHEAD of now (mid-frame packet) -> not stale, not underflow
  TEST_ASSERT_FALSE(audioStale(50,   10));     // millis wrap: now wrapped past lastRx -> still fresh
}
void test_wire_struct_size() {
  TEST_ASSERT_EQUAL_UINT32(172, sizeof(SbStreamMags));
}
void test_audio_bin_scale() {
  TEST_ASSERT_EQUAL_UINT8(0,   audioBin(0));            // silence -> off
  TEST_ASSERT_EQUAL_UINT8(255, audioBin(65535));        // full-scale -> max, no overflow
  TEST_ASSERT_UINT8_WITHIN(1, 128, audioBin(46341));    // ~70% in -> half out (locks the square curve)
  // The fix: the old /200 pinned everything >=51000 to white (0.778 knee) with zero
  // travel. The squared curve leaves headroom, so loud/bass bins still show dynamics.
  TEST_ASSERT_TRUE(audioBin(51000) < 255);
  TEST_ASSERT_TRUE(audioBin(51000) > audioBin(20000));  // monotonic
  TEST_ASSERT_TRUE(audioBin(20000) > audioBin(2000));   // monotonic
}
void test_lost_from_gaps_clean_stream() {
  uint16_t g[8] = {6, 6, 6, 6, 6, 6, 6, 6};   // every gap == the send interval -> nothing lost
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(g, 8));
}
void test_lost_from_gaps_jitter_is_not_loss() {
  uint16_t g[6] = {4, 5, 6, 6, 7, 8};   // +/-33% jitter around 6ms; every gap rounds to 1 interval
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(g, 6));
}
void test_lost_from_gaps_counts_multiples() {
  // 99 clean 10ms gaps + one 30ms gap (spans 3 intervals -> 2 packets missing) over 1020ms -> 2/s
  uint16_t g[100];
  for (int i = 0; i < 99; i++) g[i] = 10;
  g[99] = 30;                                  // sorted ascending: the outlier is last
  TEST_ASSERT_EQUAL_UINT16(2, lostFromGaps(g, 100));
}
void test_lost_from_gaps_degenerate() {
  uint16_t g[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(g, 4));      // all-zero gaps (p50 == 0) -> 0, no div-by-zero
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(g, 0));      // empty ring -> 0
  uint16_t tiny[3] = {1, 6, 6};                         // a sub-half-interval gap must not underflow
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(tiny, 3));
}
void test_lost_from_gaps_boundary_rounding() {
  // The 1.5x-interval line is where "jitter" becomes "loss": at p50=6, an 8ms gap rounds to
  // 1 span (0 lost) while 9ms rounds to 2 (1 lost over 603ms -> 2/s after rate rounding).
  uint16_t under[100], over[100];
  for (int i = 0; i < 99; i++) { under[i] = 6; over[i] = 6; }
  under[99] = 8; over[99] = 9;
  TEST_ASSERT_EQUAL_UINT16(0, lostFromGaps(under, 100));
  TEST_ASSERT_EQUAL_UINT16(2, lostFromGaps(over, 100));
}
void test_classify_sb_frames() {
  // The console broadcasts MORE than the spectrum: sync_settings at 12.5/s (one per 20
  // iterations of its 250Hz loop) and identify_main. Counting that healthy chatter as
  // "rejected" pegged the rej counter at its display cap within minutes of uptime and
  // destroyed its diagnostic value. Chatter is ignored; Junk is what rej exists for.
  SbStreamMags m; memset(&m, 0, sizeof m);
  m.ident[0] = 'S'; m.ident[1] = 'B'; m.ident[2] = 'C'; m.command_type = SB_CMD_STREAM_MAGS;
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Mags, (int)classifySbFrame((const uint8_t*)&m, sizeof m));

  uint8_t sync[40]     = {'S','B','C',0, 1};   // COMMAND_SYNC_SETTINGS (sized to match SbSyncSettings)
  uint8_t identify[6]  = {'S','B','C',0, 4};   // COMMAND_IDENTIFY_MAIN
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Sync, (int)classifySbFrame(sync, sizeof sync));
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Chatter, (int)classifySbFrame(identify, sizeof identify));

  uint8_t foreign[16]  = {'X','Y','Z',0, 5};              // not the console's protocol
  uint8_t unknown[16]  = {'S','B','C',0, SB_CMD_COUNT};   // future SB command = protocol drift
  uint8_t shortMags[16] = {'S','B','C',0, SB_CMD_STREAM_MAGS};  // right command, wrong size = layout drift
  uint8_t tiny[3]      = {'S','B','C'};                   // too short to even carry a command byte
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Junk, (int)classifySbFrame(foreign, sizeof foreign));
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Junk, (int)classifySbFrame(unknown, sizeof unknown));
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Junk, (int)classifySbFrame(shortMags, sizeof shortMags));
  TEST_ASSERT_EQUAL_INT((int)SbFrame::Junk, (int)classifySbFrame(tiny, sizeof tiny));
}
void test_sync_settings_wire_size() {
  TEST_ASSERT_EQUAL(40, (int)sizeof(SbSyncSettings));   // console p2p.h layout, padding included
}
void test_classify_sync_frames() {
  uint8_t buf[64]; memset(buf, 0, sizeof buf);
  buf[0] = 'S'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = 0;
  buf[4] = 1;                                                                  // COMMAND_SYNC_SETTINGS
  TEST_ASSERT_TRUE(classifySbFrame(buf, (int)sizeof(SbSyncSettings)) == SbFrame::Sync);
  TEST_ASSERT_TRUE(classifySbFrame(buf, 64) == SbFrame::Sync);                 // oversized ok (console growth)
  TEST_ASSERT_TRUE(classifySbFrame(buf, 39) == SbFrame::Junk);                 // truncated = layout drift
  buf[4] = 4;                                                                  // IDENTIFY_MAIN stays chatter
  TEST_ASSERT_TRUE(classifySbFrame(buf, 5)  == SbFrame::Chatter);
}
void test_bin_from_angle_geometry() {
  uint8_t t[256]; buildBinFromAngle(t);
  TEST_ASSERT_EQUAL_UINT8(0,  t[64]);    // screen bottom = bass
  TEST_ASSERT_EQUAL_UINT8(63, t[192]);   // screen top = treble
  for (int k = 1; k < 128; k++)          // mirrored about the vertical axis
    TEST_ASSERT_EQUAL_UINT8(t[(64 - k) & 255], t[(64 + k) & 255]);
  bool seen[64] = {};                    // every bin reachable (holes = silent dead bins)
  for (int a = 0; a < 256; a++) seen[t[a]] = true;
  for (int b = 0; b < 64; b++) TEST_ASSERT_TRUE(seen[b]);
}
void test_hue_slew_caps_rate() {
  HueSlew h;
  h.update(0, 1000);                       // first call snaps to target
  int p = h.update(100, 1100);             // +100 jump, dt 100ms -> at most 3 steps of travel
  TEST_ASSERT_TRUE(p >= 1 && p <= 3);
}
void test_hue_slew_converges_and_holds() {
  HueSlew h; h.update(0, 1000);
  int p = 0;
  for (uint32_t t = 1100; t <= 9000; t += 100) p = h.update(60, t);
  TEST_ASSERT_EQUAL_INT(60, p);            // 60 steps at 30/s: converged well within 8s, then holds
}
void test_hue_slew_wraps_shortest_path() {
  HueSlew h; h.update(250, 1000);          // snap to 250
  h.update(10, 1100);                      // target 10: shortest path is UP through the 256 wrap,
  int p = h.update(10, 1200);              // not 240 steps down
  TEST_ASSERT_TRUE(p >= 251 || p <= 10);
}
void test_hue_slew_dt_clamp() {
  HueSlew h; h.update(0, 1000);
  int p = h.update(100, 61000);            // a 60s gap must not buy 60s of travel: dt clamps to
  TEST_ASSERT_TRUE(p <= 6);                // 200ms -> at most 6 steps
}
void test_hue_slew_rejects_nonfinite() {
  // A NaN/Inf target (torn read, hostile radio frame) must be ignored, not integrated:
  // phase += NaN would stick forever (lastMs != 0 means no re-snap) and gHueSlew is
  // shared by all four audio modes -- one bad float would kill hue until reboot.
  HueSlew h; h.update(50, 1000);            // snap to 50
  TEST_ASSERT_EQUAL_INT(50, h.update(NAN, 1100));       // poisoned target ignored
  TEST_ASSERT_EQUAL_INT(50, h.update(INFINITY, 1200));  // likewise
  TEST_ASSERT_EQUAL_INT(53, h.update(53, 1300));        // recovers: dt 300 clamps to 200 -> maxStep 6, d=3
}
void test_note_hue_single_pitch() {
  NoteHue nh;
  uint16_t chroma[12] = {0};
  chroma[7] = 60000;                                  // lone pitch class 7 (G)
  for (int i = 0; i < 200; i++) nh.update(chroma);    // let the HOLD-0.9 EMAs settle
  // G sits at FIFTHS[7]=1 -> 1/12 of the wheel
  TEST_ASSERT_FLOAT_WITHIN(6.0f, 256.0f / 12.0f, nh.hue);
  TEST_ASSERT_TRUE(nh.gate > 0.5f);
}
void test_note_hue_flat_chromagram_gates_off() {
  NoteHue nh;
  uint16_t chroma[12];
  for (int i = 0; i < 12; i++) chroma[i] = 50000;     // atonal mush on a high pedestal
  for (int i = 0; i < 200; i++) nh.update(chroma);
  TEST_ASSERT_TRUE(nh.gate < 0.1f);
}
void test_note_hue_adjacent_notes_no_self_cancel() {
  // C+G together (pitch classes 0 and 7 = fifths slots 0 and 1): the vector mean must land
  // BETWEEN their wheel slots, not cancel into a wild angle.
  NoteHue nh;
  uint16_t chroma[12] = {0};
  chroma[0] = 60000; chroma[7] = 60000;
  for (int i = 0; i < 200; i++) nh.update(chroma);
  TEST_ASSERT_TRUE(nh.hue > 0.0f && nh.hue < 256.0f / 6.0f);   // between slot 0 and slot 1's neighborhood
  TEST_ASSERT_TRUE(nh.hue == nh.hue);                          // finite (not NaN)
}
void test_note_hue_zero_energy_is_inert() {
  NoteHue nh;
  uint16_t chroma[12] = {0};                                   // true silence, no pedestal
  for (int i = 0; i < 200; i++) nh.update(chroma);
  TEST_ASSERT_TRUE(nh.gate < 0.01f);                           // gate collapses -> caller uses user hue
  TEST_ASSERT_TRUE(nh.hue == nh.hue && nh.hue >= 0.0f && nh.hue < 256.0f);  // finite, in-domain
}
void test_note_color_base_gating_and_wrap() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, noteColorBase(100.0f, 30.0f, 0.0f));  // gate 0 -> user hue
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f,  noteColorBase(100.0f, 30.0f, 1.0f));  // gate 1 -> note hue
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f,   noteColorBase(250.0f, 10.0f, 0.5f));  // short way crosses 0: 250+16*0.5=258->2
}
void test_hue_slew_instance_rate() {
  HueSlew fast; fast.ratePerS = SB_HUE_SLEW_PER_S;   // SB mode chases the console's real hue
  fast.update(0, 1000);                              // first call snaps
  TEST_ASSERT_EQUAL(48, fast.update(100, 1200));     // 200ms * 240/s = 48 steps
  HueSlew calm;                                      // default stays 30/s
  calm.update(0, 1000);
  TEST_ASSERT_EQUAL(6, calm.update(100, 1200));      // 200ms * 30/s = 6 (existing behavior)
}

void test_agc_first_call_passes_through() {
  BandAGC a;
  TEST_ASSERT_EQUAL_UINT8(100, a.update(100, 1000));   // envelopes snap to v, no power-on sweep
}
void test_agc_pinned_material_breathes() {
  // The TODO #9 bug: heavily limited material (2000s techno) sits pinned 230..250 -- 20 counts of
  // residual travel, so Bloom's core is a big blob that barely breathes. The stretch must expand
  // that relative travel while the top stays anchored near the held peak.
  BandAGC a;
  for (uint32_t t = 0; t < 5000; t += 25) a.update((t / 25 % 2) ? 250 : 230, t);
  uint8_t hi = a.update(250, 5000), lo = a.update(230, 5025);
  TEST_ASSERT_TRUE(hi >= 240);            // held peak still renders loud (absolute level survives)
  TEST_ASSERT_TRUE(hi - lo >= 80);        // 20 raw counts of travel -> 4x+ the output travel
}
void test_agc_silence_stays_dark() {
  // Span floor: near-zero noise must not be stretched into a light show.
  BandAGC a;
  uint8_t worst = 0;
  for (uint32_t t = 0; t < 3000; t += 25) {
    uint8_t out = a.update((uint8_t)(t / 25 % 6), t);   // 0..5 residual noise
    if (out > worst) worst = out;
  }
  TEST_ASSERT_TRUE(worst <= 15);
}
void test_agc_instant_attack() {
  // A kick after a quiet passage must read at full force the same frame -- only the RELAX is slow.
  BandAGC a;
  for (uint32_t t = 0; t < 2000; t += 25) a.update(5, t);
  TEST_ASSERT_EQUAL_UINT8(250, a.update(250, 2000));
}
void test_agc_envelope_relaxes_to_new_floor() {
  // Loud section -> breakdown at a constant 120: right after the drop the stretched value bottoms
  // out (dramatic contrast), then the max envelope relaxes at AGC_RELAX_PER_S and the steady 120
  // recovers toward its absolute level instead of staying crushed forever.
  BandAGC a;
  for (uint32_t t = 0; t < 2000; t += 25) a.update(250, t);
  TEST_ASSERT_TRUE(a.update(120, 2000) <= 20);          // drop reads as a drop
  uint8_t out = 0;
  for (uint32_t t = 2025; t <= 10000; t += 25) out = a.update(120, t);
  TEST_ASSERT_TRUE(out >= 110);                          // ~6.5s at 20/s: envelope caught up
}
void test_agc_single_dropout_frame_keeps_stretch() {
  // One anomalous near-zero frame (torn lock-free read, a DJ cut) must not slam the min envelope
  // to 0 -- that balloons the span, drops the gain to ~1x, and re-crushes pinned material for the
  // ~11s the min needs to relax back up. The min falls fast (a real breakdown re-opens the window
  // in ~1s) but not instantly.
  BandAGC a;
  for (uint32_t t = 0; t < 5000; t += 25) a.update((t / 25 % 2) ? 250 : 230, t);
  a.update(0, 5000);                                     // one dropout frame
  for (uint32_t t = 5025; t < 5500; t += 25) a.update((t / 25 % 2) ? 250 : 230, t);
  uint8_t hi = a.update(250, 5500), lo = a.update(230, 5525);
  TEST_ASSERT_TRUE(hi - lo >= 80);                       // stretch survives the glitch
}
void test_agc_dt_clamp() {
  // A mode switch / config write can hand one update a multi-second dt; it must relax at most
  // 200ms worth (same clamp as the render EMAs), not teleport the envelope.
  BandAGC a;
  for (uint32_t t = 0; t < 2000; t += 25) a.update(250, t);
  a.update(120, 12000);                                  // 10s gap
  TEST_ASSERT_TRUE((a.maxQ8 >> 8) >= 245);               // 20/s * 0.2s = 4 counts, not 200
}

static SbStreamMags mkMid(uint16_t mid) {   // fill only the mid band (bins 8-23)
  SbStreamMags m; memset(&m, 0, sizeof m);
  for (int i = MID_BIN_LO; i < MID_BIN_HI; i++) m.spectrogram[i] = mid;
  return m;
}

// Mirrors the ONE decision line in onEspNowRecv (main.cpp) that isn't host-testable: score both
// bands against their own bars BEFORE updating either, then let the snare fire only if it
// out-jumped the kick. If this mirror and the callback ever disagree, the callback is wrong --
// keeping that callback to a single line is what makes this test meaningful.
struct DrumRx {
  BeatDetector bass;
  BeatDetector mid{SNARE_MID_FLOOR, SNARE_MID_RISE, SNARE_REFRACTORY_MS, SNARE_MARGIN_DIV};
  bool kick = false, snare = false;
  void feed(uint8_t b, uint8_t m, uint32_t t) {
    int bassS = bass.score(b), midS = mid.score(m);   // pre-update: read history, mutate nothing
    kick = bass.update(b, t);
    bool midFired = mid.update(m, t);                 // ALWAYS called: it owns prev + baseline
    snare = midFired && midS > bassS;
  }
  void quiet(uint32_t from, uint32_t to) {            // settle both baselines on a quiet floor
    for (uint32_t t = from; t < to; t += 10) feed(10, 10, t);
  }
};

void test_mid_band_reads_only_its_bins() {
  BloomParams p = bloomParamsFromMags(mkMid(60000));
  TEST_ASSERT_TRUE(p.mid > 100);
  TEST_ASSERT_EQUAL_UINT8(0, p.bass);           // bins 0-7 untouched
  TEST_ASSERT_EQUAL_UINT8(0, p.high);           // bins 46-63 untouched

  SbStreamMags edges; memset(&edges, 0, sizeof edges);
  edges.spectrogram[MID_BIN_LO - 1] = 65535;    // bin 7  -- bass's territory
  edges.spectrogram[MID_BIN_HI]     = 65535;    // bin 24 -- deliberately unaggregated
  TEST_ASSERT_EQUAL_UINT8(0, bloomParamsFromMags(edges).mid);
}

void test_score_is_the_rising_test() {
  // The invariant the whole design rests on: score(v) > 0 is EXACTLY update()'s level+rise test.
  // If someone edits one and not the other, this fails. Steps are 200ms apart so the refractory
  // window (120ms) never blocks a fire and can't confound the comparison.
  BeatDetector b;
  const uint8_t seq[] = {10, 12, 200, 30, 10, 60, 61, 180, 179, 250, 5};
  uint32_t t = 1000;
  for (uint8_t v : seq) {
    bool predicted = b.score(v) > 0;              // BEFORE update folds v in
    bool fired     = b.update(v, t);
    TEST_ASSERT_EQUAL(predicted, fired);
    t += 200;
  }
}

void test_score_does_not_mutate_detector() {
  BeatDetector scored, control;
  const uint8_t seq[] = {10, 200, 30, 180};
  uint32_t t = 1000;
  for (uint8_t v : seq) {
    for (int i = 0; i < 5; i++) (void)scored.score(v);   // scoring must be free of side effects
    scored.update(v, t);
    control.update(v, t);
    t += 200;
  }
  TEST_ASSERT_EQUAL_UINT8 (control.prev,       scored.prev);
  TEST_ASSERT_EQUAL_UINT16(control.baselineQ4, scored.baselineQ4);
  TEST_ASSERT_EQUAL_UINT32(control.lastMs,     scored.lastMs);
}

void test_kick_body_does_not_fire_a_snare() {
  // A kick's beater click lands in 175-415Hz too. Its fundamental clears the bass bar by far more
  // than its body clears the mid bar, so the phantom snare loses the comparison.
  DrumRx rx;
  rx.quiet(0, 500);
  rx.feed(200, 90, 500);                // kick: huge bass, modest mid body
  TEST_ASSERT_TRUE (rx.kick);
  TEST_ASSERT_FALSE(rx.snare);
}

void test_isolated_snare_fires() {
  DrumRx rx;
  rx.quiet(0, 500);
  rx.feed(15, 180, 500);                // snare: bass flat, mid jumps
  TEST_ASSERT_TRUE (rx.snare);
  TEST_ASSERT_FALSE(rx.kick);
}

void test_snare_survives_decaying_bass() {
  // The case that killed the earlier excess() draft: bass is still ELEVATED (well above its slow
  // baseline, which the 6-sample ring has already dragged up to 103) but no longer RISING at the
  // snare instant (136, down from prev=140). A naive distance-above-baseline measure -- max(floor,
  // base+margin) with no prev+margin term -- scores that 136 at +8: bass still clears its own bar
  // and would veto the snare. score()'s prev+margin term puts the bar at prev+margin = 140+25 = 165
  // instead, which pulls the real bass score negative (-29), so the modest snare (midS = +5) beats
  // it. Mutation-tested: deleting the `prev + margin` line from score() flips bassS to +8 > midS's
  // +5 and this assertion fails. Spacing is a 120BPM backbeat: kick on the beat, snare 250ms later.
  DrumRx rx;
  rx.quiet(0, 500);
  rx.feed(200, 90, 500);                              // kick
  uint8_t decay[] = {190, 180, 170, 160, 150, 140};   // bass rings out, mid returns to the floor
  uint32_t t = 530;
  for (uint8_t b : decay) { rx.feed(b, 12, t); t += 40; }
  rx.feed(136, 45, 750);                              // snare: bass still loud but falling
  TEST_ASSERT_TRUE(rx.snare);
}

void test_simultaneous_kick_and_snare_draw_both() {
  // Asymmetric on purpose: the kick fires unconditionally, only the snare must win a comparison.
  // A real hit on both drums draws both rings.
  DrumRx rx;
  rx.quiet(0, 500);
  rx.feed(120, 240, 500);               // bass up a little, mid slams
  TEST_ASSERT_TRUE(rx.kick);
  TEST_ASSERT_TRUE(rx.snare);
}

void test_band_edge_overload() {
  SbStreamMags m; memset(&m, 0, sizeof m);
  for (int i = 8; i < 16; i++) m.spectrogram[i] = 40000;   // light only the lower half of the default mid band
  BloomParams def  = bloomParamsFromMags(m);
  BloomParams same = bloomParamsFromMags(m, MID_BIN_LO, MID_BIN_HI);
  TEST_ASSERT_EQUAL_UINT8(def.mid, same.mid);              // explicit default edges == the delegating call
  TEST_ASSERT_EQUAL_UINT8(def.bass, same.bass);
  BloomParams tight = bloomParamsFromMags(m, 8, 16);       // window == exactly the lit bins -> denser average
  TEST_ASSERT_TRUE(tight.mid > def.mid);
  BloomParams miss  = bloomParamsFromMags(m, 16, 24);      // window past the lit bins -> silent
  TEST_ASSERT_EQUAL_UINT8(0, miss.mid);
  TEST_ASSERT_EQUAL_UINT8(def.bass, miss.bass);            // mid edges must not move other bands
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_band_edge_overload);
  RUN_TEST(test_bass_heavy_spectrum);
  RUN_TEST(test_treble_heavy_spectrum);
  RUN_TEST(test_loud_bands_keep_travel);
  RUN_TEST(test_beat_fires_once_then_refractory);
  RUN_TEST(test_rumble_does_not_beat);
  RUN_TEST(test_limited_treble_still_sparks);
  RUN_TEST(test_stale_timeout);
  RUN_TEST(test_wire_struct_size);
  RUN_TEST(test_audio_bin_scale);
  RUN_TEST(test_lost_from_gaps_clean_stream);
  RUN_TEST(test_lost_from_gaps_jitter_is_not_loss);
  RUN_TEST(test_lost_from_gaps_counts_multiples);
  RUN_TEST(test_lost_from_gaps_degenerate);
  RUN_TEST(test_lost_from_gaps_boundary_rounding);
  RUN_TEST(test_classify_sb_frames);
  RUN_TEST(test_sync_settings_wire_size);
  RUN_TEST(test_classify_sync_frames);
  RUN_TEST(test_bin_from_angle_geometry);
  RUN_TEST(test_hue_slew_caps_rate);
  RUN_TEST(test_hue_slew_converges_and_holds);
  RUN_TEST(test_hue_slew_wraps_shortest_path);
  RUN_TEST(test_hue_slew_dt_clamp);
  RUN_TEST(test_hue_slew_rejects_nonfinite);
  RUN_TEST(test_hue_slew_instance_rate);
  RUN_TEST(test_note_hue_single_pitch);
  RUN_TEST(test_note_hue_flat_chromagram_gates_off);
  RUN_TEST(test_note_hue_adjacent_notes_no_self_cancel);
  RUN_TEST(test_note_hue_zero_energy_is_inert);
  RUN_TEST(test_note_color_base_gating_and_wrap);
  RUN_TEST(test_agc_first_call_passes_through);
  RUN_TEST(test_agc_pinned_material_breathes);
  RUN_TEST(test_agc_silence_stays_dark);
  RUN_TEST(test_agc_instant_attack);
  RUN_TEST(test_agc_envelope_relaxes_to_new_floor);
  RUN_TEST(test_agc_single_dropout_frame_keeps_stretch);
  RUN_TEST(test_agc_dt_clamp);
  RUN_TEST(test_mid_band_reads_only_its_bins);
  RUN_TEST(test_score_is_the_rising_test);
  RUN_TEST(test_score_does_not_mutate_detector);
  RUN_TEST(test_kick_body_does_not_fire_a_snare);
  RUN_TEST(test_isolated_snare_fires);
  RUN_TEST(test_snare_survives_decaying_bass);
  RUN_TEST(test_simultaneous_kick_and_snare_draw_both);
  return UNITY_END();
}
