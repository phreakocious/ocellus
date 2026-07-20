#pragma once
#include <cstdint>
#include <algorithm>

// Torus-scroll a flat RGB565 framebuffer by (dx, dy) with wraparound. +dx shifts content
// toward larger x (column c -> c+dx, last column wraps to 0); +dy toward larger y. Used by
// the EV_WANDER_OFF rare eye event: render the eye centered, then roll the finished frame so
// it slides off one edge and reappears from the opposite. Pure / Arduino-free -> host-tested.
//
// std::rotate does the work in place with no scratch buffer: rotating the whole buffer by
// dy*W elements is a vertical whole-row roll; per-row rotate is the horizontal roll.
inline void rollFramebuffer(uint16_t* fb, int W, int H, int dx, int dy) {
  dx = ((dx % W) + W) % W;
  dy = ((dy % H) + H) % H;
  if (dy) std::rotate(fb, fb + (H - dy) * W, fb + W * H);   // +dy: content moves down
  if (dx)
    for (int r = 0; r < H; r++)
      std::rotate(fb + r * W, fb + r * W + (W - dx), fb + r * W + W);   // +dx: content moves right
}
