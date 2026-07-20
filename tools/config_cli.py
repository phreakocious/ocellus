#!/usr/bin/env python3
"""Headless driver/test for the ocellus serial config protocol.

Usage:
  python3 tools/config_cli.py PORT get
  python3 tools/config_cli.py PORT catalog
  python3 tools/config_cli.py PORT set '{"brightness": 80}'
  python3 tools/config_cli.py PORT selftest
  python3 tools/config_cli.py PORT slide-upload cat.jpg [index]
  python3 tools/config_cli.py PORT slide-list
  python3 tools/config_cli.py PORT slide-clear
  python3 tools/config_cli.py PORT raw '{"cmd":"bat"}' ['{"cmd":"batsim","mv":3400}' ...]
    (multiple JSON args are sent in one session, pausing for Enter between sends --
     the CH343 board resets on port open, so state-dependent sequences must be one session)
"""
import sys, json, time, base64
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

def img_to_rgb565(path):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    s = max(240 / im.width, 240 / im.height)                       # cover-fit, centered
    im = im.resize((round(im.width * s), round(im.height * s)))
    left = (im.width - 240) // 2; top = (im.height - 240) // 2
    im = im.crop((left, top, left + 240, top + 240))
    return pack_rgb565(im.tobytes())

def slide_upload(ser, path, index):
    data = img_to_rgb565(path)
    assert rpc(ser, {"cmd": "slide_begin", "index": index}).get("type") == "slide_ack"
    for seq, off in enumerate(range(0, len(data), 1024)):
        r = rpc(ser, {"cmd": "slide_chunk", "seq": seq, "data": base64.b64encode(data[off:off + 1024]).decode()})
        assert r.get("type") == "slide_ack", r
    assert rpc(ser, {"cmd": "slide_end"}).get("type") == "slide_ack"

def main():
    import serial
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
    time.sleep(3.0)                # reboot-to-setup(); cold-boot splash overrun is covered by rpc()'s retry
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
        slide_upload(ser, sys.argv[3], idx)
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
