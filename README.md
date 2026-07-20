# ocellus

> **ocellus** *(n., pl.* **ocelli***)* — a simple eye. The kind arthropods have: round,
> unlidded, watching.

An eye on a 240×240 round LCD. It looks around, blinks, dilates, gets bored, rolls its eyes at
you, and — if there's a [Sensory Bridge](https://github.com/connornishijima/SensoryBridge) on the
network — reacts to whatever's playing. Each one is configured for the person it belongs to over
USB, no reflash.

<!-- TODO: hero video/GIF of an actual ocellus goes here. -->

---

## Using it

**One button.**

| gesture | what happens |
|---|---|
| single click | next mode, skipping anything you didn't favorite |
| double click | the eye flinches (eye modes only) |
| triple click | jump into the effects |
| long press | power down — click again to wake |

It also sleeps on its own after a few idle minutes. Set `sleepMin` to `0` if you'd rather it never
did.

## Personalizing it

Open **<https://nullphase.net/oc/>** in Chrome or Edge, plug the ocellus in over USB, hit
**Connect**, and pick its port. Everything below is stored on the device and survives a power cut:

- **Your name** — woven into the Matrix rain, spiralled out of the center, or revealed letter by
  letter at boot (matrix / slide / bounce).
- **Brightness** (0–255), **sleep timeout**, **frame cap**, **180° flip** for an upside-down case.
- **Colors** — skin tone, iris tint, and the Void mode's background, each as a hex color.
- **Eyelids** on or off.
- **Favorites** — a checkbox per mode. Single-click only cycles the ones you ticked.
- **Palettes** — enable any of ten presets, add up to four of your own, and set how often they
  rotate. Switches crossfade rather than snap.
- **Startup** — resume where you left off, always start on one mode, or pick at random.

Firefox and Safari don't implement Web Serial, and the page needs `https` or `localhost` — opening
the file directly from disk won't work.

## The modes

**Eyes (13)** — Radiate, Glitch, Orbit, Breathe, Grid, Static, Rings, Void, Box, Magenta, Confetti,
Aztec, Mosaic.

Every eye theme has moods. It gets curious, skeptical, calm, or drowsy depending on which theme
you're on and how long you've been watching, and the mood drives gaze, lid position, pupil size, and
how often it decides to look away from you.

**Effects (17)** — Matrix, Cube, Plasma, Tesseract, Tunnel, Weave, Sonar, Squares, Bars, Ripple,
Spokes, Name Spiral, Starfield, Mystify, DVD, Pipes, Fractal.

**Audio (3)** — Bloom, Radial Spectrum, Reactive Iris.

The audio modes are **not standalone**. They listen for a Sensory Bridge broadcasting its 64-bin
spectrum over ESP-NOW; without one on the network they'll sit there showing nothing. The console's
CONTRAST knob shapes their gamma, by design — one knob drives every display on the network.

## Hardware

Today's firmware builds for an **ESP32-S3-DevKitC-1** driving a bare GC9A01 over hardware SPI —
that's the bench rig. The **legacy ESP32-C3** target still builds.

The board these are actually meant to ship on is the
[Waveshare ESP32-S3-Touch-LCD-1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28): same
display, onboard charger and battery header, a real PWM backlight, and a 6-axis IMU that should make
the manual flip toggle obsolete. **That port isn't done** — the pinout and the deltas are written up
in [`docs/hardware-esp32-s3-touch-lcd-1.28.md`](docs/hardware-esp32-s3-touch-lcd-1.28.md), and
there's a case for it in [`docs/`](docs/).

Pins live in one block at the top of `main.cpp`.

## Building it

PlatformIO, from its venv:

```sh
~/.platformio/penv/bin/pio run -e esp32-s3               # build (primary target)
~/.platformio/penv/bin/pio run -e esp32-c3-devkitm-1     # build (legacy target)
~/.platformio/penv/bin/pio test -e native                # host unit tests
```

To flash, don't guess the port. `tools/flash.py` probes every `/dev/cu.usbmodem*` with the config
protocol and only flashes the one that answers like an ocellus:

```sh
~/.platformio/penv/bin/python tools/flash.py s3 --anim 24
```

`--anim` re-selects a mode after the reboot, which is most of what you want while iterating on one.

A connected config page holds the serial port open, and both the probe and the upload will fail with
`Resource busy` until you close that tab.

## Contributing

Sources live at the repo root — `src_dir = .` — not in `src/`.

| file | what it is |
|---|---|
| `main.cpp` | rendering, dispatch, button, sleep. The big one. |
| `animations.h` | the registry: id ↔ name ↔ group. Ids are the stable key; names are free to change. |
| `config.*` | `Config` struct + JSON codec |
| `protocol.*` | the `catalog` / `get` / `set` line handler |
| `palette.*` | palette engine and crossfade |
| `audio.*` | Sensory Bridge wire decode |
| `config_store.*` | NVS persistence (namespace `ocellus`) |
| `config.html` | the Web Serial config page, self-contained |

`config.*`, `protocol.*`, `palette.*`, and `audio.*` are deliberately Arduino-free, so the `native`
env compiles and tests them on a host. Keep them that way — it's why there are tests at all.

**Adding an effect:** append to `ANIMS[]`, bump `EFFECT_COUNT`, and add a `case` to `renderEffect()`
(its switch is 0-based within the effect group, so the 18th effect is `case 17`). The config page
reads the registry over serial, so it picks up the new mode with no edits.

Mind the catch: `loop()` dispatches by *id range*, not by the registry's `group` field, and the audio
modes start at `EYE_COUNT + EFFECT_COUNT`. Growing the effect group therefore renumbers the audio
modes, and a saved `favoritesMask` or `startupId` on an existing device will point at the wrong
thing. Ids are only stable within a group. Unifying that dispatch is on the list.

**Things that will bite you:**

- Call `Serial.setRxBufferSize(2048)` *before* `Serial.begin()`. The default 256-byte RX ring
  silently truncates a full config payload, and the symptom is "the name won't save."
- SPI runs at 40 MHz. That's wiring-dependent — 80 MHz blanked the panel over breadboard jumpers.
  Drop toward 20 MHz if a new build glitches.
- The button is polled in its own FreeRTOS task. A full-framebuffer flush takes long enough to starve
  inline polling.
- The C3 has no FPU, so `float` is software-emulated. Keep trig out of hot loops while that target
  still builds.

## Provenance

Forked from **[Jekyllz/ESP32-third-eye](https://github.com/Jekyllz/ESP32-third-eye)** by Jake, whose
original ~370-line sketch is the seed this grew from. He sells
[kits and a PCB adaptor](https://www.tindie.com/products/jekyllz/esp-flashy-keychain/) for the C3
keychain build, publishes [the case as an STL](https://www.printables.com/model/1755628-case-for-the-esp32-digital-keychain),
and is [reachable on Reddit](https://www.reddit.com/user/Jekyllz/). If you want the original
keychain rather than this, go build his — it's a lovely little thing.

This fork went its own way: moods, gaze, palettes, audio reactivity, per-unit configuration, a host
test suite, and an S3 port. None of it is upstreamed.

## License

[MIT](LICENSE).

Upstream carried no license — all rights reserved by default — but Jake gave his blessing to
release this fork under an open license, so it ships MIT. Credit for the original seed is his;
see Provenance above.
