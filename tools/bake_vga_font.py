#!/usr/bin/env python3
"""Bake the VGA bitmap font used by the greetz scroller (animation id 47).

Source: the WOFF already embedded as base64 in references/greetz.html -- nothing to download.
Font:   WebPlus IBM VGA 9x16, from The Ultimate Oldschool PC Font Pack
        (c) VileR, int10h.org -- CC BY-SA 4.0

RUN WITH SYSTEM python3, NOT the PlatformIO venv python. This needs fontTools + PIL, which the
venv does not have (it has pyserial, which is why tools/flash.py uses it instead):

    python3 tools/bake_vga_font.py

Emits vga_font.h. Glyphs are baked 8 wide, not 9: the VGA 9th column carries only the
box-drawing column-duplication rule (CP437 0xC0..0xDF), which vgaCellBit() synthesizes at
draw time rather than storing as a 9th bit per row.

The header also carries a 5x8 coverage mip for the perspective crawl. Each mip texel counts
the lit dots in one 2x2 source block (0..4) and five 3-bit counts fit in one uint16_t. The
9th-dot bucket duplicates its lone horizontal sample so it stays normalized to four samples like
every other bucket.
This is old-school flash-for-cycles: 3.5 KB buys stable far-field minification without doing the
2x2 filter in the render loop.

Codes 32..255 are decoded through the CP437 codepage, not raw Unicode code points: chr(0xC9)
is 'E' with an acute accent, but CP437 0xC9 is the box-drawing double corner glyph at U+2554.
bytes([code]).decode("cp437") does that mapping.
"""
import base64
import io
import re
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
HTML = ROOT / "references" / "greetz.html"
OUT = ROOT / "vga_font.h"

FIRST, LAST = 32, 255
CELL_W, CELL_H = 8, 16
SIZE = 16          # unitsPerEm 1600, ascender 1200 -> baseline lands on row 12 of a 16-row cell
THRESHOLD = 127


def load_font() -> Path:
    html = HTML.read_text()
    m = re.search(r'url\("data:font/woff;base64,([A-Za-z0-9+/=]+)"\)', html)
    if not m:
        sys.exit(f"no embedded WOFF found in {HTML}")
    woff = base64.b64decode(m.group(1))
    f = TTFont(io.BytesIO(woff))
    f.flavor = None                      # WOFF -> plain TTF so PIL can rasterize it
    ttf = ROOT / "tmp" / "vga_font.ttf"
    ttf.parent.mkdir(exist_ok=True)      # tmp/ is gitignored
    f.save(ttf)
    return ttf


def bake(ttf: Path):
    face = ImageFont.truetype(str(ttf), SIZE)
    rows_per_glyph = []
    for code in range(FIRST, LAST + 1):
        # Rasterize into an oversized probe box so overflow is detectable rather than silently cropped.
        ch = bytes([code]).decode("cp437")
        img = Image.new("L", (CELL_W * 2, CELL_H + 8), 0)
        ImageDraw.Draw(img).text((0, 0), ch, font=face, fill=255)
        px = img.load()
        for y in range(img.height):
            for x in range(img.width):
                if px[x, y] <= THRESHOLD:
                    continue
                # CP437 0xC0..0xDF is the VGA 9-dot line/box-drawing range: real VGA hardware
                # duplicates column 7 (0-indexed) into a 9th dot for exactly these codes, and this
                # font rasterises that 9th dot too. vga_draw.h's vgaCellBit() synthesizes that
                # column at draw time from the baked column 7 instead of storing a 9th bit per row
                # (see VGA_CELL_W there), so ink at column CELL_W for these codes is expected and
                # is intentionally dropped here -- not a geometry bug. Anything else outside the
                # cell still fails loudly.
                if x == CELL_W and y < CELL_H and 0xC0 <= code <= 0xDF:
                    continue
                if x >= CELL_W or y >= CELL_H:
                    sys.exit(f"glyph {code} ({ch!r}) has ink at ({x},{y}), outside the "
                             f"{CELL_W}x{CELL_H} cell -- the bake geometry is wrong")
        rows = []
        for y in range(CELL_H):
            bits = 0
            for x in range(CELL_W):
                if px[x, y] > THRESHOLD:
                    bits |= 0x80 >> x       # MSB-first: bit 7 is the leftmost column
            rows.append(bits)
        rows_per_glyph.append((code, rows))
    return rows_per_glyph


def mip_rows(code: int, rows):
    """Pack the glyph's 5x8 2x2 coverage mip as eight uint16_t rows."""
    def source_bit(x: int, y: int) -> int:
        if x == CELL_W:
            return int(0xC0 <= code <= 0xDF and (rows[y] & 0x01) != 0)
        return int((rows[y] & (0x80 >> x)) != 0)

    packed_rows = []
    for my in range(CELL_H // 2):
        packed = 0
        for mx in range((CELL_W + 2) // 2):
            x0 = mx * 2
            # The synthesized ninth dot has no tenth neighbour. Duplicate it so its bucket still
            # represents four samples and a solid CP437 join keeps coverage 4, not 2.
            x1 = x0 + 1 if x0 + 1 <= CELL_W else x0
            coverage = sum(source_bit(x, y)
                           for y in (my * 2, my * 2 + 1)
                           for x in (x0, x1))
            packed |= coverage << (mx * 3)
        packed_rows.append(packed)
    return packed_rows


def emit(glyphs):
    n = len(glyphs)
    out = [
        "// GENERATED FILE -- do not edit by hand.",
        "// Regenerate: python3 tools/bake_vga_font.py   (system python3, needs fontTools + PIL)",
        "//",
        "// WebPlus IBM VGA 9x16, from The Ultimate Oldschool PC Font Pack",
        "// (c) VileR, int10h.org -- CC BY-SA 4.0",
        "//",
        "// CP437 32..255 baked into an 8x16 cell, one byte per row, MSB-first (bit 7 = leftmost",
        "// column). No PROGMEM: ESP32 flash is memory-mapped so it buys nothing, and PROGMEM is",
        "// undefined on the host, where the native test build compiles this same header.",
        "#pragma once",
        "#include <cstdint>",
        "",
        f"constexpr uint8_t VGA_FONT_FIRST = {FIRST};",
        f"constexpr uint8_t VGA_FONT_LAST  = {LAST};",
        f"constexpr int     VGA_FONT_W     = {CELL_W};",
        f"constexpr int     VGA_FONT_H     = {CELL_H};",
        f"constexpr int     VGA_MIP_W      = {(CELL_W + 2) // 2};",
        f"constexpr int     VGA_MIP_H      = {CELL_H // 2};",
        "",
        "// The VGA 9-dot text cell. VGA_FONT_W above is the BITMAP width (8) and must stay 8 --",
        "// bounce_splash.h derives its glyph radius and arc spacing from it. VGA_CELL_W is the",
        "// ADVANCE width, used by callers that want authentic CP437 line-drawing. Kept here rather",
        "// than in vga_draw.h (which pulls in Arduino_GFX_Library.h) so pure/host-side consumers",
        "// like nfo_crawl.h can get it without an Arduino dependency.",
        f"constexpr int     VGA_CELL_W     = {CELL_W + 1};",
        "",
        f"inline const uint8_t VGA_FONT[{n}][{CELL_H}] = {{",
    ]
    for code, rows in glyphs:
        body = ",".join(f"0x{b:02X}" for b in rows)
        label = "space" if code == 32 else repr(bytes([code]).decode("cp437"))
        out.append(f"  {{{body}}},  // {code} {label}")
    out += [
        "};",
        "",
        "// Half-resolution 2x2 coverage mip for the perspective crawl. Each uint16_t packs five",
        "// 3-bit counts (0..4), left to right. It is generated from the exact full-resolution",
        "// rows above, including the CP437 ninth-dot join rule; never edit this table by hand.",
        f"inline const uint16_t VGA_FONT_MIP[{n}][{CELL_H // 2}] = {{",
    ]
    for code, rows in glyphs:
        body = ",".join(f"0x{p:04X}" for p in mip_rows(code, rows))
        label = "space" if code == 32 else repr(bytes([code]).decode("cp437"))
        out.append(f"  {{{body}}},  // {code} {label}")
    out += [
        "};",
        "",
        "// Real VGA hardware in 9-dot mode duplicates column 8 into column 9 for codes 0xC0..0xDF",
        "// and blanks it for everything else. That rule is exactly what makes the box-drawing",
        "// characters join up, and synthesising it here is cheaper than storing a 9th bit per row",
        "// (which would force the font rows to uint16_t and break test_greetz's `const uint8_t*`",
        "// glyph type). Lives here, not in vga_draw.h, for the same reason as VGA_CELL_W above:",
        "// vga_draw.h pulls in Arduino_GFX_Library.h, which would keep this out of the host test",
        "// suite -- and a wrong range here compiles clean and only shows up as broken box joins",
        "// on the panel.",
        "inline bool vgaCellBit(uint8_t code, int col, int row) {",
        "  if (code < VGA_FONT_FIRST || row < 0 || row >= VGA_FONT_H) return false;",
        "  const uint8_t bits = VGA_FONT[code - VGA_FONT_FIRST][row];",
        "  if (col == 8) return (code >= 0xC0 && code <= 0xDF) && (bits & 0x01);",
        "  if (col < 0 || col > 7) return false;",
        "  return (bits & (0x80 >> col)) != 0;",
        "}",
        "",
        "// All nine dots of one source row, bit 8 = leftmost and bit 0 = synthesized ninth dot.",
        "// The crawl expands one of these per character before scanning a projected output row,",
        "// so its pixel loop is a mask test rather than another font-table lookup.",
        "inline uint16_t vgaCellRowBits(uint8_t code, int row) {",
        "  if (code < VGA_FONT_FIRST || row < 0 || row >= VGA_FONT_H) return 0;",
        "  uint8_t bits = VGA_FONT[code - VGA_FONT_FIRST][row];",
        "  uint16_t out = (uint16_t)bits << 1;",
        "  if (code >= 0xC0 && code <= 0xDF && (bits & 0x01)) out |= 1;",
        "  return out;",
        "}",
        "",
        "// Coverage of the 2x2 source block containing full-resolution dot (col,row), 0..4.",
        "// Callers keep their original 9x16 coordinates, so switching to the mip never changes",
        "// cell geometry. The generated data already includes the ninth-dot CP437 join rule.",
        "inline uint8_t vgaMipCoverage(uint8_t code, int col, int row) {",
        "  if (code < VGA_FONT_FIRST || col < 0 || col >= VGA_CELL_W || row < 0 || row >= VGA_FONT_H) return 0;",
        "  uint16_t packed = VGA_FONT_MIP[code - VGA_FONT_FIRST][row >> 1];",
        "  return (uint8_t)((packed >> ((col >> 1) * 3)) & 0x07);",
        "}",
        "",
    ]
    OUT.write_text("\n".join(out))
    full_bytes = n * CELL_H
    mip_bytes = n * (CELL_H // 2) * 2
    print(f"wrote {OUT} -- {n} glyphs, {full_bytes} font bytes + {mip_bytes} mip bytes")


if __name__ == "__main__":
    emit(bake(load_font()))
