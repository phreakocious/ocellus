#!/usr/bin/env python3
"""Golden: pack_rgb565 must produce exact bytes for known pixels. Run: python3 tools/test_slide_convert.py"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from config_cli import pack_rgb565

rgba = bytearray(240 * 240 * 4)
def setpx(x, y, r, g, b):
    i = (y * 240 + x) * 4
    rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3] = r, g, b, 255
setpx(120, 120, 248, 0, 0)     # center, inside circle -> red  0xF800
setpx(10, 120, 0, 252, 0)      # left,   inside circle -> green 0x07E0
# (0,0) is a corner -> outside circle -> must be cropped to black

out = pack_rgb565(rgba)
def off(x, y): return (y * 240 + x) * 2
assert out[off(120,120):off(120,120)+2] == bytes([0x00, 0xF8]), "center red LE"
assert out[off(10,120):off(10,120)+2]   == bytes([0xE0, 0x07]), "left green LE"
assert out[off(0,0):off(0,0)+2]         == bytes([0x00, 0x00]), "corner cropped"
print("OK: pack_rgb565 golden")
