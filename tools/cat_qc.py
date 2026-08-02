#!/usr/bin/env python3
"""kitty_anim.h -> cat_qc.html: every cat x every animation, playing at the real frame durations.

QC target is the BAKED HEADER, not the source PNGs -- it decodes the 4bpp nibbles and the per-variant
palette exactly the way treatcat.cpp's blitCatFrame does, so a packing or palette bug shows up here
instead of on the device. Cat-3's ITCH slot aliases its Stretching in CAT_ANIM; reading the table
(rather than the file list) means the page shows that alias too.

Self-contained: one HTML file with the spritesheets inlined as data URIs, so file:// works. Playback
is pure CSS steps() -- no JS beyond the one-line backdrop picker.

Run:  python3 tools/cat_qc.py [out.html]   (default cat_qc.html at the repo root)
"""
import base64
import io
import os
import re
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = os.path.join(HERE, "..", "kitty_anim.h")
SCALE = 8          # matches treatcat.cpp CAT_SCALE -> 208x136, the on-device size
ANIM_NAMES = ["Idle", "Meow", "Sleeping", "Licking", "Stretching", "Itch"]


def parse(path):
    h = open(path).read()
    cw = int(re.search(r"CAT_CELL_W = (\d+)", h).group(1))
    ch = int(re.search(r"CAT_CELL_H = (\d+)", h).group(1))
    fb = int(re.search(r"CAT_FRAME_BYTES = (\d+)", h).group(1))
    blk = h[h.index("CAT_PAL[6]"):]
    blk = blk[:blk.index("};")]
    pal = [[int(x, 16) for x in row.split(",")] for row in re.findall(r"\{(0x[^}]+)\}", blk)]
    arrs = {m.group(1): bytes(int(b, 16) for b in m.group(2).split(","))
            for m in re.finditer(r"static const uint8_t (cat\d+_\w+)\[\] = \{([^}]+)\};", h)}
    blk = h[h.index("CAT_ANIM[6]"):]
    blk = blk[:blk.index("\n};")]
    table = [re.findall(r"\{(\w+),(\d+),(\d+),(CP_\w+)\}", row)
             for row in blk.split("\n") if "{cat" in row]
    assert len(pal) == 6 and len(table) == 6, (len(pal), len(table))
    return cw, ch, fb, pal, arrs, table


def sheet(data, pal, cw, ch, fb, n):
    """one horizontal strip of n frames, 1x -- CSS scales it with image-rendering:pixelated"""
    im = Image.new("RGBA", (cw * n, ch), (0, 0, 0, 0))
    px = im.load()
    for fi in range(n):
        f = data[fi * fb:(fi + 1) * fb]
        for sy in range(ch):
            for sx in range(cw):
                b = f[(sy * cw + sx) >> 1]
                idx = (b & 0x0F) if (sx & 1) else (b >> 4)
                if not idx:
                    continue          # slot 0 = transparent key
                c = pal[idx]
                px[fi * cw + sx, sy] = ((((c >> 11) & 31) * 255 // 31),
                                        (((c >> 5) & 63) * 255 // 63),
                                        ((c & 31) * 255 // 31), 255)
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


CSS = """
:root { color-scheme: dark; }
body { margin:0; padding:24px; background:#15161a; color:#d6d8de;
       font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace; }
h1 { font-size:15px; font-weight:600; margin:0 0 4px; }
.meta { color:#7c8090; margin-bottom:18px; }
.bar { display:flex; gap:18px; align-items:center; margin-bottom:22px; flex-wrap:wrap; }
label { display:flex; gap:6px; align-items:center; color:#9aa0ae; }
.grid { display:grid; grid-template-columns:repeat(6,max-content); gap:10px; overflow-x:auto; }
figure { margin:0; }
figcaption { color:#7c8090; padding:5px 2px 0; white-space:nowrap; }
figcaption b { color:#cdd1da; font-weight:600; }
.cell { border:1px solid #2a2d36; border-radius:6px; background:var(--bg,#22242b); }
/* the sprite itself: background-position stepped across the strip, one step per frame */
.s { image-rendering:pixelated; background-repeat:no-repeat; animation-iteration-count:infinite; }
body:has(#pause:checked) .s { animation-play-state:paused; }
body:has(#chk:checked) .cell {
  background-image:linear-gradient(45deg,#3a3d47 25%,transparent 25%,transparent 75%,#3a3d47 75%),
                   linear-gradient(45deg,#3a3d47 25%,transparent 25%,transparent 75%,#3a3d47 75%);
  background-size:16px 16px; background-position:0 0,8px 8px; }
.row-label { grid-column:1/-1; color:#8b91a1; margin:10px 0 -2px; font-weight:600; }
"""


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "cat_qc.html")
    cw, ch, fb, pal, arrs, table = parse(HDR)
    w, h = cw * SCALE, ch * SCALE

    keys, cells = [], []
    for vi, row in enumerate(table):
        cells.append(f'<div class="row-label">Cat-{vi + 1}</div>')
        for ai, (sym, n, dur, mode) in enumerate(row):
            n, dur = int(n), int(dur)
            uri = sheet(arrs[sym], pal[vi], cw, ch, fb, n)
            k = f"k{vi}_{ai}"
            keys.append(f"@keyframes {k}{{from{{background-position-x:0}}"
                        f"to{{background-position-x:-{n * w}px}}}}")
            alias = " <i>(alias)</i>" if not sym.endswith(ANIM_NAMES[ai].lower()) else ""
            cells.append(
                f'<figure><div class="cell"><div class="s" style="width:{w}px;height:{h}px;'
                f'background-image:url({uri});background-size:{n * w}px {h}px;'
                f'animation:{k} {n * dur}ms steps({n}) infinite"></div></div>'
                f'<figcaption><b>{ANIM_NAMES[ai]}</b>{alias}<br>{n}f &middot; {dur}ms &middot; '
                f'{mode[3:].lower()}</figcaption></figure>')

    total = sum(int(n) * fb for row in table for (_, n, _, _) in row) + 6 * len(pal[0]) * 2
    html = f"""<!doctype html><meta charset=utf-8><title>cat QC</title><style>{CSS}
{chr(10).join(keys)}</style>
<h1>treatcat sprite QC</h1>
<div class=meta>kitty_anim.h &middot; {cw}&times;{ch} cells at &times;{SCALE} = {w}&times;{h} on screen
&middot; 4bpp, {len(pal[0])}-colour palette per cat &middot; ~{total // 1024} KB flash</div>
<div class=bar>
  <label><input type=checkbox id=pause> pause all</label>
  <label><input type=checkbox id=chk> checkerboard (show transparency)</label>
  <label>backdrop <input type=color value="#22242b"
    oninput="document.documentElement.style.setProperty('--bg',this.value)"></label>
</div>
<div class=grid>{''.join(cells)}</div>
"""
    with open(out, "w") as f:
        f.write(html)
    print(f"wrote {os.path.abspath(out)}  ({len(html) // 1024} KB, {sum(len(r) for r in table)} clips)")


if __name__ == "__main__":
    main()
