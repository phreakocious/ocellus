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


def probe(port):
    """True if the port answers the ocellus config protocol with a config object."""
    try:
        s = _open(port)
    except Exception:
        return False   # busy (Web Serial tab?) or vanished
    try:
        d = _reply(s, {"cmd": "get"})
        return isinstance(d, dict) and "brightness" in d and "maxFps" in d
    finally:
        s.close()


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


def run(cmd, tries=1):
    for i in range(tries):
        if i:
            print(f"  retry {i+1}/{tries} (CH343 upload drops with 'Device not configured'/errno 6 -- transient)")
        print("+", " ".join(cmd))
        if subprocess.run(cmd).returncode == 0:
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
    args = ap.parse_args()
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


def send_anim(port, anim_id):
    s = _open(port)
    try:
        return _reply(s, {"cmd": "anim", "id": anim_id})
    finally:
        s.close()


if __name__ == "__main__":
    main()
