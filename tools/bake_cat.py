#!/usr/bin/env python3
"""Bake LuizMelo Pet-Cats-Pack strips -> kitty_anim.h at native resolution, lossless.

The pack (CC0, https://luizmelo.itch.io/pet-cat-pack) is at references/Pet Cats Pack/Cat-N/
Cat-N-<Anim>.png, each PNG a horizontal strip of 50x50 frames. references/ is gitignored (untracked
reference art, like the old kitty_cat.png); only the baked kitty_anim.h at repo root is committed.
This script (like tools/toaster_convert.py) is tracked.

The frames are 50x50 but the cat content across every frame of every cat lives in a 25x17 window at
(11,15), bottom-anchored (measured: union alpha bbox is exactly x 11-35, y 15-31) -- that 25x17 IS
the native art, so one fixed crop keeps the cat aligned across anims and variants. Cat-3 has no Itch
strip -> its ITCH slot aliases its own Stretching (the idle-quirk gate picks itch OR stretch anyway;
symbol reuse = 0 flash).

NO UPSCALING, NO INVENTED DETAIL. An earlier bake ran scale2x + procedural rim/inner shading + eye
highlights to fake a 2x "detailed" cat. It looked worse than the raw art at every step: scale2x
rounds the ear tips and bulges the silhouette, the rim pass painted a bright stripe along the whole
back, and the shading passes pushed each cat past 15 colours so the 4bpp quantiser then had to crush
them. 25x17 with 8-13 colours has no detail to recover -- nothing to interpolate from -- so the only
honest options are crisp integer nearest-neighbour (this) or bigger source art. Blit x8 on-device.

FORMAT: 4bpp indexed, per-variant 16-entry RGB565 palette, slot 0 reserved as the transparent key.
Every variant uses 8-13 distinct RGB565 colours, so the 15 real slots hold them EXACTLY -- the bake
is lossless and there is no quantiser to tune. ART_W is padded to an even CELL_W so each row packs to
whole bytes; the pad column is transparent and rides off the content-centred blit.

Run:  python3 tools/bake_cat.py   ->  writes kitty_anim.h and prints the flash size.
Three self-checks, all fail loud: the per-strip frame-count assertion, the <=15-colour palette
assertion, and a pack/unpack round-trip in pack() that mirrors blitCatFrame's own nibble indexing."""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "references", "Pet Cats Pack")
OUT = os.path.join(HERE, "..", "kitty_anim.h")
SRC_FRAME = 50                       # source frames are 50x50
CROP_X0, CROP_Y0 = 11, 15            # common opaque window (measured over all frames)
ART_W, ART_H = 25, 17                # native art size inside that window
CELL_W = ART_W + ART_W % 2           # 26: even so 4bpp rows pack to whole bytes
CELL_H = ART_H
PAL_N = 16                           # 4bpp; slot 0 is the transparent key
KEY_IDX = 0

T = None                             # transparent marker in the working grids

# (enum name, filename suffix, frame count, playback mode, real-ms frame duration)
ANIMS = [
    ("IDLE",       "Idle",       10, "CP_LOOP", 150),
    ("MEOW",       "Meow",        4, "CP_LOOP", 130),
    ("SLEEPING",   "Sleeping1",   1, "CP_HOLD", 1000),
    ("LICKING",    "Licking 1",   5, "CP_ONCE", 120),
    ("STRETCHING", "Stretching", 13, "CP_ONCE", 110),
    ("ITCH",       "Itch",        2, "CP_ONCE", 150),
]
VARIANTS = ["Cat-1", "Cat-2", "Cat-3", "Cat-4", "Cat-5", "Cat-6"]
STRETCH_IDX = 4   # index into ANIMS, the fallback for a missing Itch


def rgb565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def snap565(c):
    """round-trip through RGB565 so colours collapse in the space the panel actually has"""
    v = rgb565(c)
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def crop(strip, fi):
    """one native frame as a CELL_W x CELL_H grid of snapped RGB tuples / T"""
    x0 = fi * SRC_FRAME + CROP_X0
    px = strip.crop((x0, CROP_Y0, x0 + CELL_W, CROP_Y0 + CELL_H)).load()
    return [[(snap565(px[x, y][:3]) if px[x, y][3] >= 128 else T) for x in range(CELL_W)]
            for y in range(CELL_H)]


def palettize(frames):
    """exact palette: every distinct colour in the variant gets its own slot.
    Returns (palette as RGB565 words with slot 0 = key, per-frame index grids)."""
    pal = sorted({c for g in frames for row in g for c in row if c is not T})
    assert len(pal) <= PAL_N - 1, f"{len(pal)} colours > {PAL_N - 1} slots -- needs a quantiser again"
    idx = {c: i + 1 for i, c in enumerate(pal)}   # +1: slot 0 is the transparent key
    grids = [[[KEY_IDX if c is T else idx[c] for c in row] for row in g] for g in frames]
    return [0] + [rgb565(c) for c in pal], grids


def pack(grid):
    """4bpp, two pixels per byte, high nibble first. CELL_W is even so rows never straddle.
    Round-trips against blitCatFrame's own indexing so a packing slip fails here, not on-device."""
    flat = [i for row in grid for i in row]
    data = [(flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2)]
    for n, want in enumerate(flat):
        b = data[n >> 1]
        assert (b & 0x0F if n & 1 else b >> 4) == want, f"4bpp pack/unpack mismatch at {n}"
    return data


def main():
    lines = ["// GENERATED by tools/bake_cat.py -- DO NOT HAND-EDIT.",
             "#pragma once", "#include <stdint.h>", "",
             "enum CatAnim { CA_IDLE=0, CA_MEOW, CA_SLEEPING, CA_LICKING, CA_STRETCHING, CA_ITCH, CA_COUNT };",
             "enum CatPlay { CP_LOOP=0, CP_HOLD, CP_ONCE };",
             f"static const int CAT_CELL_W = {CELL_W}, CAT_CELL_H = {CELL_H};",
             f"// CAT_ART_W is the real content width; CELL_W pads it even for 4bpp packing. Centre on ART_W.",
             f"static const int CAT_ART_W = {ART_W};",
             "// 4bpp indexed, high nibble = even x. Palette slot 0 is the transparent key.",
             f"static const uint8_t CAT_KEY_IDX = {KEY_IDX};",
             f"static const int CAT_FRAME_BYTES = {CELL_W * CELL_H // 2};",
             "struct CatAnimDef { const uint8_t* frames; uint8_t nframes; uint16_t durMs; uint8_t mode; };", ""]
    table = []      # table[vi][ai] = (symbol, nframes, dur, mode)
    palettes = []
    for vi, folder in enumerate(VARIANTS):
        # pass 1: load every native frame of this variant (the palette is per-variant, not per-anim)
        strips = {}
        for name, suffix, nframes, mode, dur in ANIMS:
            path = os.path.join(SRC, folder, f"{folder}-{suffix}.png")
            if not os.path.exists(path):
                continue
            im = Image.open(path).convert("RGBA")
            got = im.width // SRC_FRAME
            assert im.width % SRC_FRAME == 0 and got == nframes, \
                f"{folder}-{suffix}: width {im.width}/{SRC_FRAME}={got}, expected {nframes}"
            strips[name] = [crop(im, fi) for fi in range(nframes)]

        # pass 2: one exact palette for the whole variant
        order = [n for n, *_ in ANIMS if n in strips]
        pal, grids = palettize([g for n in order for g in strips[n]])
        palettes.append(pal)

        row = [None] * len(ANIMS)
        k = 0
        for name in order:
            n = len(strips[name])
            ai = next(i for i, a in enumerate(ANIMS) if a[0] == name)
            _, _, _, mode, dur = ANIMS[ai]
            data = [b for g in grids[k:k + n] for b in pack(g)]
            k += n
            sym = f"cat{vi}_{name.lower()}"
            lines.append(f"static const uint8_t {sym}[] = {{{','.join(f'0x{b:02X}' for b in data)}}};")
            row[ai] = (sym, n, dur, mode)
        # resolve any missing anim -> alias Stretching (only Cat-3 Itch, in practice)
        for ai, (name, suffix, nframes, mode, dur) in enumerate(ANIMS):
            if row[ai] is None:
                s = ANIMS[STRETCH_IDX]
                row[ai] = (f"cat{vi}_{s[0].lower()}", s[2], s[4], s[3])
        table.append(row)
        lines.append("")

    lines.append(f"static const uint16_t CAT_PAL[6][{PAL_N}] = {{")
    for pal in palettes:
        pal = pal + [0] * (PAL_N - len(pal))   # unused tail slots (fewer than 15 real colours)
        lines.append("  {" + ",".join(f"0x{c:04X}" for c in pal) + "},")
    lines.append("};")
    lines.append("")
    lines.append("static const CatAnimDef CAT_ANIM[6][CA_COUNT] = {")
    for row in table:
        cells = ", ".join(f"{{{sym},{nf},{dur},{mode}}}" for (sym, nf, dur, mode) in row)
        lines.append(f"  {{ {cells} }},")
    lines.append("};")
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    total = sum(nf * CELL_W * CELL_H // 2 for row in table for (_, nf, _, _) in row)
    total += 6 * PAL_N * 2
    print(f"wrote {OUT}  (~{total // 1024} KB flash; cell {CELL_W}x{CELL_H}, exact {PAL_N}-colour 4bpp)")


if __name__ == "__main__":
    main()
