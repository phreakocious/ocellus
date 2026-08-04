#pragma once
#include <cstdint>
#include <cmath>
#include "nfo.h"
#include "vga_font.h"   // VGA_CELL_W, VGA_FONT_H -- both pure, no Arduino dependency

// Perspective table for the .nfo crawl. Pure and Arduino-free so the host suite drives it.
//
// Mode-7 style: with the horizon NFO_H_HORIZON px ABOVE the screen top, distance goes as
// 1/(y+H), so scale(y) = (y+H)/D -- linear in the screen row, which is why the whole
// projection collapses to one 240-entry table computed once at mode entry.
//
// The two constants come from two legibility anchors, not from taste:
//   scale(120) = 0.833  -- a 32-column box is exactly 240px wide here, so the ║ borders
//                          survive down to the equator before the round mask clips them
//   scale(0)   = 0.45   -- the far end; below this an 8px glyph is mush, so rows past it fade
// Solving (120+H)/D = 0.833 and H/D = 0.45 gives D = 313, H = 141.

constexpr int NFO_SCREEN    = 240;
constexpr int NFO_H_HORIZON = 141;
constexpr int NFO_D         = 313;
// Rows below this scale fade out rather than dissolving into sampling noise.
//
// The fade is normalised over [scale(0), threshold], NOT [0, threshold]. Over [0, threshold] the
// ratio bottoms out at scale(0)/threshold, so the old 0.55 threshold left the horizon row sitting
// at 0.669 brightness -- it never faded at all, it just dimmed slightly. That is precisely where
// the projection is worst: at scale 0.45 one screen row spans 1/0.45 = 2.2 source rows, so a
// 1px glyph stroke pops in and out as the text scrolls (point-sampling a minified image), and at
// two-thirds brightness the shimmer was the most visible thing on screen.
//
// 0.586 puts the ramp at rows 0..42. It was briefly 0.656 (rows 0..64) when the fade was the only
// defence against the far-end flicker, and that read as too dark -- the vertical coverage taps and
// the faded scanlines below now handle the flicker, so the fade only has to cover the last few
// rows where the glyph really is mush. Raise to push the horizon further down the glass.
constexpr int NFO_FADE_Q8   = 150;   // 0.586 in Q8

// Switch to the baked 2x2 coverage mip wherever a screen pixel spans roughly 1.5 or more source
// dots. 0.668 is the same boundary at which the old round(1/scale) coverage sampler went from one
// tap to two, but the mip pays one filtered lookup instead of OR-ing multiple full-resolution rows.
constexpr int NFO_MIP_Q8 = 171;   // 0.668 in Q8; top ~69 screen rows

struct NfoRow {
  int32_t srcYQ8;      // source Y offset for this screen row, Q8, before the scroll is added
  int32_t invColQ16;   // source COLUMNS per screen pixel, Q16 -- the reciprocal, so the
                       // per-pixel loop steps instead of dividing
  int32_t halfWidth;   // half the projected row width in screen px
  uint8_t bright;      // 0..255, folds the far-end fade and the scanline dim together
  uint8_t mip;         // use VGA_FONT_MIP's 2x2 coverage texels on this minified row
};

inline void nfoBuildTable(NfoRow out[NFO_SCREEN]) {
  const int rowPx = NFO_COLS * VGA_CELL_W;         // 288 at full scale
  for (int y = 0; y < NFO_SCREEN; y++) {
    // float here is fine: this runs ONCE per mode entry, never per frame or per pixel.
    float s = (float)(y + NFO_H_HORIZON) / (float)NFO_D;
    out[y].srcYQ8    = (int32_t)((y / s) * 256.0f);
    out[y].invColQ16 = (int32_t)((1.0f / (s * VGA_CELL_W)) * 65536.0f);
    out[y].halfWidth = (int32_t)(rowPx * s * 0.5f);
    const float thresh = NFO_FADE_Q8 / 256.0f;
    const float far    = (float)NFO_H_HORIZON / (float)NFO_D;   // scale(0), the smallest scale drawn
    float b = 1.0f;
    if (s < thresh) { b = (s - far) / (thresh - far); if (b < 0.0f) b = 0.0f; b *= b; }
    // Scanlines are a NEAR-field effect. At the far end a screen row already spans 2+ source rows,
    // so dimming every other one throws away half of the little vertical resolution left, and it
    // costs brightness exactly where the fade band can least afford it. Scale the scanline depth
    // by the fade itself: full 0.55 dim at full brightness, none at the horizon.
    if (y & 1) b *= 1.0f - 0.45f * b;
    out[y].bright = (uint8_t)(b * 255.0f);

    // The far field consumes a build-time-filtered font mip. It preserves partial strokes as
    // coverage instead of OR-ing them into thicker glyphs, and trades the old 2-3 hot-loop font
    // probes for one packed-table lookup (two only while interpolating between mip rows).
    out[y].mip = (uint8_t)(s <= (NFO_MIP_Q8 / 256.0f));
  }
}

// Total scroll distance for a pass: the text height plus the SOURCE span the screen covers,
// which perspective makes ~197px -- not the 240px screen height. Getting this wrong makes
// every duration wrong.
//
// Takes an already-built table so a caller with one in hand (the render path keeps gNfoTable
// live for the whole mode) never pays for a second 240-entry float rebuild just to read this.
inline int32_t nfoTraversal(const NfoRow* t, int lines) {
  return lines * VGA_FONT_H + ((t[NFO_SCREEN-1].srcYQ8 - t[0].srcYQ8) >> 8);
}

// Convenience overload for callers with no table in hand (host tests, mostly). Builds a
// throwaway table on the stack -- fine off the render path; use the (table, lines) overload
// above anywhere a table already exists.
inline int32_t nfoTraversal(int lines) {
  NfoRow t[NFO_SCREEN]; nfoBuildTable(t);
  return nfoTraversal(t, lines);
}
