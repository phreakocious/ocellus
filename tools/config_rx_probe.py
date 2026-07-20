#!/usr/bin/env python3
"""Reproduce / characterize the config-save "bad json" RX-drop bug.

    python tools/config_rx_probe.py /dev/cu.usbmodemXXXX

Sends the device its own config back as a full `set` (what config.html sends) and a
clean size-threshold sweep. On the buggy firmware the big `set` returns `bad json`
while small lines pass. See docs/config-save-rx-drop-investigation.md.

Needs pyserial (use the PlatformIO venv python: ~/.platformio/penv/bin/python).
A connected config.html Web Serial tab locks the port -- close it first.
"""
import sys, time, json
import serial

def wait_typed(s, want, wait):
    end = time.time() + wait
    while time.time() < end:
        raw = s.readline().decode("utf-8", "replace").strip()
        if raw.startswith("{"):
            try:
                j = json.loads(raw)
                if j.get("type") in want:
                    return j
            except Exception:
                pass
    return None

def flush_dev(s):
    s.write(b"\n"); time.sleep(0.2); s.reset_input_buffer()

def clean_get(s):
    flush_dev(s); s.write(b'{"cmd":"get"}\n')
    return wait_typed(s, ("config", "err"), 1.5)

def sized_get(s, target):
    """A valid-JSON line of `target` bytes -> parses to `config` if intact, `err` if truncated."""
    base = len(json.dumps({"cmd": "get", "pad": ""}))
    line = json.dumps({"cmd": "get", "pad": "x" * max(0, target - base)})
    s.reset_input_buffer(); s.write((line + "\n").encode())
    j = wait_typed(s, ("config", "err"), 2.0)
    return (j or {}).get("type")

def main():
    port = sys.argv[1]
    s = serial.Serial(port, 115200, timeout=0.4)
    time.sleep(0.4); s.reset_input_buffer()

    g = clean_get(s)
    if not g or g.get("type") != "config":
        print("no config reply -- not an ocellus, port busy, or board halted "
              "(native-USB boards need a physical replug after flashing)")
        return
    print("device answers get: OK")

    # Reproduce: echo the device's own config back as a full set (== what config.html sends)
    cfg = {k: v for k, v in g.items() if k != "type"}
    payload = json.dumps({"cmd": "set", "config": cfg})
    s.reset_input_buffer(); s.write((payload + "\n").encode())
    r = wait_typed(s, ("config", "err"), 2.5)
    t = r.get("type") if r else None
    print(f"full set  payload={len(payload)}B -> {t}"
          f"{'  <-- BUG' if t == 'err' else ''}")

    # Clean threshold sweep
    print("threshold (clean single-shots):")
    for target in [40, 80, 120, 200, 300, 450, 650]:
        ok = tot = 0
        for _ in range(6):
            g = clean_get(s)
            if not g or g.get("type") != "config":
                continue   # device not in a known-clean state; don't count this trial
            time.sleep(0.1)
            r = sized_get(s, target); tot += 1
            if r == "config":
                ok += 1
            time.sleep(0.12)
        print(f"  {target:4d}B  {ok}/{tot}")
    s.close()

if __name__ == "__main__":
    main()
