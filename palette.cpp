#include "palette.h"
#include <cmath>

// --- RGB565 channel helpers ---
static inline void dec565(uint16_t c, int& r, int& g, int& b) {
  r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F;
}
static inline uint16_t enc565(int r, int g, int b) {
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Byte-exact to main.cpp: sinLUT[i] = (int16_t)(sin(i*2pi/256)*127); pColor packs fastSin+128.
void buildRainbowLUT(uint16_t out[256]) {
  const double TWO_PI = 2.0 * 3.141592653589793;
  for (int p = 0; p < 256; p++) {
    auto qsin = [&](int a) { return (int)(int16_t)(std::sin((a & 0xFF) * TWO_PI / 256.0) * 127); };
    int r = qsin(p) + 128, g = qsin(p + 85) + 128, b = qsin(p + 170) + 128;
    out[p] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
}

void buildPaletteLUT(const std::vector<uint16_t>& stops, uint16_t out[256]) {
  int n = (int)stops.size();
  if (n <= 0) { for (int p = 0; p < 256; p++) out[p] = 0; return; }        // guard (callers avoid this)
  if (n == 1) { for (int p = 0; p < 256; p++) out[p] = stops[0]; return; } // solid
  for (int p = 0; p < 256; p++) {
    int t = p * n;                 // 0 .. 255*n
    int seg  = t >> 8;             // segment 0..n-1
    int frac = t & 0xFF;           // fractional numerator over 256
    int r0, g0, b0, r1, g1, b1;
    dec565(stops[seg % n],       r0, g0, b0);
    dec565(stops[(seg + 1) % n], r1, g1, b1);   // wraps last->first (cyclic)
    out[p] = enc565(r0 + (r1 - r0) * frac / 256,
                    g0 + (g1 - g0) * frac / 256,
                    b0 + (b1 - b0) * frac / 256);
  }
}

void buildWheelLUT(uint16_t out[256], uint8_t incandescentMix) {
  for (int h = 0; h < 256; h++) {
    // 8 sections of 32 hues; linear "third" ramps within each (FastLED's default Y1 yellow boost;
    // matches FastLED's classic scale8 ((i*85)>>8); builds with FASTLED_SCALE8_FIXED differ by ±1 LSB).
    uint8_t offset8  = (uint8_t)((h & 0x1F) << 3);
    uint8_t third    = (uint8_t)((offset8 * 85)  >> 8);   // scale8(offset8, 85):  0..82
    uint8_t twothird = (uint8_t)((offset8 * 170) >> 8);   // scale8(offset8, 170): 0..164
    int r, g, b;
    switch (h >> 5) {
      case 0:  r = 255 - third;    g = third;          b = 0;            break; // red -> orange
      case 1:  r = 171;            g = 85 + third;     b = 0;            break; // orange -> yellow
      case 2:  r = 171 - twothird; g = 170 + third;    b = 0;            break; // yellow -> green
      case 3:  r = 0;              g = 255 - third;    b = third;        break; // green -> aqua
      case 4:  r = 0;              g = 171 - twothird; b = 85 + third;   break; // aqua -> blue
      case 5:  r = third;          g = 0;              b = 255 - third;  break; // blue -> purple
      case 6:  r = 85 + third;     g = 0;              b = 171 - third;  break; // purple -> pink
      default: r = 170 + third;    g = 0;              b = 85 - third;   break; // pink -> red
    }
    if (incandescentMix) {   // console apply_incandescent_filter, mix stage only
      int inv = 255 - incandescentMix;
      r = ((r * inv) >> 8) + ((((r * 255) >> 8) * incandescentMix) >> 8);
      g = ((g * inv) >> 8) + ((((g * 114) >> 8) * incandescentMix) >> 8);
      b = ((b * inv) >> 8) + ((((b *  40) >> 8) * incandescentMix) >> 8);
    }
    // Normalize to full brightness: keep the hue/tint RATIOS (the warm character survives as a
    // shift toward red on mixed colors) but discard the luminance loss. The console's shaping --
    // FastLED's 171-capped sections AND the incandescent dimming (measured 0.86 on the real
    // console = blue crushed to ~27%) -- is tuned for physically-bright LEDs; on the LCD it just
    // read dim (hardware pass, 2026-07-13). Primaries normalize to themselves, so the landmark
    // entries (red/green/blue) are still byte-exact FastLED.
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx > 0 && mx < 255) { r = r * 255 / mx; g = g * 255 / mx; b = b * 255 / mx; }
    out[h] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
}

std::vector<uint16_t> paletteStops(uint8_t id, const std::vector<Palette>& customs) {
  switch (id) {
    // Preset stops (RGB565 via the same encoding as hexToRgb565). Keep in sync with
    // PALETTE_PRESETS in animations.h. Id 0 (Rainbow) is special-cased -> empty.
    case 1: return { hexToRgb565("#00ff41"), hexToRgb565("#002200") };                    // Matrix green
    case 2: return { hexToRgb565("#ff0000"), hexToRgb565("#ff7700"), hexToRgb565("#ffdd00") }; // Fire
    case 3: return { hexToRgb565("#001a66"), hexToRgb565("#00ccff"), hexToRgb565("#ffffff") }; // Ice
    case 4: return { hexToRgb565("#1a1a1a"), hexToRgb565("#ffffff") };                     // Mono
    // From spiralSquared/collatz _palettes; cyclic (last stop loops back toward the first).
    case 5: return { hexToRgb565("#0a051e"), hexToRgb565("#280a50"), hexToRgb565("#781478"), hexToRgb565("#ff1e78"), hexToRgb565("#ff50c8"), hexToRgb565("#00dcff"), hexToRgb565("#14ffc8"), hexToRgb565("#7828b4") }; // Synthwave
    case 6: return { hexToRgb565("#440154"), hexToRgb565("#3b528b"), hexToRgb565("#21918c"), hexToRgb565("#35b779"), hexToRgb565("#5ec962"), hexToRgb565("#a3db3a"), hexToRgb565("#fde725"), hexToRgb565("#782846") }; // Viridis
    case 7: return { hexToRgb565("#0d0887"), hexToRgb565("#5402a3"), hexToRgb565("#9e0187"), hexToRgb565("#d5414e"), hexToRgb565("#f89516"), hexToRgb565("#f0f921"), hexToRgb565("#965064") };                          // Plasma
    case 8: return { hexToRgb565("#000500"), hexToRgb565("#001e0a"), hexToRgb565("#0a3c14"), hexToRgb565("#14781e"), hexToRgb565("#32c832"), hexToRgb565("#96ff64"), hexToRgb565("#ffdc32"), hexToRgb565("#325014") }; // Forest
    case 9: return { hexToRgb565("#00000a"), hexToRgb565("#140028"), hexToRgb565("#320050"), hexToRgb565("#780078"), hexToRgb565("#0078ff"), hexToRgb565("#00c8c8"), hexToRgb565("#64ffff"), hexToRgb565("#1e0a50") }; // Twilight
  }
  if (id >= CUSTOM_ID_BASE && id < CUSTOM_ID_BASE + (int)customs.size())
    return customs[id - CUSTOM_ID_BASE].colors;
  return {};   // id 0 (Rainbow) or unresolved
}

std::vector<uint8_t> activeRotationList(uint32_t enabledMask, const std::vector<Palette>& customs) {
  std::vector<uint8_t> list;
  for (int id = 0; id < 32; id++) {
    if (!(enabledMask & (1u << id))) continue;
    bool ok = (id < PRESET_COUNT) ||
              (id >= CUSTOM_ID_BASE && id < CUSTOM_ID_BASE + (int)customs.size());
    if (ok) list.push_back((uint8_t)id);
  }
  if (list.empty()) list.push_back(0);   // Rainbow fallback
  return list;
}
