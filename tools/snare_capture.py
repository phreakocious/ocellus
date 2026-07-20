#!/usr/bin/env python
"""Capture the eye's ESP-NOW spectrum tap into a .tap fixture, driving Strudel for ground truth.

Usage (run with the PlatformIO venv python -- it has pyserial):
  ~/.platformio/penv/bin/python tools/snare_capture.py <pattern> [--secs 10] [--port PORT]
                                                       [--out FILE] [--manual]

Patterns (spec 2026-07-14-snare-tuning-rig-design.md section 3):
  kick snare backbeat coincident hats rumble
The device must run the tap firmware; the driver jumps it to Audio Debug (id 37, radio ON).
--manual: skip the Strudel HTTP calls -- paste the printed code into strudel.cc yourself.
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from flash import find_port, _open, _reply   # same probe/port discipline as flash.py

STRUDEL = "http://localhost:3000"
PKT_RATE_MIN = 120    # console streams ~175/s; far below = console off / wrong device
PEAK_MIN = 2000       # max bin over the whole capture; below = muted console (packets still flow)

# Ground truth is ARITHMETIC from the pattern at 120 BPM (setcpm(30): one cycle = 4 beats = 2s).
# role drives the sweep's objective: constraint = any snare fired kills the config;
# recall = maximize fired/expected; report = printed, never scored (fails at 1:1 by design).
#
# `sd`, NOT `cp`. The spec's first pattern table said clap; measured against the real console it is
# the wrong instrument, and the corpus it produced could not test this detector at all. A 909 clap
# has NO drum body -- captured means: bass(0-7) 390, body(8-23) 4273, bright(40-55) 22491, peaking
# at bins 40-47. It is spectrally a hi-hat. `sd` is what the mid band was built for: body(8-23)
# 22797, peaking 65535 across bins 12-20 (`bd` for scale: bass 43218, body 28755). Tuning against
# cp would have tuned the snare detector to fire on hats -- and since cp and hh occupy the same
# bins, the "zero snares on hats" constraint would have killed every config with any recall.
PATTERNS = {
    "kick":       dict(code='setcpm(30)\n$: s("bd*4").bank("RolandTR909")',
                       kicks=2.0, snares=0.0, ioi=None, role="constraint"),
    "snare":      dict(code='setcpm(30)\n$: s("~ sd ~ sd").bank("RolandTR909")',
                       kicks=0.0, snares=1.0, ioi=1000, role="recall"),
    "backbeat":   dict(code='setcpm(30)\n$: s("bd*4, [~ sd]*2").bank("RolandTR909")',
                       kicks=2.0, snares=1.0, ioi=1000, role="recall"),
    "coincident": dict(code='setcpm(30)\n$: s("[bd,sd]*4").bank("RolandTR909")',
                       kicks=2.0, snares=2.0, ioi=500, role="report"),
    "hats":       dict(code='setcpm(30)\n$: s("hh*16").bank("RolandTR909")',
                       kicks=0.0, snares=0.0, ioi=None, role="constraint"),
    "rumble":     dict(code='setcpm(30)\n$: s("bd*4").bank("RolandTR909")\n'
                            '$: note("c1").s("sawtooth").lpf(120)',
                       kicks=2.0, snares=0.0, ioi=None, role="constraint"),
}


def strudel(path, payload=None):
    # /api/code takes {"code": "..."}; /api/play and /api/stop take no body (strudel-claude,
    # src/app/api/*/route.ts). The server only holds state + broadcasts over SSE -- the BROWSER
    # tab on localhost:3000 is what actually makes sound, so one must be open (checked below).
    data = json.dumps(payload).encode() if payload is not None else b""
    req = urllib.request.Request(STRUDEL + path, data=data, method="POST",
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=5).read() or "{}")


def repo_sha():
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.run(["git", "diff", "--quiet", "HEAD"]).returncode != 0
    return sha + ("-dirty" if dirty else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pattern", choices=sorted(PATTERNS))
    ap.add_argument("--secs", type=float, default=10.0)
    ap.add_argument("--port")
    ap.add_argument("--out")
    ap.add_argument("--manual", action="store_true", help="you start/stop Strudel by hand")
    a = ap.parse_args()
    pat = PATTERNS[a.pattern]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "test", "fixtures", a.pattern + ".tap")

    port = a.port or find_port(want_ch343=False)
    if not port:
        sys.exit("no ocellus answered the config probe (board attached? config.html tab holding the port?)")

    s = _open(port)
    try:
        r = _reply(s, {"cmd": "anim", "id": 37})   # Audio Debug: radio ON (36 is the radio-OFF sensor screen)
        if not isinstance(r, dict) or r.get("type") != "anim":   # a stale tap/prof line can parse as a bare int
            sys.exit(f"anim cmd failed: {r!r} -- is the tap firmware flashed?")
        if a.manual:
            print("--- paste into Strudel and press play: ---")
            print(pat["code"])
            input("--- Enter when it is playing --- ")
        else:
            strudel("/api/code", {"code": pat["code"]})
            if not strudel("/api/play").get("isPlaying"):
                sys.exit("ABORT: /api/play did not latch isPlaying")
        time.sleep(1.0)                            # let the pattern and the console settle
        s.reset_input_buffer()
        s.write(b'{"cmd":"tap","on":true}\n')      # compact JSON: the firmware matches these exact bytes
        lines, deadline = [], time.time() + a.secs
        while time.time() < deadline:
            ln = s.readline().decode(errors="replace").strip()
            if ln.startswith("tap "):
                lines.append(ln)                   # [prof] noise and JSON acks are filtered right here
        s.write(b'{"cmd":"tap","on":false}\n')
        summary, stop_deadline = None, time.time() + 3
        while time.time() < stop_deadline:
            ln = s.readline().decode(errors="replace").strip()
            if ln.startswith("tap "):
                lines.append(ln)
                continue
            try:
                d = json.loads(ln)
            except ValueError:
                continue
            if d.get("type") == "tap" and d.get("on") is False:
                summary = d
                break
    finally:
        s.close()
        if not a.manual:
            try:
                strudel("/api/stop")
            except Exception:
                pass

    # Three aborts (spec section 2) plus a device-vs-host line-count integrity check.
    if summary is None:
        sys.exit("ABORT: no tap summary from the device")
    if summary["drops"]:
        sys.exit(f"ABORT: {summary['drops']} ring drops -- the capture has holes, unusable")
    if summary["sent"] != len(lines):
        sys.exit(f"ABORT: device sent {summary['sent']} lines, host saw {len(lines)} -- host-side loss")
    rate = len(lines) / a.secs
    if rate < PKT_RATE_MIN:
        sys.exit(f"ABORT: {rate:.0f} pkt/s (expect ~175) -- console off? radio not up?")
    peak = max(int(ln.split()[2][i:i + 4], 16) for ln in lines for i in range(0, 256, 4))
    if peak < PEAK_MIN:
        sys.exit(f"ABORT: peak bin {peak} -- console muted? (packets flow, spectrum is silence)")

    hdr = {"pattern": a.pattern, "code": pat["code"], "bpm": 120, "secs": a.secs,
           "expect": {"kicks_per_s": pat["kicks"], "snares_per_s": pat["snares"],
                      "snare_ioi_ms": pat["ioi"]},
           "role": pat["role"], "repo_sha": repo_sha()}
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("# " + json.dumps(hdr) + "\n")
        f.write("\n".join(lines) + "\n")
    print(f"{out}: {len(lines)} packets, {rate:.0f}/s, peak {peak}")


if __name__ == "__main__":
    main()
