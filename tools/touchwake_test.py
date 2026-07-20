#!/usr/bin/env python3
"""Bench test: confirm a screen TAP wakes the ocellus from deep sleep.

    ~/.platformio/penv/bin/python tools/touchwake_test.py            # auto-find port
    ~/.platformio/penv/bin/python tools/touchwake_test.py --port /dev/cu.usbmodemXXXX

Deep-sleep wake is hardware behaviour -- it can't be host-unit-tested, so this is an
operator-in-the-loop bench check. It opens the CH343 serial port and watches the firmware's
markers (see main.cpp): `[sleep] deep-sleep entered` on sleep, then `[boot] ... src=touch|button`
on the next wake. The Waveshare CH343 UART is a separate USB chip, so the port stays open across
the ESP32's sleep/wake reboot and catches the whole cycle.

PASS = we saw the device sleep and then report `src=touch`. Any `src=button` wake means the button
(not touch) woke it -- release the button and TAP the glass instead.

Needs pyserial + the flash.py probe (sibling file). Run under the PlatformIO venv python.
Gotcha: close any config.html (Web Serial) tab first -- it locks the port.
"""
import argparse, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import serial
    from flash import find_port, ports   # reuse the ocellus port probe
except ImportError as e:
    sys.exit(f"needs pyserial + tools/flash.py -- run with ~/.platformio/penv/bin/python ({e})")


def main():
    ap = argparse.ArgumentParser(description="Bench test: tap-to-wake from deep sleep.")
    ap.add_argument("--port", help="serial port (default: probe for the ocellus)")
    ap.add_argument("--timeout", type=int, default=90, help="seconds to wait for the tap-wake (default 90)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit(f"No ocellus found among {ports() or '(no usbmodem ports)'}.\n"
                 "  - close any config.html (Web Serial) tab holding the port\n"
                 "  - or pass --port /dev/cu.usbmodemXXXX")

    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(1.2)  # let the CDC/CH343 settle (opening may reset the board once)
    s.reset_input_buffer()

    print(f"port: {port}")
    print("\nBench steps:")
    print("  1) Long-press the button to sleep it (or wait for the idle sleep timeout).")
    print("  2) RELEASE the button.")
    print("  3) TAP the screen.\n")
    print(f"Watching for tap-wake (timeout {args.timeout}s)...  Ctrl-C to abort.\n")

    slept = False
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            continue
        print(f"  | {line}")
        if "[sleep]" in line:
            slept = True
        elif line.startswith("[boot]") and "src=" in line:
            src = line.split("src=", 1)[1].split()[0]
            if src == "touch":
                print("\nPASS: a tap woke it from deep sleep." if slept
                      else "\nPASS: woke on touch (didn't observe the sleep marker -- already asleep?).")
                s.close(); sys.exit(0)
            if src == "button":
                print("  -> that was a BUTTON wake, not touch. Sleep it again, release the button, and TAP the glass.")
            # src=cold -> a plain reboot; ignore and keep waiting

    print(f"\nFAIL/inconclusive: no touch-wake seen in {args.timeout}s.")
    if not slept:
        print("  (never saw '[sleep]' -- did it actually go to sleep? long-press the button)")
    s.close(); sys.exit(1)


if __name__ == "__main__":
    main()
