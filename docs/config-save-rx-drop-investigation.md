# Config save → "Device error: bad json" — investigation

**Status:** FIXED, two layers, both shipped & hardware-verified.
- **Host** (0d28bad, 2026-07-16): per-key `set`s + echo-verify in `config.html saveConfig`.
- **Device** (2026-07-16): **config-save render hold** — after any `set` line, `loop()` skips
  render/flush/naps for 250 ms (re-armed per line), mirroring the slide-upload drain. The first
  line of a save may still drop (echo-verify retries it); every later part arrives into a quiet
  RX path. Trigger was QR mode's `qrBits` (~190 B), the first per-key line long enough to fail
  its whole 4-retry budget. Verified on the board: 22-part save clean; before the hold,
  cold `qrBits` lines dropped ~75% of attempts.

**Date:** 2026-07-15. Firmware: `main` @ 8211449.

## Symptom
Saving from `config.html` (Web Serial) shows **"Device error: bad json."** Reported
on *both* a bench native-USB S3 and a Waveshare board (CH343 UART).
Loading (`get`) and mode-jumps (`anim`) work fine; only **Save** fails.

## Error path (confirmed)
- Device `protocol.cpp:35`: `if (deserializeJson(d, line)) return {"type":"err","msg":"bad json"}`
  — the device could not parse the line it received.
- Page `config.html:129`: renders `"Device error: " + msg.msg`.

So the `set` line **arrives at the device corrupted/truncated.** It is a receive
(host→device) problem, not a reply problem — device→host replies (full ~600 B config
echoes) come back intact every time.

## Reproduced
`{"cmd":"get"}` (14 B) → reliable `config`. The real config-shaped `set` (~649 B) →
reliable `bad json`. Failure probability **rises with line length**: ~30 B mostly OK,
~300 B essentially always fails. (Harness: `tools/config_rx_probe.py`.)

## Ruled out (with evidence)
| Hypothesis | Verdict | Evidence |
|---|---|---|
| Out-of-memory parse | **No** | `[prof]` shows **126 KB** free heap; ArduinoJson v7 elastic doc, 650 B can't fail on memory. |
| `g_rxbuf` 2048 cap (`main.cpp:99`) | **No** | Max possible `set` is ~900 B (10 palette presets, favorites ≤35 ids, 4×5 custom). 649 ≪ 2048. |
| `setRxBufferSize(2048)` is a no-op | **No** | `HWCDC::setRxBufferSize` (framework) deletes+recreates the queue; the 2048 ring is real. |
| Second Serial reader / race | **No** | `pollConfigSerial` is the only `Serial.read()`, single caller in `loop()`; no second task reads Serial. |
| CH343/UART transport-specific | **No** | Fails identically on native-USB HWCDC. |
| Naive send pacing (chunk+delay) fixes it | **No** | Chunk48/delay35ms ≈ 3/10. Spreading the line over ~15 frames gives *more* flush windows to drop a byte — often **worse** than one blast. |

## Root cause (inferred; not device-confirmed)
Incoming bytes are dropped **during the multi-ms full-framebuffer flush** that
dominates every frame. Measured at id 30: render ~11.5 ms + **flush ~14 ms**, ~31 fps
→ the loop is busy ~25 of every ~32 ms. Small lines (get/anim, <~40 B) survive; a
~650 B line reliably loses bytes → invalid JSON → "bad json".

This is the **same failure class** the button already works around: per the project
notes, "the multi-ms full-framebuffer flush starves inline polling," which is why the
button runs in its own high-priority FreeRTOS task. Serial RX has no such protection.

**Not confirmed:** the exact drop point (hardware RX FIFO overflow vs. RX interrupt
masked during the SPI-DMA flush). Confirming it needs device-side instrumentation
(log `g_rxbuf.size()` per newline), which was blocked tonight — see below.

## Key enabler for a host-side fix (confirmed)
`configFromJson` (`config.cpp:60`) has **merge semantics**: every scalar field is
applied only `if (d[field].is<...>())`. A partial `{"cmd":"set","config":{"brightness":80}}`
updates *only* brightness. So the save can be split into several **small** `set`
commands (which provably traverse the RX) instead of one big line — no firmware
reflash required.

**Caveat:** the array fields clear-then-refill and can't be split incrementally:
- `favorites` (~110 B) — clears `favoritesMask`, refills from the array.
- `palettes.enabled` (~60 B).
- `palettes.custom` (~360 B for 4 full slots) — `config.cpp:100` clears `customPalettes`
  then refills; must arrive as one object. **This one is big enough to still drop.**

## Fix options (ranked)
1. **Host-side, recommended first.** `config.html` Save sends **only changed** top-level
   keys, each as its own small `set`, awaiting each echo; log "Saved" after the last.
   Most saves change 1–2 scalars → 1–2 tiny reliable commands. Deployable by editing
   `config.html` alone — fixes every unit with a page reload, no reflash.
   - Covers scalars fully. `favorites`/`enabled` usually OK at ~60–110 B.
   - **Does not guarantee** a full 4-slot custom-palette save (~360 B). Custom palettes
     are Phase-3-pending anyway; acceptable as a first fix, document the limit.
2. **Device-side, complete but needs reflash + verification.** Make RX survive the flush
   so any line size works: drain/service RX from a high-priority task or ensure the RX
   interrupt isn't starved during the SPI-DMA flush (mirror the button-task fix). Then
   the existing single-line protocol just works. Requires reflashing all units (now easy
   via the web flasher) and cannot be verified without hardware.
3. **Device-side protocol.** Chunk-and-ACK config into a buffer immune to the frame loop.
   Biggest change; only if 1 and 2 prove insufficient.

## Verification plan (do this first next session, with the board replugged)
1. Physically **replug** the bench board (native-USB reset doesn't restart the app — see below), confirm it boots and answers `get`.
2. Baseline: `python tools/config_rx_probe.py <port>` → confirm the ~649 B `set` still says `bad json` and small lines pass.
3. Apply fix #1 in `config.html`, save from the page, watch for "Saved" and a `get` that reflects the change.
4. To nail the exact drop point (optional but definitive): add `Serial.printf("[rxdbg] n=%u\n", (unsigned)g_rxbuf.size());` right after `if (ch=='\n') {` in `pollConfigSerial`, flash, and read what length actually arrives for a 650 B line.

## Hardware verification blocked (why no fix shipped tonight)
The bench board is a 16 MB / 2 MB-PSRAM S3 on native USB-Serial/JTAG. After an esptool
flash, the post-flash reset is done "via RTS pin," which is a **no-op on native
USB-JTAG** — the app doesn't restart until the board is **physically power-cycled**.
(The Waveshare boots fine after web-flashing because its CH343 auto-reset
circuit works.) So after reflashing tonight the board sits halted and can't be driven
over serial until a manual replug. The chip is fine and reflashable (esptool reaches it
every time); it's currently on **clean `esp32-s3`** firmware, no instrumentation.
