#include "audio.h"
#include <cmath>
#include <cstring>

uint8_t audioBin(uint16_t mag) {
  // The firmware auto-ranges each bin to 0..1 and applies a perceptual lift before
  // packing to 0..65535. Since SensoryBridge ed75741 that lift is mag^gamma with
  // gamma set by the console's CONTRAST knob (0.75 knob-down .. 0.15 knob-up); the
  // fixed sqrt(sqrt) (gamma 0.25) this curve was tuned against == knob at ~0.83.
  // The old mag/200 clipped to white at packed value 0.778, so any sustained bin
  // (esp. bass) pinned to 255 -> a dead flat line on a per-bin plot. Squaring the
  // normalized value spans the full 0..255 and never clips at ANY gamma; net render
  // is mag^(2*gamma), so the console knob shifts our contrast too (by design: one
  // knob shapes every display on the network). More travel on loud bins? add
  // another `n *= n`.
  float n = mag / 65535.0f;
  n *= n;
  return (uint8_t)(n * 255.0f + 0.5f);
}

BloomParams bloomParamsFromMags(const SbStreamMags& m, uint8_t midLo, uint8_t midHi) {
  uint32_t bassSum = 0, midSum = 0, highSum = 0, allSum = 0;
  for (int i = 0;     i < 8;     i++) bassSum += m.spectrogram[i];
  for (int i = midLo; i < midHi; i++) midSum  += m.spectrogram[i];
  for (int i = 46;    i < 64;    i++) highSum += m.spectrogram[i];
  for (int i = 0;     i < 64;    i++) allSum  += m.spectrogram[i];
  // Same clip-free curve as the per-bin renderers -- band averages are magnitudes too, and the
  // loudest bands are exactly the ones the old linear curve threw away all the travel on.
  return { audioBin(bassSum / 8), audioBin(midSum / (uint32_t)(midHi - midLo)),
           audioBin(highSum / 18), audioBin(allSum / 64) };
}

BloomParams bloomParamsFromMags(const SbStreamMags& m) {
  return bloomParamsFromMags(m, MID_BIN_LO, MID_BIN_HI);
}

int BeatDetector::score(uint8_t v) const {
  // update() fires iff  v > floorV && v > prev + margin && v > base + margin,
  // i.e. iff  v > max(floorV, prev + margin, base + margin).  Call that the bar; this is the
  // distance to it. One copy of the rule, so score() and update() cannot disagree.
  int base   = (int)(baselineQ4 >> 4);
  int margin = base / (int)marginDiv;
  if (margin < (int)rise) margin = (int)rise;
  int bar = base + margin;
  if ((int)prev + margin > bar) bar = (int)prev + margin;
  if ((int)floorV        > bar) bar = (int)floorV;
  return (int)v - bar;
}

bool BeatDetector::update(uint8_t v, uint32_t now) {
  // A transient must clear the floor, jump over the last frame, AND outrun a slow local average.
  //
  // The baseline is what rejects a breakdown's rumble. What separates a kick from rumble is
  // DURATION, not loudness: an ~8-frame EMA (~200ms at our frame rate) is slower than a kick
  // (~60ms, so a kick outruns it) but faster than a swell (which drags the baseline up with it
  // until it stops qualifying). Sustained energy thus goes quiet after ~2 triggers at its onset --
  // which is musically right; the first moments of a rumble ARE a transient.
  //
  // The margin has to SCALE with level. audioBin's curve is squared, so its slope grows with
  // magnitude: up at raw ~60000 a couple thousand counts of ordinary band noise moves the value by
  // ~15, and any fixed `rise` gets machine-gunned. A proportional margin tracks the curve -- ~+50
  // needed on a loud rumble, still just `rise` when the band is quiet and a soft kick should count.
  // marginDiv sets how hard it bites -- treble wants it far gentler than bass (see audio.h).
  bool rising     = score(v) > 0;
  bool refractory = (uint32_t)(now - lastMs) < refractoryMs;
  prev = v;
  // Q4 fixed-point, NOT a plain uint8 EMA. Integer division truncates toward zero, so at 8-bit
  // resolution a signal sitting below the baseline decrements it by (v-base)/8 == 0 while every
  // transient still adds: the baseline ratchets UP and never settles at the true mean (a 180/205
  // hat pattern walked it to ~197 instead of ~184), inflating the margin until nothing can fire.
  // 4 fractional bits keep the per-step bias under 1/16 count. v<<4 maxes at 4080 -- fits uint16.
  baselineQ4 = (uint16_t)((int)baselineQ4 + (((int)v << 4) - (int)baselineQ4) / 8);   // after the test: the baseline is history, not this sample
  if (rising && !refractory) { lastMs = now; return true; }
  return false;
}

uint8_t BandAGC::update(uint8_t v, uint32_t now) {
  uint16_t vQ8 = (uint16_t)v << 8;
  if (!lastMs) { lastMs = now; maxQ8 = minQ8 = vQ8; return v; }
  uint32_t dt = clampDt((uint32_t)(now - lastMs));
  lastMs = now;
  uint16_t relax = (uint16_t)((uint32_t)AGC_RELAX_PER_S * dt * 256 / 1000);
  if (vQ8 >= maxQ8) maxQ8 = vQ8;           // attack is instant -- only the relax is slow
  else maxQ8 -= (uint16_t)(maxQ8 - vQ8) < relax ? (uint16_t)(maxQ8 - vQ8) : relax;   // relax toward v, never past it
  // The min FALLS rate-limited too (8x the relax: a real breakdown re-opens the window in <1s) --
  // an instant fall let ONE near-zero frame (torn lock-free read, a DJ cut) balloon the span and
  // crush the stretch back to ~1x for the ~11s the min then needs to climb home.
  uint16_t fall = (uint16_t)(relax * 8);
  if (vQ8 <= minQ8) minQ8 -= (uint16_t)(minQ8 - vQ8) < fall ? (uint16_t)(minQ8 - vQ8) : fall;
  else minQ8 += (uint16_t)(vQ8 - minQ8) < relax ? (uint16_t)(vQ8 - minQ8) : relax;
  int span = (maxQ8 - minQ8) >> 8;
  if (span < AGC_SPAN_FLOOR) span = AGC_SPAN_FLOOR;
  int mx  = maxQ8 >> 8;
  int out = mx - (mx - (int)v) * 255 / span;   // expand downward from the held peak
  return out < 0 ? 0 : (uint8_t)out;           // out <= mx <= 255 by construction; only the floor needs a clamp
}

bool audioStale(uint32_t lastRxMillis, uint32_t now, uint32_t timeoutMs) {
  // Signed diff: millis-wrap safe AND tolerates lastRxMillis briefly AHEAD of `now`.
  // The renderer snapshots `now` at loop-top; a packet landing during the ~59ms frame
  // (console streams ~195/s) sets lastRxMillis > now, so unsigned (now-lastRx) underflowed
  // to ~4.29e9 and flapped us to idle-breath ~45% of frames regardless of timeout.
  return (int32_t)(now - lastRxMillis) > (int32_t)timeoutMs;
}

uint16_t lostFromGaps(const uint16_t* sortedGaps, uint16_t n) {
  if (!n) return 0;
  uint16_t p50 = sortedGaps[(n * 50) / 100];
  if (!p50) return 0;                                    // sub-ms stream: gaps quantize to 0, nothing to infer
  uint32_t lost = 0, sumMs = 0;
  for (uint16_t i = 0; i < n; i++) {
    sumMs += sortedGaps[i];
    uint32_t spans = (sortedGaps[i] + p50 / 2) / p50;    // send intervals this gap covers, rounded
    if (spans > 1) lost += spans - 1;                    // spans==0 (tiny gap) must not underflow
  }
  if (!sumMs) return 0;
  return (uint16_t)(((uint64_t)lost * 1000 + sumMs / 2) / sumMs);
}

SbFrame classifySbFrame(const uint8_t* data, int len) {
  if (len < 5 || data[0] != 'S' || data[1] != 'B' || data[2] != 'C') return SbFrame::Junk;
  if (data[4] == SB_CMD_STREAM_MAGS)                     // ours -- but only at the asserted wire size
    return len >= (int)sizeof(SbStreamMags) ? SbFrame::Mags : SbFrame::Junk;
  if (data[4] == SB_CMD_SYNC_SETTINGS)                   // ours too now -- same wire-size discipline
    return len >= (int)sizeof(SbSyncSettings) ? SbFrame::Sync : SbFrame::Junk;
  return data[4] < SB_CMD_COUNT ? SbFrame::Chatter : SbFrame::Junk;
}

void buildBinFromAngle(uint8_t out[256]) {
  for (int a = 0; a < 256; a++) {
    int d = (a - 64) & 255;         // angular distance from screen-bottom...
    if (d > 128) d = 256 - d;       // ...folded to 0..128 (the fold IS the L/R mirror)
    out[a] = (uint8_t)(d * 63 / 128);
  }
}

void NoteHue::update(const uint16_t chroma[12]) {
  // pitch-class -> wheel slot; x7 mod 12 is its own inverse (circle of fifths).
  static const uint8_t FIFTHS[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};
  static float WCOS[12], WSIN[12];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 12; i++) {
      float ang = FIFTHS[i] * (2.0f * 3.14159265f / 12.0f);
      WCOS[i] = cosf(ang); WSIN[i] = sinf(ang);
    }
    ready = true;
  }
  // The chromagram rides a high flat pedestal (the console's perceptual lift pulls every lit bin
  // toward 1.0): subtract the mean so only ABOVE-average notes drive the vector and the gate.
  float c[12], mean = 0, mx = 0;
  for (int i = 0; i < 12; i++) {
    c[i] = chroma[i] * (1.0f / 65535.0f);
    mean += c[i];
    if (c[i] > mx) mx = c[i];
  }
  mean *= (1.0f / 12.0f);
  float sx = 0, sy = 0;
  for (int i = 0; i < 12; i++) {
    float w = c[i] - mean;
    if (w > 0) { sx += w * WCOS[i]; sy += w * WSIN[i]; }
  }
  // Smooth the VECTOR, never the angle -- averaging across the 256->0 wrap jumps the whole wheel.
  const float HOLD = 0.9f;
  sxS   = sxS   * HOLD + sx   * (1.0f - HOLD);
  syS   = syS   * HOLD + sy   * (1.0f - HOLD);
  mxS   = mxS   * HOLD + mx   * (1.0f - HOLD);
  meanS = meanS * HOLD + mean * (1.0f - HOLD);
  float h = atan2f(syS, sxS) * (256.0f / (2.0f * 3.14159265f));
  if (h < 0) h += 256.0f;
  hue = h;
  // Tonality gate = peakedness (one note above the pedestal, vs flat) x loudness clamp.
  float peak = (mxS > 1e-4f) ? ((mxS - meanS) / mxS) : 0.0f;
  float loud = mxS > 1.0f ? 1.0f : mxS;
  gate = peak * loud;
}

float noteColorBase(float userBase, float noteHue, float gate) {
  // wraps at 256 (the true byte wheel); the console's note_color_base uses 255 -- sub-step difference near the wrap, kept correct here
  float d = noteHue - userBase;
  while (d > 128.0f)  d -= 256.0f;
  while (d < -128.0f) d += 256.0f;
  float h = userBase + d * gate;
  while (h >= 256.0f) h -= 256.0f;
  while (h < 0.0f)    h += 256.0f;
  return h;
}

int HueSlew::update(float target, uint32_t now) {
  float t = fmodf(target, 256.0f);
  if (t < 0) t += 256.0f;
  if (!(t >= 0.0f && t < 256.0f)) return (int)phase;   // NaN/Inf target (torn read, hostile frame):
                                                        // ignore it -- phase must never be poisoned,
                                                        // gHueSlew is shared by ALL audio modes
  if (!lastMs) { lastMs = now; phase = t; return (int)phase; }   // first call: no 4s power-on sweep
  uint32_t dt = clampDt((uint32_t)(now - lastMs));
  lastMs = now;
  float d = t - phase;                     // shortest way around the phase circle
  if (d > 128.0f)  d -= 256.0f;
  if (d < -128.0f) d += 256.0f;
  float maxStep = ratePerS * (float)dt / 1000.0f;
  if (d >  maxStep) d =  maxStep;
  if (d < -maxStep) d = -maxStep;
  phase += d;
  if (phase >= 256.0f) phase -= 256.0f;
  if (phase < 0)       phase += 256.0f;
  return (int)phase;
}

void drawdownStep(DrawdownState& s, bool stale, uint32_t now) {
  if (!stale) {                                   // heard a packet -> full recovery
    s.wantRadio = true;
    s.backoffMs = BACKOFF_BASE_MS;
    return;
  }
  if (s.wantRadio) {                              // listen window open
    if ((int32_t)(now - s.windowStart) >= (int32_t)LISTEN_MS) {  // elapsed empty -> close
      s.wantRadio = false;
      s.offAt = now;
    }
  } else {                                        // radio off, waiting out the backoff
    if ((int32_t)(now - s.offAt) >= (int32_t)s.backoffMs) {      // interval served -> reopen
      s.wantRadio = true;
      s.windowStart = now;
      uint32_t next = s.backoffMs * 2;            // double AFTER serving, so the first off is BASE
      s.backoffMs = next > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : next;
    }
  }
}

// xorshift32 on the SynthState's own stream (deterministic under a fixed seed).
static inline uint32_t synthRand(SynthState& st) {
  uint32_t s = st.rng ? st.rng : 0x9E3779B9;   // never let a zero seed lock the generator
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return st.rng = s;
}

void synthAudio(SbStreamMags& out, SynthState& st,
                bool& beat, bool& snare, bool& spark, uint32_t now) {
  constexpr uint32_t WALK_STEP_MS = 30;        // fixed tick -> cadence-independent
  constexpr int PEAK_AMP = 60000;              // traveling-note tent peak amplitude (per-peak width below)

  if (st.lastWalkMs == 0) {                     // first call: seed the schedules + envelopes
    for (int i = 0; i < NUM_FREQS; i++) st.band[i] = synthRand(st) % 8000;   // start near-dark
    st.lastWalkMs  = now ? now : 1;             // 1 so a first call at now==0 still inits
    st.nextBeatMs  = st.lastWalkMs + 400 + synthRand(st) % 800;
    st.nextSnareMs = st.lastWalkMs + 200 + synthRand(st) % 600;
    st.nextSparkMs = st.lastWalkMs + 150 + synthRand(st) % 450;
    st.peakPosQ8[0] = (int16_t)(14 << 8); st.peakVelQ8[0] =  55;   // two "notes" drifting
    st.peakPosQ8[1] = (int16_t)(44 << 8); st.peakVelQ8[1] = -40;   // opposite ways, so they cross
    st.energyQ8 = 160;
  }

  beat = snare = spark = false;                 // OR-latched across the ticks this call processes

  // Advance in whole WALK_STEP_MS ticks up to `now`; do ALL evolution at tick time `t` (== the
  // internal clock), never at call time -> reaching a given `now` is bit-identical at any cadence.
  while ((int32_t)(now - st.lastWalkMs) >= (int32_t)WALK_STEP_MS) {
    st.lastWalkMs += WALK_STEP_MS;
    uint32_t t = st.lastWalkMs;

    // Master loudness envelope: mean-reverts to mid, with rare breakdowns/drops -> macro dynamics.
    int e = st.energyQ8 + ((int)(synthRand(st) % 21) - 10) + (160 - st.energyQ8) / 32;
    if (synthRand(st) % 500 == 0) e =  25 + (int)(synthRand(st) % 55);   // breakdown: near-silent
    if (synthRand(st) % 500 == 0) e = 220 + (int)(synthRand(st) % 37);   // drop: slammed
    st.energyQ8 = (int16_t)(e < 20 ? 20 : (e > 256 ? 256 : e));
    uint32_t E = (uint32_t)st.energyQ8;
    uint32_t g = 140 + E * 116 / 256;           // hit/peak brightness gain ~0.55..1.0: loud hits
                                                // reach full scale; E varies brightness, never caps it

    for (int i = 0; i < NUM_FREQS; i++) {       // ENVELOPE: decay toward dark + a faint scaled floor
      uint32_t v = (uint32_t)st.band[i] * 236 / 256;              // ~0.92/tick tail (~260ms half-life)
      uint32_t floorAmp = i < 8 ? 700 : (i < 32 ? 450 : 300);
      v += (synthRand(st) % floorAmp) * E / 256;
      st.band[i] = (uint16_t)(v > 60000 ? 60000 : v);
    }

    if ((int32_t)(t - st.nextBeatMs) >= 0) {    // kick: punch bass (energy-scaled), then it decays
      beat = true;
      st.nextBeatMs = t + 400 + synthRand(st) % 800;
      if (synthRand(st) % 5 < 2) snare = true;  // ~40% coincident snare
      uint32_t base = (50000 + synthRand(st) % 15000) * g / 256;  // up to ~full scale on loud kicks
      for (int i = 0; i < 8; i++) {                               // per-bin jitter -> ragged, not a plateau
        int h = (int)base - (int)(synthRand(st) % 16000);
        if (h > (int)st.band[i]) st.band[i] = (uint16_t)(h < 0 ? 0 : h);
      }
    }
    if ((int32_t)(t - st.nextSnareMs) >= 0) {   // offbeat snare: punch the mid band
      snare = true;
      st.nextSnareMs = t + 500 + synthRand(st) % 1500;
      uint32_t base = (44000 + synthRand(st) % 16000) * g / 256;
      for (int i = MID_BIN_LO; i < MID_BIN_HI; i++) {
        int h = (int)base - (int)(synthRand(st) % 18000);
        if (h > (int)st.band[i]) st.band[i] = (uint16_t)(h < 0 ? 0 : h);
      }
    }
    if ((int32_t)(t - st.nextSparkMs) >= 0) {   // hi-hat: punch treble, faster cadence
      spark = true;
      st.nextSparkMs = t + 150 + synthRand(st) % 450;
      uint32_t base = (40000 + synthRand(st) % 15000) * g / 256;
      for (int i = 46; i < NUM_FREQS; i++) {
        int h = (int)base - (int)(synthRand(st) % 16000);
        if (h > (int)st.band[i]) st.band[i] = (uint16_t)(h < 0 ? 0 : h);
      }
    }

    for (int p = 0; p < 2; p++) {               // two "notes" drift across the band and INJECT a
      st.peakPosQ8[p] += st.peakVelQ8[p];        // tent into the decaying bands -> comet trail (Echo)
      if (synthRand(st) % 24 == 0)               // occasional velocity nudge -> unpredictable paths
        st.peakVelQ8[p] = (int16_t)(st.peakVelQ8[p] + ((int)(synthRand(st) % 41) - 20));
      if (st.peakVelQ8[p] >  110) st.peakVelQ8[p] =  110;
      if (st.peakVelQ8[p] < -110) st.peakVelQ8[p] = -110;
      if (st.peakPosQ8[p] < 0)                       { st.peakPosQ8[p] = (int16_t)(-st.peakPosQ8[p]);                        st.peakVelQ8[p] = (int16_t)(-st.peakVelQ8[p]); }
      if (st.peakPosQ8[p] > ((NUM_FREQS - 1) << 8))  { st.peakPosQ8[p] = (int16_t)(((NUM_FREQS - 1) << 9) - st.peakPosQ8[p]); st.peakVelQ8[p] = (int16_t)(-st.peakVelQ8[p]); }
      int w = (p == 0) ? 4 : 8;                    // one sharp note, one broad -> not identical twins
      uint32_t amp = ((uint32_t)PEAK_AMP - synthRand(st) % 18000) * g / 256;   // amplitude flickers
      int c = st.peakPosQ8[p] >> 8;
      for (int d = -(w - 1); d <= w - 1; d++) {
        int i = c + d; if (i < 0 || i >= NUM_FREQS) continue;
        int ad = d < 0 ? -d : d;
        uint32_t tent = amp * (w - ad) / w;
        if (tent > st.band[i]) st.band[i] = (uint16_t)(tent > 60000 ? 60000 : tent);
      }
    }
  }

  memset(&out, 0, sizeof out);
  for (int i = 0; i < NUM_FREQS; i++) out.spectrogram[i] = st.band[i];
}
