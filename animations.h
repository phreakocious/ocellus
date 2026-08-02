#pragma once
#include <cstdint>

// The audio-reactive niche (ESP-NOW RX + audio modes + audio/waterfall debug screens) is the
// DEF CON-only feature; a board that will never sit near a Sensory Bridge can shed it with
// -DOCELLUS_AUDIO=0 in its env's build_flags (compile-checked by env:esp32-s3-noaudio).
// Id layout (2026-07-16): eye/effect ids 0..37 are the stable per-unit range and never move.
// The reserved holes are USED UP (37 = Garden Eels): audio stays pinned at 38..41 and debug at
// 42..44 in EVERY flavor, so `anim` / flash.py --anim numbers mean the same screen in both
// flavors (audio-off simply has no 38..41 entries). Effects now continue ABOVE the debug block:
// 45 = Swirl, 46 = treatcat, 47 = Greetz, 48 = GIFs, 49..55 = ported lab effects, next effect 56+ -- do NOT shift
// audio/debug; units in the field have those ids in saved configs. The debug ids (42..44) are
// interior holes in the playable
// space, so membership stays isPlayableId()/animIdKnown(), never a plain `< ANIM_COUNT` check.
// Cross-flavor caveat, accepted deliberately: flashing an audio-off build onto a unit whose saved
// config held audio favorites or a fixed audio startup id drops those at the next config save
// (config.cpp decode clamps to this build's playables); reflashing audio back on does not restore.
#ifndef OCELLUS_AUDIO
#define OCELLUS_AUDIO 1
#endif

struct AnimInfo { uint8_t id; const char* name; const char* group; };

// Registry split: ids 0..EYE_COUNT-1 are eyes; id EYE_COUNT is the first effect (Matrix).
constexpr int EYE_COUNT    = 13;   // eye ids 0..12
constexpr int EFFECT_COUNT = 25;   // effect ids 13..37 (33 = Wormhole, 34 = QR, 35 = Toasters, 36 = Boids, 37 = Garden Eels)
constexpr int AUDIO_BASE   = 38;   // audio ids pinned here; NO reserved ids left below -- next effect goes to 45+ (see header comment)
constexpr int AUDIO_COUNT  = OCELLUS_AUDIO ? 4 : 0;   // audio ids 38..41 (41 = Echo); 0 = niche compiled out
constexpr uint8_t SWIRL_ID = 45;   // first effect past the debug block (13..37 is full)
constexpr uint8_t TREATCAT_ID = 46;   // interactive treat cat
constexpr uint8_t GREETZ_ID = 47;     // demoscene greetz scroller
constexpr uint8_t GIF_ID    = 48;     // animated GIFs from LittleFS
constexpr uint8_t ATLAS_BASE = 49;    // id 49: first ported creative-coding lab effect (atlas.html catalog)
constexpr int ATLAS_COUNT  = 7;       // ids 49..55: Julia, Interference, Munching Squares, Wireframe Globe, Rose Window, Polar Rose, Fermat Spiral
constexpr int ANIM_COUNT   = ATLAS_BASE + ATLAS_COUNT;   // = 56; one PAST the highest playable id (55). An id BOUND, not a count (42..44 are holes; next effect -> 56+)
constexpr uint64_t PLAYABLE_MASK = ((1ull << (EYE_COUNT + EFFECT_COUNT)) - 1)
                                 | (AUDIO_COUNT ? ((1ull << AUDIO_COUNT) - 1) << AUDIO_BASE : 0)
                                 | (1ull << SWIRL_ID)
                                 | (1ull << TREATCAT_ID)
                                 | (1ull << GREETZ_ID)
                                 | (1ull << GIF_ID)
                                 | (((1ull << ATLAS_COUNT) - 1) << ATLAS_BASE);   // ids 49..55
inline bool isPlayableId(int id) { return id >= 0 && id < 64 && ((PLAYABLE_MASK >> id) & 1); }
constexpr int PLAYABLE_ENTRY_COUNT = EYE_COUNT + EFFECT_COUNT + AUDIO_COUNT + 4 + ATLAS_COUNT;  // +4 = Swirl/treatcat/Greetz/GIFs (45..48), +ATLAS_COUNT ported lab effects (49..55)
constexpr int DEBUG_COUNT  = OCELLUS_AUDIO ? 3 : 1;   // dev-only screens; audio off = sensor debug only
constexpr uint8_t DEBUG_ID = 42;                          // sensor debug -- pinned in every flavor, reached via anim cmd / flash.py --anim (not in the button cycle)
#if OCELLUS_AUDIO
constexpr uint8_t AUDIO_DEBUG_ID = DEBUG_ID + 1;          // id 43: ESP-NOW/audio telemetry (radio on, see isAudioMode)
constexpr uint8_t WATERFALL_ID = DEBUG_ID + 2;            // id 44: 64-bin spectrogram waterfall (radio on, see isAudioMode)
#endif
constexpr int REGISTRY_COUNT = PLAYABLE_ENTRY_COUNT + DEBUG_COUNT;  // ANIMS[] size -- NOT an id bound (the id space has holes); validate ids with animIdKnown()

// loop() gives Pipes a non-standard framebuffer path (the rest get fillScreen(BLACK)).
constexpr uint8_t PIPES_ID = EYE_COUNT + 15;  // id 28; no per-frame clear -> pipes accumulate until they self-restart
constexpr uint8_t FLUID_ID   = EYE_COUNT + 17;  // id 30; tilt-driven liquid. NOT "last effect" -- pinned,
                                                // so appending an effect can't silently retarget it.
constexpr uint8_t YINYANG_ID = EYE_COUNT + 18;  // id 31; spinning nekojiru yin-yang
constexpr uint8_t SLIDESHOW_ID = EYE_COUNT + 19;  // id 32; Web Serial image slideshow
constexpr uint8_t QR_ID = EYE_COUNT + 21;         // id 34; per-unit QR code (bitmap from config.html; device only blits)
constexpr uint8_t BOIDS_ID = EYE_COUNT + 23;      // id 36; flocking with vapor trails -> loop() skips the per-frame clear (fadeFrame instead)
#if OCELLUS_AUDIO
constexpr uint8_t ECHO_ID = AUDIO_BASE + 3;       // id 41; radial ripple spectrogram (last audio mode)
#endif

// ids 0..55; the debug trio (42..44) is an interior hole (between Echo 41 and Swirl 45). Names are placeholders,
// safe to rename (ids are the stable key).
static const AnimInfo ANIMS[REGISTRY_COUNT] = {
  { 0, "Radiate",  "eye"}, { 1, "Glitch",   "eye"}, { 2, "Orbit",    "eye"},
  { 3, "Breathe",  "eye"}, { 4, "Grid",     "eye"}, { 5, "Static",   "eye"},
  { 6, "Rings",    "eye"}, { 7, "Void",     "eye"}, { 8, "Box",      "eye"},
  { 9, "Magenta",  "eye"}, {10, "Confetti", "eye"}, {11, "Aztec",    "eye"},
  {12, "Mosaic",   "eye"},
  {13, "Matrix",    "effect"}, {14, "Cube",     "effect"}, {15, "Plasma",  "effect"},
  {16, "Tesseract", "effect"}, {17, "Tunnel",   "effect"}, {18, "Weave",   "effect"},
  {19, "Sonar",     "effect"}, {20, "Squares",  "effect"}, {21, "Bars",    "effect"},
  {22, "Ripple",    "effect"}, {23, "Spokes",   "effect"},
  {24, "Name Spiral", "effect"},
  {25, "Starfield",   "effect"}, {26, "Mystify",    "effect"}, {27, "DVD",       "effect"},
  {28, "Pipes",       "effect"}, {29, "Fractal", "effect"},
  {30, "Fluid",       "effect"},
  {31, "Yin-Yang",    "effect"},
  {32, "Slideshow",  "effect"},
  {33, "Wormhole",   "effect"},
  {34, "QR",         "effect"},
  {35, "Toasters",   "effect"},
  {36, "Boids",      "effect"},
  {37, "Garden Eels", "effect"},
  // id space below AUDIO_BASE is now full -- the next effect takes id 45+, audio/debug stay put
#if OCELLUS_AUDIO
  {38, "Bloom",          "audio"},
  {39, "Radial Spectrum", "audio"},
  {40, "Reactive Iris",   "audio"},
  {41, "Echo",            "audio"},
#endif
  {SWIRL_ID, "Swirl", "effect"},         // id 45
  {TREATCAT_ID, "treatcat", "effect"},   // id 46 -- interactive treat cat
  {GREETZ_ID, "Greetz", "effect"},       // id 47 -- demoscene sine-wave marquee
  {GIF_ID, "GIFs", "effect"},            // id 48 -- animated GIFs off LittleFS
  // ids 49..55 -- creative-coding lab effects ported from effects.js (atlas.html catalog)
  {(uint8_t)(ATLAS_BASE+0), "Julia",           "effect"},
  {(uint8_t)(ATLAS_BASE+1), "Interference",    "effect"},
  {(uint8_t)(ATLAS_BASE+2), "Munching Sq",     "effect"},
  {(uint8_t)(ATLAS_BASE+3), "Wireframe Globe", "effect"},
  {(uint8_t)(ATLAS_BASE+4), "Rose Window",     "effect"},
  {(uint8_t)(ATLAS_BASE+5), "Polar Rose",      "effect"},
  {(uint8_t)(ATLAS_BASE+6), "Fermat Spiral",   "effect"},
  {DEBUG_ID, "Sensor Debug", "debug"},   // the constant, not a literal
#if OCELLUS_AUDIO
  {AUDIO_DEBUG_ID, "Audio Debug", "debug"},
  {WATERFALL_ID,   "Waterfall",   "debug"},
#endif
};

// Catalog name for an id. Linear over REGISTRY_COUNT (56) because the id space has holes --
// ANIMS[] is NOT indexed by id, and treating it that way is the bug this exists to prevent.
inline const char* animName(uint8_t id) {
  for (int i = 0; i < REGISTRY_COUNT; i++) if (ANIMS[i].id == id) return ANIMS[i].name;
  return "?";
}

inline bool animIdKnown(int id) {   // registry membership -- the only valid id test (id space has holes)
  for (const AnimInfo& a : ANIMS) if (a.id == id) return true;
  return false;
}

static const char* PALETTE_PRESETS[] = { "Rainbow", "Matrix green", "Fire", "Ice", "Mono",
                                         "Synthwave", "Viridis", "Plasma", "Forest", "Twilight" };
constexpr int PALETTE_PRESET_COUNT = (int)(sizeof(PALETTE_PRESETS) / sizeof(PALETTE_PRESETS[0]));
