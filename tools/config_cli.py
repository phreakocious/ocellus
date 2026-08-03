#!/usr/bin/env python3
"""Headless driver/test for the ocellus serial config protocol.

Usage:
  python3 tools/config_cli.py PORT get
  python3 tools/config_cli.py PORT catalog
  python3 tools/config_cli.py PORT set '{"brightness": 80}'
  python3 tools/config_cli.py PORT selftest
  python3 tools/config_cli.py PORT slide-upload cat.jpg [index] [--fit|--cover]
    (default: images with transparency are scaled/centred to fit inside the round mask;
     opaque ones are cover-cropped. --fit/--cover force either way.)
  python3 tools/config_cli.py PORT slide-set DIR_OR_FILE... [--fit|--cover]
    (clears, then uploads the whole set in ONE session -- one reset instead of one per file)
  python3 tools/config_cli.py PORT slide-list
  python3 tools/config_cli.py PORT slide-clear
  python3 tools/config_cli.py PORT raw '{"cmd":"bat"}' ['{"cmd":"batsim","mv":3400}' ...]
    (multiple JSON args are sent in one session, pausing for Enter between sends --
     the CH343 board resets on port open, so state-dependent sequences must be one session)
"""
import sys, json, time, base64, os, glob
# serial (pyserial) imported lazily in main() so the pure converters (pack_rgb565 / img_to_rgb565)
# are importable without pyserial -- e.g. tools/test_slide_convert.py under a bare python3.

def rpc(ser, obj, attempts=3):
    # The CH343 board reboots on port open only when the OS's prior DTR/RTS state makes the
    # open a line transition (edge-triggered reset circuit) -- so sometimes it reboots, sometimes
    # not, and a cold boot runs the multi-second name splash, outlasting main()'s settle. Resend on
    # timeout instead of stretching the sleep: the fast path stays fast, the boot race resolves
    # itself by the second attempt. Resends only happen when NO reply arrived, so double-apply is
    # a non-issue for the idempotent config/bat/batsim commands this tool speaks.
    for _ in range(attempts):
        ser.reset_input_buffer()
        ser.write((json.dumps(obj) + "\n").encode())
        ser.flush()
        deadline = time.time() + 3
        while time.time() < deadline:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                continue
            try:
                return json.loads(line)
            except ValueError:
                continue   # device log line ([prof]/[boot]/[espnow]), not a protocol reply -- keep reading
    raise TimeoutError("no response")

def wait_ready(ser, timeout=20.0):
    """Poll until the app actually answers, instead of sleeping a fixed guess.

    pollConfigSerial() only runs once loop() does, so a freshly-reset unit is deaf for its whole
    boot splash -- measured at ~9.3s, three times the 3.0s settle this replaced. rpc()'s retry
    (3 x 3s) papered over that for get/set, but slide_begin landing in the deaf window aborted
    uploads. Same lesson as flash.py's get_config(wait=...): probe, don't guess.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        ser.reset_input_buffer()
        ser.write(b'{"cmd":"get"}\n'); ser.flush()
        end = time.time() + 1.0
        while time.time() < end:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue                      # [prof]/[boot] log line, not a protocol reply
            if isinstance(d, dict) and "brightness" in d:
                # The polls we sent while it was deaf sat in its 2048B RX ring and all get answered
                # at once on wake, so N replies are queued behind this one. Leaving them there
                # desyncs every later command (a slide_clear reads a stale config reply). Drain
                # until 0.5s passes with no further JSON; [prof] lines arrive forever and must not
                # keep resetting the timer, or this never returns.
                quiet = time.time() + 0.5
                while time.time() < quiet:
                    if ser.readline().decode(errors="replace").strip().startswith("{"):
                        quiet = time.time() + 0.5
                return True
    return False

def pack_rgb565(rgba):
    """Pure: bytes-like RGBA (240*240*4) -> little-endian RGB565 bytes, round-cropped. Golden-tested."""
    out = bytearray(240 * 240 * 2); R = 120
    for y in range(240):
        for x in range(240):
            i = (y * 240 + x) * 4; o = (y * 240 + x) * 2
            r, g, b = rgba[i], rgba[i + 1], rgba[i + 2]
            dx = x - 120 + 0.5; dy = y - 120 + 0.5
            if dx * dx + dy * dy > R * R:
                r = g = b = 0
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF
    return bytes(out)

def content_disc(alpha):
    """Centre and radius of the opaque content: the smallest disc about the bbox centre that holds it.

    Only each row's leftmost/rightmost opaque pixel can be the farthest from a horizontally-centred
    point, so the scan breaks out of both ends instead of walking every pixel.
    """
    import math
    bb = alpha.getbbox()
    cx, cy = (bb[0] + bb[2]) / 2, (bb[1] + bb[3]) / 2
    px = alpha.load(); best = 0.0
    for y in range(bb[1], bb[3]):
        for xl in range(bb[0], bb[2]):
            if px[xl, y]: break
        else: continue                                             # fully transparent row
        for xr in range(bb[2] - 1, xl - 1, -1):
            if px[xr, y]: break
        for x in (xl, xr):
            best = max(best, (x - cx) ** 2 + (y - cy) ** 2)
    return cx, cy, math.sqrt(best) or 1.0

def img_to_rgb565(path, fit=None):
    """240x240 RGB565, round-cropped.

    fit=False  cover-fit: fill the frame, let the round mask clip the corners. Right for photos.
    fit=True   content-fit: centre on the opaque content and scale so its farthest pixel lands on
               the mask edge -- nothing is clipped, and art that sits off-centre gets recentred.
    fit=None   (default) content-fit when the image has any transparency, else cover-fit. A fully
               opaque image gives no signal about what is background, so cropping it is the safer
               guess; pass fit=True to shrink one inside the circle anyway.
    """
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    alpha = im.split()[3]
    if fit is None:
        fit = alpha.getextrema()[0] < 255
    if fit:
        cx, cy, rad = content_disc(alpha)
        s = 120 / rad
        r = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))))
        # Pad with the image's own corner colour, not black: shrinking a white-background graphic
        # inside the mask otherwise rings it in black. A transparent corner means the backdrop is
        # meant to show through, so fall back to black there (and the mask blacks the corners anyway).
        c = im.getpixel((0, 0))
        out = Image.new("RGBA", (240, 240), (c[0], c[1], c[2], 255) if c[3] == 255 else (0, 0, 0, 255))
        out.paste(r, (round(120 - cx * s), round(120 - cy * s)), r)
    else:
        s = max(240 / im.width, 240 / im.height)                   # cover-fit, centered
        r = im.resize((round(im.width * s), round(im.height * s)))
        left = (r.width - 240) // 2; top = (r.height - 240) // 2
        out = r.crop((left, top, left + 240, top + 240))
    return pack_rgb565(out.tobytes())

def slide_upload(ser, path, index, fit=None, tries=3):
    """Retry the WHOLE transfer, never a single chunk.

    Serial RX drops bytes during the framebuffer flush, so a chunk line can arrive mangled. But the
    device tears the upload down on any chunk-level error (slide_proto's fail() -> abortTmp +
    active=false), and re-sending a seq it already applied is itself a 'bad seq' abort -- so
    resuming mid-stream can turn one dropped byte into a corrupt slide. slide_begin resets
    nextSeq/got/active unconditionally, which makes restarting from the top always safe.
    """
    data = img_to_rgb565(path, fit)
    for attempt in range(tries):
        try:
            r = rpc(ser, {"cmd": "slide_begin", "index": index})
            if r.get("type") == "slide_ack":
                for seq, off in enumerate(range(0, len(data), 1024)):
                    r = rpc(ser, {"cmd": "slide_chunk", "seq": seq,
                                  "data": base64.b64encode(data[off:off + 1024]).decode()})
                    if r.get("type") != "slide_ack":
                        break
                else:
                    r = rpc(ser, {"cmd": "slide_end"})
                    if r.get("type") == "slide_ack":
                        return
        except TimeoutError as e:
            r = e            # rpc() gives up after its own retries; a lost reply restarts the
                             # transfer like any other failure rather than killing a whole batch
        print(f"  upload attempt {attempt + 1}/{tries} failed ({r}), restarting", file=sys.stderr)
        ser.reset_input_buffer()   # a late reply to the abandoned attempt would desync the next one
        time.sleep(0.5)
    sys.exit(f"slide upload failed after {tries} attempts: {r}")

def main():
    import serial
    fit = True if "--fit" in sys.argv else False if "--cover" in sys.argv else None
    sys.argv = [a for a in sys.argv if a not in ("--fit", "--cover")]
    port, cmd = sys.argv[1], sys.argv[2]
    # CH343 board line-state facts, all measured on the bench (2026-07-16):
    #  - host->device bytes only flow with DTR asserted; both-deasserted is receive-only.
    #  - the cross-coupled auto-reset circuit makes any open a reset ROULETTE: whether (and into
    #    which mode) the chip resets depends on the OS's prior line state and the order the driver
    #    asserts the lines -- DTR-before-RTS pulls IO0 low at the reset edge = DOWNLOAD mode, app dead.
    # So don't gamble: force the esptool-style sequence ourselves. EN low while IO0 is high, then
    # release into the both-asserted steady state (transistors cancel -> EN and IO0 both high).
    # Every open therefore reboots the device DETERMINISTICALLY into the app -- which is why
    # multi-step device-state work must ride ONE session (see `raw`'s Enter pacing). Native-USB
    # boards ignore the lines entirely and just pay the settle.
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 1
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(0.05)
    ser.rts = True                 # EN low (chip held in reset); DTR deasserted keeps IO0 high
    time.sleep(0.1)
    ser.dtr = True                 # both asserted -> EN releases with IO0 high: clean app boot, TX enabled
    if not wait_ready(ser):        # reboot-to-setup(); the splash keeps it deaf for ~9.3s
        sys.exit(f"{port}: no reply after reset -- port busy (config.html tab?) or not an ocellus")
    ser.reset_input_buffer()
    if cmd == "get":
        print(json.dumps(rpc(ser, {"cmd": "get"}), indent=2))
    elif cmd == "catalog":
        print(json.dumps(rpc(ser, {"cmd": "catalog"}), indent=2))
    elif cmd == "set":
        print(json.dumps(rpc(ser, {"cmd": "set", "config": json.loads(sys.argv[3])}), indent=2))
    elif cmd == "selftest":
        cat = rpc(ser, {"cmd": "catalog"})
        ids = [a["id"] for a in cat["animations"]]
        assert cat["type"] == "catalog" and ids == list(range(len(ids))), cat  # contiguous ids 0..N-1
        rpc(ser, {"cmd": "set", "config": {"name": "SelfTest", "brightness": 42}})
        got = rpc(ser, {"cmd": "get"})
        assert got["name"] == "SelfTest" and got["brightness"] == 42, got
        print(f"OK: catalog={len(ids)} anims, set/get round-trip verified")
    elif cmd == "slide-upload":
        idx = int(sys.argv[4]) if len(sys.argv) > 4 else len(rpc(ser, {"cmd": "slide_list"})["slides"])
        slide_upload(ser, sys.argv[3], idx, fit)
        print(json.dumps(rpc(ser, {"cmd": "slide_list"}), indent=2))
    elif cmd == "slide-set":
        # A whole set in ONE session. main() resets the board on every open and the splash costs
        # ~9.3s before it answers, so uploading N files as N invocations pays N resets -- and the
        # panel freezes for each (loop() skips render while gSlideUploading). Clears first, so
        # device index order == the order given here.
        paths = []
        for a in sys.argv[3:]:
            paths += sorted(glob.glob(os.path.join(a, "*"))) if os.path.isdir(a) else [a]
        paths = [p for p in paths if os.path.splitext(p)[1].lower()
                 in (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp")]
        if not paths:
            sys.exit("slide-set: no images found")
        if rpc(ser, {"cmd": "slide_clear"}).get("type") != "slide_ack":
            sys.exit("slide-set: clear failed")
        for i, p in enumerate(paths):
            print(f"  [{i}] {os.path.basename(p)}", flush=True)
            slide_upload(ser, p, i, fit)
        print(json.dumps(rpc(ser, {"cmd": "slide_list"}), indent=2))
    elif cmd == "slide-list":
        print(json.dumps(rpc(ser, {"cmd": "slide_list"}), indent=2))
    elif cmd == "slide-clear":
        print(json.dumps(rpc(ser, {"cmd": "slide_clear"}), indent=2))
    elif cmd == "raw":
        for i, line in enumerate(sys.argv[3:]):
            if i: input("-- Enter to send next --")
            print(json.dumps(rpc(ser, json.loads(line)), indent=2))
    else:
        sys.exit("unknown cmd")
    ser.close()

if __name__ == "__main__":
    main()
