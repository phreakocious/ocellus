#pragma once
#include <cstdint>

// Wire format — byte-identical to SensoryBridge SB_COMMAND_STREAM_MAGS (p2p.h).
// Each spectrogram bin is magnitude(0..1) * 65535 (Q16 fixed-point); waveform_peak/hue_shift
// are raw floats. static_assert guards our copy against accidental edits / layout drift.
#define NUM_FREQS 64
struct SbStreamMags {
  char     ident[4];                 // "SBC\0"
  uint8_t  command_type;             // 5 = COMMAND_STREAM_MAGS
  uint16_t spectrogram[NUM_FREQS];
  uint16_t chromagram[12];           // unused by the bloom; kept for layout parity
  float    waveform_peak;
  float    hue_shift;
  uint8_t  shi;                      // ignored
};
static_assert(sizeof(SbStreamMags) == 172, "SB wire layout drift - re-sync with SensoryBridge p2p.h");

// Wire format --- byte-identical to SensoryBridge SB_COMMAND_SYNC_SETTINGS (p2p.h): the console's
// knob/flag broadcast, 12.5/s. The SB virtual palette consumes CHROMA_KNOB / NOTE_COLOR /
// INCANDESCENT_FILTER; the rest ride along for layout parity (natural alignment pads to 40).
struct SbSyncSettings {
  char    ident[4];              // "SBC\0"
  uint8_t command_type;          // 1 = COMMAND_SYNC_SETTINGS
  float   PHOTONS_KNOB;
  float   CHROMA_KNOB;
  float   MOOD_KNOB;
  uint8_t LIGHTSHOW_MODE;
  uint8_t MIRROR_ENABLED;
  uint8_t CHROMAGRAM_RANGE;
  uint8_t AUTO_COLOR_SHIFT;
  uint8_t PRISM_COUNT;
  float   INCANDESCENT_FILTER;
  float   BULB_OPACITY;
  uint8_t LS_REMAP;
  uint8_t NOTE_COLOR;
};
static_assert(sizeof(SbSyncSettings) == 40, "SB sync layout drift - re-sync with SensoryBridge p2p.h");

// Integer band energies, each on the same clip-free audioBin() curve (below) as the per-bin
// renderers. They used to use a linear avg/200 that pinned to 255 at raw 51000 -- fine when the
// console sent an instantaneous spectrum, fatal once it peak-holds (SensoryBridge be012a2): bass
// bins sat at full scale, so `bass` was a flat 255 with no travel and `high` cleared every
// threshold. Tests assert relative order + that loud bands still have headroom.
struct BloomParams { uint8_t bass, mid, high, level; };
BloomParams bloomParamsFromMags(const SbStreamMags& m);
// Band-edge-injected flavor for the offline sweep (tools/snare_sweep.cpp): identical math, mid
// edges passed in. The no-arg version delegates here with MID_BIN_LO/HI, so there is one copy of
// the band arithmetic and the device build cannot drift from what the sweep measures.
BloomParams bloomParamsFromMags(const SbStreamMags& m, uint8_t midLo, uint8_t midHi);

// Scale one raw spectrogram bin (0..65535) to 0..255 (squared curve, clip-free at any
// console CONTRAST/gamma setting — see audio.cpp). For per-bin renderers (Radial
// Spectrum, Reactive Iris) that read snap.spectrogram[] directly.
uint8_t audioBin(uint16_t mag);

// Trigger points, in the 0..255 audioBin domain. Shared by the renderers AND the audio-debug
// screen so the reported rates always describe what actually fires. Re-mapped from the old
// clipping-curve values by matching the raw magnitude each one tripped at (bass 130 -> raw 26000
// -> 40 here; the +30 linear rise -> ~+15 on the squared curve).
constexpr uint8_t  BEAT_BASS_FLOOR   = 40;   // bass must clear this...
constexpr uint8_t  BEAT_BASS_RISE    = 15;   // ...and jump this much over the last frame
constexpr uint16_t BEAT_REFRACTORY_MS = 120; // one kick = one beat

// Sparks fire on a treble EDGE, not a level. A peak-hold envelope is designed to keep the level
// up, so any level threshold over-fires under it (measured: 20-45% of frames vs the ~9% this was
// tuned for) and would drift again the next time the console's ENV_DECAY_MS moves. An edge
// survives the hold -- which is exactly why bass, already edge-triggered, came through fine.
constexpr uint8_t  SPARK_HIGH_FLOOR  = 50;
constexpr uint8_t  SPARK_HIGH_RISE   = 15;
constexpr uint16_t SPARK_REFRACTORY_MS = 90;  // hi-hats come faster than kicks

// How hard the proportional margin bites, per band (margin = baseline / marginDiv, see update()).
// Bass needs it harsh: sustained low-frequency energy -- a breakdown's rumble -- is common and must
// not read as kicks. Treble has no rumble analogue (hats ARE transients), and on heavily limited
// material (2000s techno) the baseline sits near full scale, where a /4 margin demands a ~45-count
// jump no brickwalled hi-hat ever makes. That silenced the sparks: 2-3% of frames vs the ~9% target.
constexpr uint8_t  BEAT_MARGIN_DIV  = 4;
constexpr uint8_t  SPARK_MARGIN_DIV = 10;

// The mid band: spectrogram bins 18-31. The console maps bin i to notes[i + NOTE_OFFSET] with
// NOTE_OFFSET defaulting to 12 (SensoryBridge system.h/globals.h), so this is ~330-620Hz -- a
// snare's body. Before this band existed, bass aggregated bins 0-7 (110-165Hz) and high bins 46-63
// (1568-4186Hz), and NO detector watched anything between: a snare fired nothing but a spark, i.e.
// it rendered as a hi-hat. Ends at bin 31 (exclusive 32): the console auto-ranges in two zones
// split at bin 32 (NUM_ZONES=2), so a band straddling that boundary has its halves normalized
// independently and would drift for reasons unrelated to the music -- so run the band right up to
// it. STARTS at bin 18, not the original 8: measured against the tuning corpus (a real TR-909 snare
// through the console, tools/snare_sweep.cpp) a kick's body floods bins 0-15 at full scale, so a
// mid band starting at 8 out-scored the bass detector on every kick and fired a phantom snare on
// each one -- 20 false positives on a plain bd*4 loop, zero vetoes. Only bands starting at >=16
// clear the kick body; >=18 clears it with margin, and 16-32 still leaks on sustained low material
// (rumble). The sweep confirmed this is the ONLY load-bearing change: with the band moved, the
// shipped floor/rise/margin/refractory/weight all pass every fixture unchanged (the whole plateau
// of bins 18-32 is viable for every constant combination). The original bins 8-23 (175-415Hz) sat
// too low -- it was reading the low end of the snare's body, which is also where the kick lives.
constexpr uint8_t MID_BIN_LO = 18;
constexpr uint8_t MID_BIN_HI = 32;   // exclusive; == the bin-32 auto-range zone boundary
constexpr uint8_t MID_BIN_N  = MID_BIN_HI - MID_BIN_LO;

// Snare trip points, in the 0..255 audioBin domain. The margin divisor sits between bass's harsh /4
// (which must reject a breakdown's rumble) and treble's gentle /10 (hats ARE transients). The
// refractory caps the snare at 10/s -- a backbeat is ~2/s at 120BPM, so there is room for a fill.
constexpr uint8_t  SNARE_MID_FLOOR     = 40;
constexpr uint8_t  SNARE_MID_RISE      = 15;
constexpr uint16_t SNARE_REFRACTORY_MS = 100;
constexpr uint8_t  SNARE_MARGIN_DIV    = 6;

// A snare fires only if it out-scores the kick (onEspNowRecv): midS * NUM > bassS * DEN. 1:1 is the
// unweighted comparison (bit-identical to no weight); NUM up favors the snare, DEN up favors the
// kick. It only bites when a kick and a snare land on the SAME packet -- on a normal backbeat they
// alternate, bassS is negative at snare time, and the snare wins at any weight.
//
// This was introduced to correct a predicted scale bias -- bass being 8 near-fully-lit bins high on
// audioBin's SQUARED curve, mid a wider average of a band a snare only partly lights -- on the
// theory that a kick would out-score a snare and silence it. MEASURED (tools/snare_sweep.cpp over
// the .tap corpus), that theory was backwards: with the old bins-8-23 band a kick's mid score BEAT
// its own bass score on every kick, so the comparison never vetoed anything (veto counter: 0) and
// each kick drew a phantom snare. The bias was not in the SCALE, it was in the BAND -- bins 8-15
// are kick body. Moving MID_BIN_LO to 18 fixed it outright, and the sweep then found every weight
// from 1:4 to 3:1 equally viable, i.e. this constant is not load-bearing at all on this corpus.
// It stays at 1:1 (a no-op) rather than being deleted: the coincident fixture ([bd,sd] on the same
// packet) is exactly the case it exists for, and that case still fires 20/20 -- so if real material
// ever does show a same-packet bias, this is the knob, and the fixture is already there to tune it.
constexpr uint8_t  SNARE_VS_KICK_NUM   = 1;
constexpr uint8_t  SNARE_VS_KICK_DEN   = 1;

// --- Audio-mode drawdown ---
constexpr uint32_t DRAWDOWN_SILENCE_MS = 3000;    // silence before fps cap + radio duty-cycle
constexpr uint32_t LISTEN_MS           = 1500;    // radio-on listen window per probe
constexpr uint32_t BACKOFF_BASE_MS     = 8000;    // first radio-off interval
constexpr uint32_t BACKOFF_MAX_MS      = 300000;  // cap (5 min)

// Radio duty-cycle state machine for a silent audio mode. Pure: `stale` is
// audioStale(gAudioRxMillis, now, DRAWDOWN_SILENCE_MS); the caller drives ensureRadio(s.wantRadio).
struct DrawdownState {
  bool     wantRadio;    // desired ESP-NOW state
  uint32_t windowStart;  // when the current listen window opened
  uint32_t offAt;        // when the radio was last turned off
  uint32_t backoffMs;    // current off-interval; doubles on each reopen, capped at BACKOFF_MAX_MS
};
void drawdownStep(DrawdownState& s, bool stale, uint32_t now);

// Procedural "attract" demo: fabricates a packet-shaped spectrum + transients so the audio modes
// stay alive (and unpredictable) with no console. Pure: all state evolves inside a fixed-tick loop
// keyed on `now`, never on call count, so the look is identical at 30 or 58 fps.
struct SynthState {
  uint32_t rng;                 // xorshift state; seed from esp_random() on device, fixed in tests
  uint16_t band[NUM_FREQS];     // per-bin random-walk energy, raw 0..65535 domain
  uint32_t lastWalkMs;          // internal clock; advanced to `now` in fixed ticks
  uint32_t nextBeatMs;          // scheduled next beat
  uint32_t nextSnareMs;         // scheduled next offbeat snare
  uint32_t nextSparkMs;         // scheduled next spark
  int16_t  peakPosQ8[2];        // two traveling spectral "notes", bin position in Q8 (bin<<8)
  int16_t  peakVelQ8[2];        // their per-tick drift velocity, Q8; occasionally perturbed
  int16_t  energyQ8;            // master loudness envelope (~20..256); mean-reverting + excursions
};
void synthAudio(SbStreamMags& out, SynthState& st,
                bool& beat, bool& snare, bool& spark, uint32_t now);

// Rising-edge detector with a refractory window, so one transient fires once. Defaults are the
// bass/beat trip points; construct with explicit values for other bands (Bloom's treble sparks).
struct BeatDetector {
  uint8_t  floorV, rise, marginDiv;
  uint16_t refractoryMs;
  uint8_t  prev       = 0;
  uint16_t baselineQ4 = 0;  // slow local average, Q4 fixed-point; a transient must outrun it (see update())
  uint32_t lastMs     = 0;
  // Explicit ctor, not aggregate init: the device toolchain is gnu++11, where a default member
  // initializer makes the struct a non-aggregate and `BeatDetector{a,b,c}` won't compile.
  BeatDetector(uint8_t f = BEAT_BASS_FLOOR, uint8_t r = BEAT_BASS_RISE,
               uint16_t ms = BEAT_REFRACTORY_MS, uint8_t div = BEAT_MARGIN_DIV)
    : floorV(f), rise(r), marginDiv(div), refractoryMs(ms) {}
  // How far v clears THIS band's live trigger bar (floor, frame-to-frame rise, and the slow
  // baseline's proportional margin, whichever binds hardest), in audioBin counts. > 0 is EXACTLY
  // the level+rise test update() applies -- update() is written on top of this, so the two cannot
  // drift apart. Reads prev/baselineQ4 and mutates nothing, so it is valid BEFORE update() folds v
  // in: that is what lets the RX callback compare two bands' transients against each other, and
  // the debug screen plot a detector's bar without perturbing detection.
  //
  // Distance above the slow baseline ALONE is not a transient measure -- a rising swell sits above
  // a lagging EMA for as long as it keeps rising. The prev+margin term is what makes a flat or
  // decaying band score negative.
  int score(uint8_t v) const;
  bool update(uint8_t v, uint32_t now);
};

// One shared frame-gap clamp for every wall-clock smoother (render EMAs, HueSlew, BandAGC): a
// mode switch or a long NVS config write can hand a single update a multi-second dt, and nothing
// should teleport its state across that gap. One constant, not five hand-copied 200s.
constexpr uint32_t DT_CLAMP_MS = 200;
static inline uint32_t clampDt(uint32_t dt) { return dt > DT_CLAMP_MS ? DT_CLAMP_MS : dt; }

// Slow AGC / contrast-stretch for the band values (bass/high/level). Heavily limited material
// (2000s techno) sits pinned near full scale with ~20 counts of residual travel, so anything
// sized by a band barely breathes -- no absolute threshold fixes that, the RELATIVE contrast has
// to be expanded. Envelope pair per band: max rises instantly and relaxes down at AGC_RELAX_PER_S;
// min falls instantly and relaxes up. Output re-positions v inside that window, expanded AROUND
// THE HELD PEAK (anchored at the max, never above it): the console peak-holds what it sends
// (SB be012a2), so the max IS the held peak -- anchoring there keeps a pinned track rendering
// loud while its residual travel stretches, and makes silence self-gating (output can never
// exceed the recent max). Span floored at AGC_SPAN_FLOOR so near-zero noise isn't stretched
// into a light show. Feeds RENDER sizes only -- BeatDetector keeps raw values; its baseline and
// proportional margin are tuned to the audioBin curve and would double-adapt behind an AGC.
constexpr uint8_t  AGC_SPAN_FLOOR  = 48;   // min stretch window -> gain caps at 255/48 ~= 5.3x
constexpr uint16_t AGC_RELAX_PER_S = 20;   // envelope relax, counts/s (~6.5s across a loud->breakdown drop)
struct BandAGC {
  uint16_t maxQ8 = 0, minQ8 = 0;   // Q8 envelopes: 20/s is 0.5 count/frame at 40fps -- uint8 would freeze (see BeatDetector's Q4 note)
  uint32_t lastMs = 0;             // 0 = first call: snap envelopes to v, pass v through
  uint8_t update(uint8_t v, uint32_t now);
};

// True once the last packet is older than timeoutMs. Signed-diff impl: millis-wrap safe
// AND treats lastRx briefly ahead of now (mid-frame packet) as fresh, not underflow-stale.
bool audioStale(uint32_t lastRxMillis, uint32_t now, uint32_t timeoutMs = 500);

// Lost-packet rate from the inter-packet gap ring (array must be sorted ascending — the
// audio-debug screen already sorts it for the quantiles). The send interval is self-calibrated
// to p50 rather than hardcoding the sender's TARGET_HZ, so it survives console retunes: a gap
// spanning round(gap/p50) send intervals hides (spans - 1) lost packets. Returns lost packets
// per second, rounded, normalized over the ring's timespan (the sum of its gaps).
uint16_t lostFromGaps(const uint16_t* sortedGaps, uint16_t n);

// The console broadcasts more than the spectrum on the same channel: COMMAND_SYNC_SETTINGS at
// 12.5/s (run_p2p sends one every 20 iterations of its 250Hz loop) and COMMAND_IDENTIFY_MAIN.
// Treating those as rejects pegged the rej counter at its display cap in minutes and destroyed
// its signal. Sync = the knob/flag broadcast, consumed by the SB virtual palette (main.cpp
// stores it, latest-wins). Chatter = other recognized SB frames that just aren't ours, e.g.
// identify (ignore silently). Junk = what the rej counter exists for: foreign ident, unknown
// command (protocol drift), or a mags frame of the wrong size (layout drift). Command ids mirror
// SB's COMMAND_TYPES enum (p2p.h).
constexpr uint8_t SB_CMD_SYNC_SETTINGS = 1;
constexpr uint8_t SB_CMD_STREAM_MAGS = 5;
constexpr uint8_t SB_CMD_COUNT       = 6;   // NUM_COMMAND_TYPES on the console; >= this = drift
enum class SbFrame : uint8_t { Mags, Sync, Chatter, Junk };
SbFrame classifySbFrame(const uint8_t* data, int len);

// Angle->bin map for the Echo ripple renderer (main.cpp id 34): byte-angle (fastSin convention:
// 0 = +x/right, 64 = +y/screen-down) -> spectrogram bin, mirrored about the vertical axis.
// Bass (bin 0) at the bottom, treble (bin 63) at the top, both halves sweeping bass->treble so
// a kick draws symmetric arcs. Applied once at polar-LUT build time, never per frame.
void buildBinFromAngle(uint8_t out[256]);

// Chromagram -> note hue + tonality gate: RX-side port of the console's update_note_hue (GDFT.h)
// so its NOTE_COLOR mode can be replicated from the packets alone. Feed the Q16 chromagram once
// per rendered frame. hue is byte-hue (0..256 wheel, circle-of-fifths layout so harmonic
// neighbors are chromatic neighbors); gate is 0..1 tonality*loudness (0 = atonal/quiet -> caller
// collapses to the user hue via noteColorBase). EMA HOLD 0.9 is the console's constant, tuned at
// ITS frame rate; ours differs (~50fps) -- retune here if the note color visibly drags or jitters.
struct NoteHue {
  float sxS = 0, syS = 0, mxS = 0, meanS = 0;   // smoothed fifths-vector + gate terms
  float hue  = 0;    // 0..256
  float gate = 0;    // 0..1
  void update(const uint16_t chroma[12]);
};

// Rotate from the user's base hue toward the note hue by gate, the short way around the wheel
// (port of console note_color_base, byte-hue domain). Result wrapped to 0..256.
float noteColorBase(float userBase, float noteHue, float gate);

// Palette-phase slew limiter (TODO.md's "log(delta_hue)" item): the console's hue_shift can
// jump packet-to-packet (observed swings of +/-4 -> +/-160 palette-phase steps), which strobes
// any mode where hue paints the whole frame. Chase the target along the SHORTEST cyclic path
// (palette phase is a 256-step circle, so a live<->stale source switch is also bounded to <=128
// steps of calm drift) at a capped rate. A rate cap, not log(): robust to whatever scale another
// project's hue_shift uses. Cap ~= the stale drift's calm 25 steps/s (~10s/palette cycle).
// One shared instance in main.cpp keeps hue continuous across audio-mode switches; a struct
// (not a function-local static) so native tests can construct fresh instances.
constexpr float HUE_SLEW_PER_S = 30.0f;
// The SB virtual palette chases the console's REAL displayed hue, which kicks with the music by
// design -- the calm 30/s cap would lag it by seconds. 240/s still bounds a torn-float frame and
// the live<->stale handoff to a fraction of the wheel per frame.
constexpr float SB_HUE_SLEW_PER_S = 240.0f;
struct HueSlew {
  float    phase  = 0;
  float    ratePerS = HUE_SLEW_PER_S;
  uint32_t lastMs = 0;   // 0 = first call: snap to target instead of slewing from phase 0
  int update(float target, uint32_t now);   // returns the current phase, 0..255
};
