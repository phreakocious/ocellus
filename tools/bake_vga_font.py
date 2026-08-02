#!/usr/bin/env python3
"""Bake the VGA bitmap font used by the greetz scroller (animation id 47).

Source: the WOFF already embedded as base64 in references/greetz.html -- nothing to download.
Font:   WebPlus IBM VGA 9x16, from The Ultimate Oldschool PC Font Pack
        (c) VileR, int10h.org -- CC BY-SA 4.0

RUN WITH SYSTEM python3, NOT the PlatformIO venv python. This needs fontTools + PIL, which the
venv does not have (it has pyserial, which is why tools/flash.py uses it instead):

    python3 tools/bake_vga_font.py

Emits vga_font.h. The glyphs are baked 8 wide, not 9: the VGA 9th column is blank for every
ASCII glyph and exists only to carry the box-drawing characters, which this mode never renders.
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

FIRST, LAST = 32, 126
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
        img = Image.new("L", (CELL_W * 2, CELL_H + 8), 0)
        ImageDraw.Draw(img).text((0, 0), chr(code), font=face, fill=255)
        px = img.load()
        for y in range(img.height):
            for x in range(img.width):
                if px[x, y] > THRESHOLD and (x >= CELL_W or y >= CELL_H):
                    sys.exit(f"glyph {code} ({chr(code)!r}) has ink at ({x},{y}), outside the "
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


def emit(glyphs):
    n = len(glyphs)
    out = [
        "// GENERATED FILE -- do not edit by hand.",
        "// Regenerate: python3 tools/bake_vga_font.py   (system python3, needs fontTools + PIL)",
        "//",
        "// WebPlus IBM VGA 9x16, from The Ultimate Oldschool PC Font Pack",
        "// (c) VileR, int10h.org -- CC BY-SA 4.0",
        "//",
        "// ASCII 32..126 baked into an 8x16 cell, one byte per row, MSB-first (bit 7 = leftmost",
        "// column). No PROGMEM: ESP32 flash is memory-mapped so it buys nothing, and PROGMEM is",
        "// undefined on the host, where the native test build compiles this same header.",
        "#pragma once",
        "#include <cstdint>",
        "",
        f"constexpr uint8_t VGA_FONT_FIRST = {FIRST};",
        f"constexpr uint8_t VGA_FONT_LAST  = {LAST};",
        f"constexpr int     VGA_FONT_W     = {CELL_W};",
        f"constexpr int     VGA_FONT_H     = {CELL_H};",
        "",
        f"inline const uint8_t VGA_FONT[{n}][{CELL_H}] = {{",
    ]
    for code, rows in glyphs:
        body = ",".join(f"0x{b:02X}" for b in rows)
        label = "space" if code == 32 else repr(chr(code))
        out.append(f"  {{{body}}},  // {code} {label}")
    out += ["};", ""]
    OUT.write_text("\n".join(out))
    print(f"wrote {OUT} -- {n} glyphs, {n * CELL_H} bytes")


if __name__ == "__main__":
    emit(bake(load_font()))
