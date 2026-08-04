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
constexpr int NFO_FADE_Q8   = 141;   // 0.55 in Q8

struct NfoRow {
  int32_t srcYQ8;      // source Y offset for this screen row, Q8, before the scroll is added
  int32_t invColQ16;   // source COLUMNS per screen pixel, Q16 -- the reciprocal, so the
                       // per-pixel loop steps instead of dividing
  int32_t halfWidth;   // half the projected row width in screen px
  uint8_t bright;      // 0..255, folds the far-end fade and the scanline dim together
};

inline void nfoBuildTable(NfoRow out[NFO_SCREEN]) {
  const int rowPx = NFO_COLS * VGA_CELL_W;         // 288 at full scale
  for (int y = 0; y < NFO_SCREEN; y++) {
    // float here is fine: this runs ONCE per mode entry, never per frame or per pixel.
    float s = (float)(y + NFO_H_HORIZON) / (float)NFO_D;
    out[y].srcYQ8    = (int32_t)((y / s) * 256.0f);
    out[y].invColQ16 = (int32_t)((1.0f / (s * VGA_CELL_W)) * 65536.0f);
    out[y].halfWidth = (int32_t)(rowPx * s * 0.5f);
    float b = s >= (NFO_FADE_Q8 / 256.0f) ? 1.0f
                                          : (s / (NFO_FADE_Q8 / 256.0f)) * (s / (NFO_FADE_Q8 / 256.0f));
    if (y & 1) b *= 0.55f;                          // scanlines
    out[y].bright = (uint8_t)(b * 255.0f);
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
