#pragma once
#include <cstdint>
#include <vector>
#include "config.h"   // Palette

// Palette id scheme (fixed low-id blocks; must fit the uint32 palettesEnabled mask):
//   bits 0..15  -> presets  (PRESET_COUNT used; 10..15 reserved for future presets)
//   bits 16..19 -> custom[0..MAX_CUSTOM-1]   (id = CUSTOM_ID_BASE + index)
// Keep PRESET_COUNT and the preset order in sync with PALETTE_PRESETS[] in animations.h.
constexpr int PRESET_COUNT   = 10;
constexpr int CUSTOM_ID_BASE = 16;
constexpr int MAX_CUSTOM     = 4;

// Build a 256-entry RGB565 lookup indexed by pColor's phase byte.
// Rainbow (preset 0) reproduces the legacy 3-sine rainbow byte-for-byte;
// buildPaletteLUT walks a cyclic gradient over `stops` (2..5 colors; 1 = solid).
void buildRainbowLUT(uint16_t out[256]);
void buildPaletteLUT(const std::vector<uint16_t>& stops, uint16_t out[256]);

// FastLED hsv2rgb_rainbow at sat=val=255, reduced to the pure hue wheel: what a Sensory Bridge
// console shows for a given CHSV hue byte. incandescentMix (0..255) applies the console's
// incandescent filter mix stage against its (255,114,40) lookup; its LED-level brightness-leakage
// stage is deliberately not ported. Index IS the hue -- pColor's SB branch relies on that.
void buildWheelLUT(uint16_t out[256], uint8_t incandescentMix);

// Colour stops for a palette id: preset table (ids 1..4) or custom.colors (ids 16..).
// Rainbow (id 0) and unresolved ids return empty (Rainbow is special-cased by buildRainbowLUT).
std::vector<uint16_t> paletteStops(uint8_t id, const std::vector<Palette>& customs);

// Enabled ids that actually resolve to a palette, ascending. Empty enabled set or only
// unresolvable bits -> {0} (Rainbow) so rotation always has something to show.
std::vector<uint8_t> activeRotationList(uint32_t enabledMask, const std::vector<Palette>& customs);
