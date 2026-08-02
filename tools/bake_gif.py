#!/usr/bin/env python3
"""Bake references/meme_gifs/*.gif -> 240x240 / 12 fps / 64-colour GIFs for the GIF player (id 48).

Spec: docs/superpowers/specs/2026-07-26-gif-player-design.md. references/ is gitignored (the 55 MB
of source memes are not in the repo), so THIS FILE is where the curation lives -- KEEP and CUT below
are the decision of 2026-08-01, in code, reviewable in a diff. Baked output is likewise not
committed; it goes to the device over Web Serial.

Every source file must appear in exactly one of KEEP or CUT. Drop a new meme into the source dir and
the script fails loud rather than silently ignoring it -- that assertion is most of the value here.

WHY THESE PARAMETERS (all measured, do not "tune" them back):
  - dither=none is load-bearing. Bayer dithering destroys interframe similarity and cost ~15% more
    bytes across the set for no visible gain at 240 px.
  - stats_mode=diff biases the shared palette toward moving pixels instead of the background.
  - 12 fps, not 8-10 and not 20. The on-device decode spike (2026-08-01, board) measured 38 ms
    worst-case decode + 14 ms flush = 52 ms against 12 fps's 83 ms budget, so 12 holds with room.
    Going faster is affordable in time and NOT in bytes -- frames are the file.
  - 64 colours. The round 240 px panel hides banding that would show on a desktop.

STORAGE REALITY: photographic clips bake at 280-430 KB/s, not the 100-150 KB/s the spec first
guessed (that figure came from the flat cartoon clips). The per-clip cap below is the guard rail;
the printed total against each board's filesystem is the real answer.

Run:  python3 tools/bake_gif.py [--out DIR] [--only NAME ...] [--cap-mb N]
      python3 tools/bake_gif.py --self-check      # bakes one clip, asserts the output is sane
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "references" / "meme_gifs"
OUT_DEFAULT = HERE.parent / "build" / "gifs"

SIZE = 240
FPS = 12
COLORS = 64
CAP_MB = 2.0          # refuse any single clip larger than this; a filesystem-eater is a mistake
TRIM_DEFAULT = 4.0    # seconds; the cap that makes the whole set fit a board

# Filesystem bytes available per board, from the spec's partition tables.
BOARDS = [("waveshare 16MB", 13.9), ("bench S3 8MB", 6.2), ("S3-Zero / C3 4MB", 2.75)]

# --- the curation, decided 2026-08-01 from center-crop previews with the round bezel masked ------
# The deciding factor was background: a near-black surround loses nothing to the bezel and the
# subject glows on the glass; a wide composition with the joke spread across the frame dies.
#
# Per-clip overrides:
#   trim   -- seconds to keep (default: whole clip, capped at TRIM_DEFAULT)
#   start  -- seconds to skip in (default 0)
#   anchor -- horizontal crop anchor: "center" (default), "left", "right"
#   src    -- source file stem, when it differs from the output name (one source -> several clips)
#   region -- (x, y, w, h) sub-rectangle of the source, taken BEFORE the square crop. For multi-panel
#             images: a square crop of a stacked two-panel comic just shows both panels squeezed with
#             the gutter slicing the circle.
KEEP = {
    # Square or near-square: the crop is free, nothing to decide.
    "anya": {}, "cookiemonster": {}, "dumpsterfire": {}, "fiiiine": {}, "nyancat": {},
    "pricks": {}, "sweets": {}, "wave": {}, "waves": {},

    # A stacked two-panel comic (436x500, white gutter at y 246..253). Cropped square it reads as
    # two squeezed panels with the gutter across the middle of the circle, so it ships as the two
    # panels in sequence -- they sort adjacent, and the device lists clips name-sorted, so the setup
    # still lands before the punchline. Each panel is ~436x247, and the square crop of that is a
    # clean read. The "THIS IS FINE" caption on panel 2 clips slightly at the rim; the face is the
    # meme, so that is accepted rather than fixed.
    "thisisfine1": {"src": "thisisfine", "region": (0, 0, 436, 246)},
    "thisisfine2": {"src": "thisisfine", "region": (0, 253, 436, 247)},

    # Over the trim cap; first 4 s reads fine on all three.
    "cupcake": {"trim": 4.0},
    "fine":    {"trim": 4.0},

    # Wide but centered on black -- previewed and confirmed, the bezel takes only dead space.
    "giphy": {}, "jellyfish": {}, "shark": {}, "sniffs": {},

    # 301 frames / 12 s / 4.6 MB baked whole, a third of the board for one clip. Trimmed it is
    # ~1.5 MB and it is the best-looking clip in the set on a round dark panel.
    "seaangel": {"trim": 4.0},

    # The weight of this frame is the monitor and keyboard on the LEFT; a centered crop shaves
    # exactly that and leaves an anonymous yellow blob. Left-anchored keeps the whole gag.
    "spongebobloop": {"anchor": "left"},

    # RIFF/WebP wearing a .gif extension. 374x360 / 78 frames underneath, so it crops fine once the
    # container is dealt with (see webp_to_frames).
    "raccoon": {},
}

# --- sets: which clips go on a given unit ---------------------------------------------------------
# NOT an #ifdef -- these are filesystem contents, nothing here reaches the preprocessor. A set is a
# named list plus the board it targets, and the bake asserts the total fits that board's filesystem.
# That makes hardware sets and per-unit sets the same mechanism: "small" exists because 2.75 MB
# forces it, "kitsune" would exist because someone likes cats. Add custom sets here the same way.
SETS = {
    "full":  dict(board="waveshare 16MB", clips=None),    # None = everything in KEEP
    # The 4 MB boards (S3-Zero console, C3) hold about a tenth of the set. Cheapest-per-byte of the
    # keeps, which happens to skew cartoon -- the photographic ones are exactly the expensive ones.
    "small": dict(board="S3-Zero / C3 4MB", clips=[
        "sniffs", "sweets", "nyancat", "fiiiine", "thisisfine1", "thisisfine2",
        "cookiemonster", "spongebobloop", "giphy", "anya",
    ]),
}

CUT = {
    "welcome":          "text-heavy and wide; unreadable at 240 px behind a round bezel",
    "wehavetechnology": "text-heavy and wide; unreadable at 240 px behind a round bezel",
    "win95":            "text-heavy and wide; unreadable at 240 px behind a round bezel",
    "flyingmanta":      "the wide horizon IS the joke; square crop leaves one manta and empty water",
    "perfecthandloop":  "250x188 source, so 240x240 is an upscale, and the fingers clip the rim",
}


def is_webp(path):
    """The raccoon problem: RIFF container behind a .gif name."""
    with open(path, "rb") as f:
        head = f.read(12)
    return head[:4] == b"RIFF" and head[8:12] == b"WEBP"


def webp_to_frames(path, tmpdir):
    """Explode an animated WebP to PNGs so ffmpeg can take it as an image sequence.

    ffmpeg's animated-WebP demuxing is version-dependent and fails opaquely when absent; PIL reads
    it reliably. Returns (pattern, source_fps).
    """
    from PIL import Image, ImageSequence
    im = Image.open(path)
    n, durations = 0, []
    for fr in ImageSequence.Iterator(im):
        fr.convert("RGB").save(tmpdir / f"f{n:04d}.png")
        durations.append(fr.info.get("duration", 100))
        n += 1
    avg_ms = sum(durations) / len(durations) if durations else 100
    return str(tmpdir / "f%04d.png"), max(1.0, 1000.0 / avg_ms)


def crop_expr(anchor, region=None):
    pre = ""
    if region:
        rx, ry, rw, rh = region
        pre = f"crop={rw}:{rh}:{rx}:{ry},"      # sub-rectangle first; the square crop then sees only it
    side = "min(iw\\,ih)"
    x = {"center": f"(iw-{side})/2", "left": "0", "right": f"iw-{side}"}[anchor]
    return f"{pre}crop={side}:{side}:{x}:(ih-{side})/2"


def bake(name, opts, outdir, cap_mb):
    src = SRC / f"{opts.get('src', name)}.gif"
    if not src.exists():
        raise SystemExit(f"missing source: {src}")
    dst = outdir / f"{name}.gif"

    chain = (f"{crop_expr(opts.get('anchor', 'center'), opts.get('region'))},"
             f"scale={SIZE}:{SIZE}:flags=lanczos,fps={FPS},"
             f"split[a][b];[a]palettegen=max_colors={COLORS}:stats_mode=diff[p];"
             f"[b][p]paletteuse=dither=none")

    tmp = None
    try:
        if is_webp(src):
            tmp = Path(tempfile.mkdtemp())
            pattern, sfps = webp_to_frames(src, tmp)
            src_args = ["-framerate", f"{sfps:.3f}", "-i", pattern]
        else:
            src_args = ["-i", str(src)]

        pre = []
        if opts.get("start"):
            pre += ["-ss", str(opts["start"])]
        trim = opts.get("trim", TRIM_DEFAULT)
        pre += ["-t", str(trim)]

        cmd = ["ffmpeg", "-v", "error", "-y", *pre, *src_args,
               "-lavfi", chain, "-loop", "0", str(dst)]
        subprocess.run(cmd, check=True)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)

    mb = dst.stat().st_size / 1e6
    if mb > cap_mb:
        dst.unlink()
        raise SystemExit(f"{name}: {mb:.2f} MB exceeds the {cap_mb} MB cap -- trim it or cut it")
    return dst, mb


def probe(path):
    """(frames, seconds) of a baked clip, straight from the file we just wrote."""
    from PIL import Image
    im = Image.open(path)
    n = getattr(im, "n_frames", 1)
    return n, n / FPS


def check_sets():
    """Validate SETS before any bake. A typo'd board or clip name would otherwise surface as a
    KeyError after minutes of ffmpeg, or -- worse -- silently skip the budget assertion."""
    caps = dict(BOARDS)
    for name, s in SETS.items():
        assert s["board"] in caps, f"set '{name}': unknown board {s['board']!r} (have {sorted(caps)})"
        for c in s["clips"] or []:
            assert c in KEEP, f"set '{name}': {c!r} is not in KEEP"
    assert not (set(KEEP) & set(CUT)), f"in both KEEP and CUT: {sorted(set(KEEP) & set(CUT))}"
    print(f"sets OK: " + ", ".join(f"{n}({len(s['clips'] or KEEP)})" for n, s in sorted(SETS.items())))


def self_check(outdir):
    """Bake the smallest keep and assert the output is what the device expects."""
    from PIL import Image
    check_sets()
    name = "sweets"                      # 480x480, ~4 s, smallest source in the keep set
    dst, mb = bake(name, KEEP[name], outdir, CAP_MB)
    im = Image.open(dst)
    assert im.format == "GIF", f"not a GIF: {im.format}"
    assert im.size == (SIZE, SIZE), f"wrong size: {im.size}"
    assert getattr(im, "n_frames", 1) > 1, "single frame -- the fps filter ate the animation"
    assert len(im.getpalette()) // 3 <= 256, "palette overflow"
    assert mb <= CAP_MB, f"{mb} MB over cap"
    print(f"self-check OK: {name} -> {im.size[0]}x{im.size[1]}, "
          f"{im.n_frames} frames, {mb:.2f} MB, palette {len(im.getpalette())//3}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--set", default="full", choices=sorted(SETS),
                    help="which clip set to bake (default full); output goes to build/gifs/<set>/")
    ap.add_argument("--out", help="override the output dir (default build/gifs/<set>)")
    ap.add_argument("--only", nargs="*", help="bake just these names, ignoring the set")
    ap.add_argument("--cap-mb", type=float, default=CAP_MB)
    ap.add_argument("--self-check", action="store_true")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        raise SystemExit("ffmpeg not on PATH")
    if args.self_check:
        # Never into a set directory -- not even an empty one. A leftover there is a set that
        # flash.py would cheerfully upload as if it were real.
        self_check(Path(tempfile.mkdtemp()))
        return
    check_sets()

    outdir = Path(args.out) if args.out else OUT_DEFAULT / args.set
    outdir.mkdir(parents=True, exist_ok=True)

    # A set is the whole contents of a filesystem image, so a clip dropped from SETS has to
    # disappear from disk too -- otherwise it rides along in the next uploadfs forever.
    for stale in outdir.glob("*.gif"):
        stale.unlink()

    # Every source accounted for, or fail loud. A meme dropped in and forgotten is the bug this
    # assertion exists to catch.
    found = {p.stem for p in SRC.glob("*.gif")}
    known = {o.get("src", n) for n, o in KEEP.items()} | set(CUT)
    if found - known:
        raise SystemExit(f"unclassified source clips (add to KEEP or CUT): {sorted(found - known)}")
    if known - found:
        raise SystemExit(f"KEEP/CUT names with no source file: {sorted(known - found)}")

    sel = SETS[args.set]
    names = args.only or sel["clips"] or sorted(KEEP)
    print(f"set '{args.set}' -> {outdir}  ({len(names)} clips, target {sel['board']})")
    rows, total = [], 0.0
    for name in names:
        if name not in KEEP:
            raise SystemExit(f"{name} is not in KEEP ({CUT.get(name, 'unknown clip')})")
        dst, mb = bake(name, KEEP[name], outdir, args.cap_mb)
        frames, secs = probe(dst)
        rows.append((name, mb, frames, secs, mb * 1000 / secs if secs else 0))
        total += mb
        print(f"  {name:<16} {mb:6.2f} MB  {frames:3d}f  {secs:4.1f}s  {rows[-1][4]:5.0f} KB/s")

    print(f"\n{len(rows)} clips, {total:.2f} MB total")
    for label, cap in BOARDS:
        fit = "fits" if total <= cap else f"OVER by {total - cap:.2f} MB"
        mark = " <- target" if label == sel["board"] else ""
        print(f"  {label:<18} {cap:5.2f} MB  -> {fit}{mark}")
    if CUT:
        print(f"\ncut ({len(CUT)}): " + ", ".join(sorted(CUT)))

    (outdir / "manifest.json").write_text(json.dumps(
        {"set": args.set, "board": sel["board"], "fps": FPS, "size": SIZE, "colors": COLORS,
         "total_mb": round(total, 3),
         "clips": [{"name": n, "mb": round(m, 3), "frames": f, "secs": round(s, 2)}
                   for n, m, f, s, _ in rows]}, indent=2))

    # A set that does not fit its own target board is a packaging bug, not a warning. Fail after
    # writing the manifest so the numbers are on disk to look at.
    cap = dict(BOARDS)[sel["board"]]
    if total > cap and not args.only:
        raise SystemExit(f"\nset '{args.set}' is {total:.2f} MB but {sel['board']} holds {cap:.2f} MB "
                         f"-- drop a clip or trim one")
    print(f"\nflash it:  python3 tools/flash.py <target> --gifs {args.set}")


main()
