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

// VGA_CELL_W (the 9-dot advance width) now lives in vga_font.h, included above -- moved there
// so pure/host-side code can reach it without this file's Arduino_GFX_Library.h dependency.

// Real VGA hardware in 9-dot mode duplicates column 8 into column 9 for codes 0xC0..0xDF and
// blanks it for everything else. That rule is exactly what makes the box-drawing characters join
// up, and synthesising it here is cheaper than storing a 9th bit per row (which would force the
// font rows to uint16_t and break test_greetz's `const uint8_t*` glyph type).
inline bool vgaCellBit(uint8_t code, int col, int row) {
  if (code < VGA_FONT_FIRST || row < 0 || row >= VGA_FONT_H) return false;
  const uint8_t bits = VGA_FONT[code - VGA_FONT_FIRST][row];
  if (col == 8) return (code >= 0xC0 && code <= 0xDF) && (bits & 0x01);
  if (col < 0 || col > 7) return false;
  return (bits & (0x80 >> col)) != 0;
}
