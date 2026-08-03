#!/usr/bin/env python3
"""Build + flash ocellus firmware, then re-select the mode after reboot.

    tools/flash.py [s3|s3-touch|s3-zero|c3] [--no-build] [--anim ID] [--port /dev/cu.usbmodemXXX]

Encodes the port dance CLAUDE.md documents by hand:
  - several 303A:* USB-CDC boards may be plugged in and the /dev number changes,
    and the label ("USB JTAG/serial debug unit") can't be trusted. So we PROBE each
    /dev/cu.usbmodem* with the config protocol ({"cmd":"get"}) and flash the one that
    answers like an ocellus -- never a mislabeled port or a stray SensoryBridge stick.
    The probe can't tell one ocellus from another, though, so the TARGET scopes it:
    s3-touch goes strictly by the CH343 USB descriptor, everything else strictly by
    the probe (see find_port).
  - build + upload via PlatformIO, which handles the S3/C3 bootloader offsets (unlike
    SB, no manual esptool dance needed). esptool still aborts if the chip doesn't match
    the -e env, a handy "did I grab the right port?" check.
  - a reflash reboots to the startup mode; --anim re-selects a mode afterward (the thing
    you end up doing by hand every flash while iterating on an effect).

Run under a python that has pyserial -- the PlatformIO venv already does:
    ~/.platformio/penv/bin/python tools/flash.py s3 --anim 24
Needs: pyserial, PlatformIO (~/.platformio/penv/bin/pio).

Gotcha: a connected config.html (Web Serial) tab locks the port -- close it first or
the probe and the upload both fail "Resource busy".
"""
import argparse, glob, json, os, shutil, subprocess, sys, time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("needs pyserial -- run with ~/.platformio/penv/bin/python, or: pip install pyserial")

CH343_ID = (0x1A86, 0x55D3)   # Waveshare board's CH343 UART -- opening it auto-resets, so the config probe can't see it
ESPRESSIF_VID = 0x303A        # native USB-Serial/JTAG (bench DevKitC, S3-Zero, C3) -- all 303A:1001, indistinguishable by id
BUSY = object()               # sentinel: port held open, which is NOT the same as "answered nothing"

PIO = os.path.expanduser("~/.platformio/penv/bin/pio")
if not os.path.exists(PIO):
    PIO = shutil.which("pio")
    if not PIO:
        sys.exit("PlatformIO 'pio' not found (expected ~/.platformio/penv/bin/pio)")

TARGETS = {   # alias -> pio env; chip is only for the message / sanity
    "s3":       dict(env="esp32-s3",           chip="ESP32-S3"),
    "s3-touch": dict(env="esp32-s3-touch-128", chip="ESP32-S3"),   # Waveshare board (CH343 UART)
    "s3-zero":  dict(env="esp32-s3-zero",      chip="ESP32-S3"),   # console unit (native USB-Serial/JTAG, same probe path as s3)
    "c3":       dict(env="esp32-c3-devkitm-1", chip="ESP32-C3"),
}


def ports():
    return sorted(glob.glob("/dev/cu.usbmodem*"))


def _open(port):
    # Native USB-Serial/JTAG has no auto-reset line, so opening the port does NOT reboot
    # the board (unlike a DTR/RTS auto-reset UART bridge). The settle lets the CDC come up.
    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(3.0)   # CH343 resets the Waveshare on open; leave setup() time before the first RPC
                      # (1.6 raced the slower first boot right after a flash -- anim cmd hit a dead UART)
    s.write(b"\n"); s.flush()   # reset leaves boot garbage in the device's RX line buffer; terminate
    time.sleep(0.3)             # it so the first real command isn't glued to it ("bad json" / no reply)
    s.reset_input_buffer()
    return s


def _reply(s, obj):
    """First JSON line the device sends back. The firmware also emits plain log lines
    ([prof] once a second, [boot]/[espnow] at startup); those are not replies -- skip them."""
    s.write((json.dumps(obj) + "\n").encode()); s.flush()
    deadline = time.time() + 3
    while time.time() < deadline:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            continue
        try:
            v = json.loads(line)
        except ValueError:
            continue
        if isinstance(v, dict):   # every device reply is an object; a bare number/string is a
            return v              # buffer fragment (e.g. "812" from a torn line), not the reply
    return None


def get_config(port):
    """The unit's config dict, or None if this port isn't an ocellus (BUSY if it's held open --
    almost always a config.html Web Serial tab, which is a very different thing from silence)."""
    try:
        s = _open(port)
    except Exception:
        return BUSY    # held by a Web Serial tab / another probe, or vanished
    try:
        d = _reply(s, {"cmd": "get"})
        return d if isinstance(d, dict) and "brightness" in d and "maxFps" in d else None
    finally:
        s.close()


def probe(port):
    """True if the port answers the ocellus config protocol with a config object."""
    return isinstance(get_config(port), dict)


def list_boards(do_probe):
    """Every attached board of ours, by USB descriptor -- no port is opened, so nothing reboots.
    This is the answer to "which S3 is which": the bench DevKitC, the S3-Zero and the C3 all
    enumerate as 303A:1001 and chip-detect cannot separate them, but their USB serial number IS
    the MAC -- a stable per-board id, free from the descriptor, and unlike an `esptool flash_id`
    probe it doesn't halt the board. --probe additionally asks each unit its configured name,
    which costs ~3s per port and reboots any ship board (opening a CH343 asserts reset)."""
    rows = [("PORT", "VID:PID", "KIND", "USB SERIAL / MAC", "LOCATION", "NAME")]
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        if (p.vid, p.pid) == CH343_ID:
            kind = "ship (CH343)"
        elif p.vid == ESPRESSIF_VID:
            kind = "native USB"
        else:
            continue          # not one of ours
        name = "-"
        if do_probe:
            cfg = get_config(p.device)
            name = ("(busy -- close the config.html tab)" if cfg is BUSY else
                    "(not an ocellus)" if cfg is None else cfg.get("name") or "(unnamed)")
        rows.append((p.device, "%04X:%04X" % (p.vid, p.pid), kind,
                     p.serial_number or "-", p.location or "-", name))
    if len(rows) == 1:
        print("No ocellus boards attached.")
        return
    w = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    for r in rows:
        print("  ".join(c.ljust(w[i]) for i, c in enumerate(r)).rstrip())


def ch343_ports():
    """Waveshare board ports, matched by USB descriptor (no open -> no reset)."""
    return sorted(p.device for p in list_ports.comports() if (p.vid, p.pid) == CH343_ID)


def find_port(want_ch343):
    # The probe can't tell WHICH ocellus answered (console and bench S3 both answer), so the
    # target decides the search: s3-touch is the only CH343 board -> descriptor match ONLY,
    # never a probe-answering native-USB ocellus (that exact fallthrough once put s3-touch
    # firmware on the console and crash-looped it). Conversely a non-touch target must never
    # fall back to the CH343 -- same bug mirrored.
    ch343 = set(ch343_ports())
    if want_ch343:
        if ch343:
            p = sorted(ch343)[0]
            print(f"(CH343 board by VID:PID 1A86:55D3: {p})")
            return p
        return None
    for p in ports():
        if p in ch343:
            continue          # opening a CH343 auto-resets it mid-handshake -> can't probe anyway
        if probe(p):
            return p
    return None


def run(cmd, tries=1, env=None):
    for i in range(tries):
        if i:
            print(f"  retry {i+1}/{tries} (CH343 upload drops with 'Device not configured'/errno 6 -- transient)")
        print("+", " ".join(cmd))
        if subprocess.run(cmd, env=env).returncode == 0:
            return 0
    return 1


def main():
    ap = argparse.ArgumentParser(description="Build + flash ocellus firmware.")
    ap.add_argument("target", nargs="?", default="s3", choices=TARGETS, help="board env (default s3)")
    ap.add_argument("--no-build", action="store_true", help="skip compiling; upload the last build")
    ap.add_argument("--anim", type=int, metavar="ID", help="select this animation id after flashing")
    ap.add_argument("--only-anim", action="store_true",
                    help="don't build or flash at all -- just switch the running board to --anim")
    ap.add_argument("--port", help="skip the probe and use this port (e.g. a blank board)")
    ap.add_argument("--gifs", metavar="SET",
                    help="upload a baked GIF set to the filesystem and nothing else "
                         "(no firmware build or flash) -- bake it first with tools/bake_gif.py --set SET")
    ap.add_argument("--list", action="store_true",
                    help="list attached boards (USB descriptor only, nothing is opened or reset) and exit")
    ap.add_argument("--probe", action="store_true",
                    help="with --list, also ask each unit its configured name (~3s/port; reboots ship boards)")
    args = ap.parse_args()

    if args.list:
        list_boards(args.probe)
        return

    tgt = TARGETS[args.target]

    port = args.port or find_port(args.target == "s3-touch")
    if not port:
        sys.exit(f"No ocellus found among {ports() or '(no usbmodem ports)'}.\n"
                 "  - a config.html (Web Serial) tab may be holding the port -- close it\n"
                 "  - or pass --port /dev/cu.usbmodemXXXX to force one (e.g. a blank board)")
    print(f"{args.target} ({tgt['chip']}): {port}")

    # Mode-switch only: the board already runs the firmware you want, you just want a different
    # animation on screen. Skips the ~20s build+upload. Note this is NOT reboot-free on the Waveshare
    # board -- opening its CH343 port asserts the auto-reset line (see CH343_ID above), so it restarts
    # and the anim command lands after; that's still ~2s vs ~20s.
    if args.only_anim:
        if args.anim is None:
            sys.exit("--only-anim needs --anim ID")
        print(f"anim {args.anim}: {send_anim(port, args.anim)}")
        return

    # GIF sets live on LittleFS, which is a different partition from the firmware -- so loading a
    # a unit's memes never needs a rebuild or a reflash. This is also the only sane bulk path:
    # uploadfs writes the whole set in ~150 s, where the same bytes through the Web Serial gif_chunk
    # protocol (115200, base64, per-chunk acks) would take ~22 minutes. Serial is for swapping ONE
    # clip on a unit already in someone's hands; this is for loading a set.
    # PLATFORMIO_DATA_DIR is PlatformIO's own override for data_dir, so no ini edit is involved.
    if args.gifs:
        d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "build", "gifs", args.gifs)
        if not os.path.isdir(d):
            sys.exit(f"no baked set at {d}\n  bake it first: python3 tools/bake_gif.py --set {args.gifs}")
        n = len(glob.glob(os.path.join(d, "*.gif")))
        if not n:
            sys.exit(f"{d} has no .gif in it -- uploading it would just erase the board's "
                     f"filesystem.\n  bake it first: python3 tools/bake_gif.py --set {args.gifs}")
        print(f"uploading {n} clips from {d} (filesystem only -- firmware untouched)")
        rc = run([PIO, "run", "-e", tgt["env"], "-t", "uploadfs", "--upload-port", port],
                 tries=3, env={**os.environ, "PLATFORMIO_DATA_DIR": d})
        sys.exit(rc and "filesystem upload failed (Web Serial tab holding the port?)")

    if not args.no_build and run([PIO, "run", "-e", tgt["env"]]) != 0:
        sys.exit("build failed")

    if run([PIO, "run", "-e", tgt["env"], "-t", "upload", "--upload-port", port], tries=3) != 0:
        sys.exit("upload failed (Web Serial tab holding the port? wrong chip for this env?)")

    time.sleep(3)   # let it reboot + re-enumerate; the /dev number can change
    back = args.port if (args.port and args.port in ports()) else find_port(args.target == "s3-touch")
    if not back:
        print("Flashed OK, but the board hasn't answered yet -- give it a moment / check it.")
        return
    print(f"OK -- back up at {back}")
    if args.anim is not None:
        print(f"anim {args.anim}: {send_anim(back, args.anim)}")


def send_anim(port, anim_id, tries=6):
    """Select an animation, retrying until the device actually echoes {"type":"anim"}.

    The first attempt after a flash usually fails: the board is still in setup() (display init,
    LittleFS mount, NVS read) and the command lands on a UART that isn't being drained yet. The
    failure is NOT an error reply -- it is None, or a {"err": ...}, or a stale echo of some other
    command. All three mean "too early, ask again", which is why a bare send looks like it worked
    and silently leaves the board on its startup mode.

    Hand-rolled as a shell loop three sessions running (2026-07-28 twice, 2026-07-30) before
    landing here. If it still reports None after this many tries, the board is genuinely not
    answering and retrying harder will not help.
    """
    for attempt in range(tries):
        try:
            s = _open(port)
        except Exception:
            time.sleep(1.0)
            continue
        try:
            v = _reply(s, {"cmd": "anim", "id": anim_id})
        finally:
            s.close()
        if isinstance(v, dict) and v.get("type") == "anim":
            return v
        time.sleep(1.0)
    return None


if __name__ == "__main__":
    main()
