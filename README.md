# ocellus

> **ocellus** *(n., pl.* **ocelli***)* — a simple eye. The kind arthropods have: round,
> unlidded, watching.

An eye on a 240×240 round LCD. It looks around, blinks, dilates, gets bored, rolls its eyes at
you, and — if you happen to be near the **Wall of Sheep** — reacts to whatever's playing.
Configured over USB, no reflash.

<!-- TODO: hero video/GIF of an actual ocellus goes here. -->

---

## Using it

**One button:**

| gesture | what happens |
|---|---|
| single click | next mode (only the ones you favorited) |
| double click | the eye flinches (eye modes) |
| triple click | cycle the dev/debug screens |
| quadruple click | jump into / step through the effects |
| long press | power down — click again to wake |

**Touch** (on the round glass):

| gesture | what happens |
|---|---|
| swipe left / right | previous / next favorite |
| swipe up | open the **carousel** — a scrollable strip to scrub straight to any mode |
| tap | wake, jitter the eye, or feed the cat (treatcat mode) |

It sleeps on its own after a few idle minutes. Set `sleepMin` to `0` if you'd rather it never did.

## Configuring it

Each unit is configured over USB — no reflash, and everything survives a power cut. Open
**<https://nullphase.net/oc/>** in Chrome or Edge, plug the ocellus in, hit **Connect**, and pick
its port:

- **A name** — woven into the Matrix rain, spiralled out of the center, or revealed letter by
  letter at boot.
- **Brightness** (0–255), **sleep timeout**, **frame cap**, **180° flip** for an upside-down case.
- **Colors** — skin tone, iris tint, and mode backgrounds, each as a hex color.
- **Eyelids** on or off.
- **Favorites** — a checkbox per mode. Single-click and swipes cycle only the ones you ticked.
- **Palettes** — enable any of ten presets, add up to four of your own, and set how often they
  rotate. Switches crossfade rather than snap.
- **Your own content** — upload images for the slideshow, a QR code, or animated GIFs; they live on
  the device.
- **Startup** — resume where you left off, always start on one mode, or pick at random.

Firefox and Safari don't implement Web Serial, and the page needs `https` or `localhost` — opening
the file directly from disk won't work.

**No toolchain?** The one-click web flasher at **<https://nullphase.net/oc/flash/>** installs the
firmware straight from the browser: Connect, Install, done.

## The modes

**Eyes (13)** — Radiate, Glitch, Orbit, Breathe, Grid, Static, Rings, Void, Box, Magenta, Confetti,
Aztec, Mosaic.

Every eye theme has moods. It gets curious, skeptical, calm, or drowsy depending on the theme and
how long you've been watching, and the mood drives gaze, lid position, pupil size, and how often it
decides to look away from you.

**Effects** — Matrix, Cube, Plasma, Tesseract, Tunnel, Weave, Sonar, Squares, Bars, Ripple, Spokes,
Name Spiral, Starfield, Mystify, DVD, Pipes, Fractal, Swirl — plus a physics-and-creative set: Fluid
(tilt-driven), Yin-Yang, Wormhole, Toasters, Boids, Garden Eels, and seven ports from a
creative-coding lab: Julia, Interference, Munching Squares, Wireframe Globe, Rose Window, Polar
Rose, Fermat Spiral.

**Interactive** — Slideshow (your images), QR (your code), GIFs (your clips), and **treatcat**, a
little cat you tap to feed.

**Audio (4)** — Bloom, Radial Spectrum, Reactive Iris, Echo.

The audio modes are **not standalone**. They listen for a
[Sensory Bridge](https://github.com/connornishijima/SensoryBridge) broadcasting its 64-bin spectrum
over ESP-NOW. A stock Sensory Bridge doesn't broadcast that — it's our units that do, so in practice
this lights up if you're near the **Wall of Sheep**. With nothing on the air, the audio modes just
sit there showing nothing.

## Hardware

Units ship on the
**[Waveshare ESP32-S3-Touch-LCD-1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28)** — a
self-contained round board with the GC9A01 display, an onboard LiPo charger and battery header, a
PWM backlight, capacitive touch, and a 6-axis IMU the tilt-reactive modes read. Env
`esp32-s3-touch-128`. A parametric fob enclosure lives under [`hardware/`](hardware/).

The firmware also builds for a bare **ESP32-S3-DevKitC-1** (the bench rig), the **ESP32-S3-Zero**,
and the **legacy ESP32-C3**. Pins live in one block at the top of `main.cpp`.

## Building it

PlatformIO, from its venv:

```sh
~/.platformio/penv/bin/pio run -e esp32-s3-touch-128     # build (Waveshare — the ship board)
~/.platformio/penv/bin/pio run -e esp32-s3               # build (bare S3 devkit — bench rig)
~/.platformio/penv/bin/pio run -e esp32-c3-devkitm-1     # build (legacy C3)
~/.platformio/penv/bin/pio test -e native                # host unit tests
```

To flash, don't guess the port. `tools/flash.py` probes every `/dev/cu.usbmodem*` with the config
protocol and only flashes the one that answers like an ocellus:

```sh
~/.platformio/penv/bin/python tools/flash.py s3-touch --anim 24
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

**Adding an effect:** append an entry to `ANIMS[]` in `animations.h` with a fresh id above the
current top, and wire a branch into `loop()`'s dispatch. The eye/effect ids run 0–37, then effects
continue *above* the pinned audio (38–41) and debug (42–44) blocks at 45+. Those pinned ids must
never move — units in the field have them in saved configs — so the id space has holes, and
membership is tested with `isPlayableId()` / `animIdKnown()`, never `id < ANIM_COUNT`. The config
page reads the registry over serial, so it picks up the new mode with no edits.

**Things that will bite you:**

- Call `Serial.setRxBufferSize(2048)` *before* `Serial.begin()`. The default 256-byte RX ring
  silently truncates a full config payload, and the symptom is "the name won't save."
- SPI runs at **80 MHz** on the Waveshare and the S3-Zero, 40 MHz on the bench devkit and C3. It's
  wiring-dependent — 80 MHz blanked the panel over breadboard jumpers on one early rig. Drop toward
  20 MHz if a new build glitches.
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
