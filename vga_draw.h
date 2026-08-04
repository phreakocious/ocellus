#pragma once
#include <Arduino_GFX_Library.h>
#include "vga_font.h"

// One VGA_FONT glyph, TOP-LEFT at (x,y), drawn as scale x scale blocks in `col`.
//
// Lifted out of the greetz scroller (id 47) so the boot bounce splash can share it rather than
// carry a second copy of the same loop (spec 2026-08-02). Greetz calls it with scale 2 and its
// phosphor colour, which is byte-for-byte what its own loop did.
//
// Characters outside the baked range draw nothing, matching greetz's original guard.
//
// ponytail: fillRect clips against the canvas bounds for us, which is what keeps a partly
// offscreen glyph from writing outside the framebuffer. Direct framebuffer writes would be
// faster but would need that clip written by hand -- revisit only if this measures hot.
inline void vgaBlit(Arduino_Canvas* c, char ch, int x, int y, int scale, uint16_t col) {
  uint8_t u = (uint8_t)ch;
  if (u < VGA_FONT_FIRST || u > VGA_FONT_LAST) return;
  const uint8_t* rows = VGA_FONT[u - VGA_FONT_FIRST];
  for (int r = 0; r < VGA_FONT_H; r++) {
    uint8_t bits = rows[r];
    if (!bits) continue;
    for (int b = 0; b < VGA_FONT_W; b++)
      if (bits & (0x80 >> b)) c->fillRect(x + b * scale, y + r * scale, scale, scale, col);
  }
}

// VGA_CELL_W (the 9-dot advance width) and vgaCellBit (the 9-dot join rule) now live in
// vga_font.h, included above -- moved there so pure/host-side code (nfo_crawl.h, and the host
// test suite) can reach them without this file's Arduino_GFX_Library.h dependency.
