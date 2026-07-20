# Audio: the debug screens, and what the numbers mean

The eye visualizes an audio spectrum streamed over ESP-NOW from a SensoryBridge console. This
document is for the person sitting in front of a device with music playing, trying to work out why a
visual isn't doing what they expect. It covers what the debug screens show, what the detectors
actually fire on, and which constant to move when a reading is wrong.

Source of truth for the tuning constants: `audio.h`. Everything here describes the code in that file
and in `onEspNowRecv` / `renderAudioDebug` / `renderWaterfall` (`main.cpp`).

---

## Getting to the screens

```bash
~/.platformio/penv/bin/python tools/flash.py s3-zero --anim 37     # audio debug
~/.platformio/penv/bin/python tools/flash.py s3-zero --anim 38     # spectrogram waterfall
~/.platformio/penv/bin/python tools/flash.py s3-zero --anim 32     # Bloom (the mode the detectors drive)
```

Add `--no-build` to just jump modes on a device that already has current firmware. Swap `s3-zero`
for `s3` / `s3-touch` / `c3` as appropriate.

**The ids move.** They are computed from `ANIM_COUNT` (`animations.h`), so adding one animation
shifts every debug id by one. Do not trust an id you read in a comment, a plan, or in `CLAUDE.md` —
`animations.h`'s catalog table is the only authority. As of this writing:

| id | mode | |
|----|------|---|
| 32–35 | Bloom, Radial Spectrum, Reactive Iris, Echo | the audio modes |
| 36 | Sensor Debug | |
| 37 | **Audio Debug** | the tuning instrument |
| 38 | **Waterfall** | all 64 bins over time |

Debug screens sit above `ANIM_COUNT`, so they stay out of the button cycle and out of favorites.
37 and 38 power the radio, like the audio modes.

---

## The spectrum: what the 64 bins actually are

The console maps bin `i` to `notes[i + NOTE_OFFSET]`, and `NOTE_OFFSET` defaults to 12
(SensoryBridge `system.h` / `globals.h`), against a chromatic table starting at 55 Hz. So the bins
are **musical notes, not linear frequency**, and the band edges land here:

| band | bins | frequency | what lives there |
|------|------|-----------|------------------|
| `bass` | 0–7 | 110–165 Hz | kick fundamental |
| `mid` | 8–23 | 175–415 Hz | **snare body**, bass guitar, low vocals |
| *(unaggregated)* | 24–45 | 415–1480 Hz | vocals, guitars — no detector, by choice |
| `high` | 46–63 | 1568–4186 Hz | hats, snare *crack* |
| `level` | 0–63 | everything | the whole mix |

Two consequences worth holding onto:

- **A snare's crack reaches `high`.** Before the mid band existed, a snare fired a *spark* and
  nothing else — it rendered as a hi-hat. That is the hole this all exists to close.
- **The console auto-ranges in two zones**, split at bin 32 (`NUM_ZONES = 2`). A band straddling
  that boundary has its halves normalized independently and will drift for reasons unrelated to the
  music. This is why `mid` stops at bin 23 and does not extend into the 24–45 gap.

Every band value is put through `audioBin()`, which normalizes to 0..1 and **squares** it. That
curve never clips at any console CONTRAST setting — but it also means loud bands sit high and
quiet ones get compressed hard. Several surprises below trace back to that squaring.

---

## Audio Debug (id 37)

### The rows

```
        AUDIO DEBUG  id37
 state  LIVE                     gap ms
 pkt/s  176                 p50     5
   age     6 ms             p90     6
  bass  118 ^201 s174       p99    12
   mid   64 ^131            max    41
   lvl   90 ^160 s142      lost     0
  trig  b 2 s 9              rej     0
 snare  n 2 v 1 r 0          stl     0
   fps   58
```

**`state`** — `LIVE` or `STALE`. Stale means no packet for 500 ms; every audio mode falls back to a
self-animating idle breath rather than freezing.

**`pkt/s`** — packets actually arriving. The console streams ~175/s. This is published on a 1-second
window, like every number here except the instantaneous band values (a digit changing 58 times a
second is unreadable).

**`age`** — worst gap, in ms, between now and the last packet, over the window.

**Band rows** (`bass` / `mid` / `lvl`) — three numbers: **instantaneous**, **`^` peak over the
window**, and **`s` the AGC-stretched value** where one exists. The stretched value is what the
renderers actually size things by; the raw value is what the detectors fire on. They differ on
purpose, and `mid` has no `s` column because nothing renders a mid-derived size.

**`trig`** — `b` beats (kicks) and `s` sparks (treble edges) per second.

**`snare`** — `n` fired, `v` vetoed, `r` refractory-eaten, per second. This row is the instrument;
see below.

**`fps`** — render rate. Also on the `[prof]` serial line, along with free heap and pkt/s.

### The right column: link health, not render health

`p50` should sit **on the console's send interval** — about 5–6 ms at 176 packets/s. That is the
number that tells you the link is healthy. The tail is what matters after that: `p99` and `max` well
above `p50` mean packets that never arrived, and `lost` is the loss rate inferred from that tail. A
`max` alone can't distinguish one stall a minute from one a second; `p99` can.

`rej` counts junk frames — a foreign ident, an unknown command, or a mags frame of the wrong size.
**Nonzero `rej` that climbs steadily means protocol drift**: the console's wire format has moved and
ours hasn't. `stl` counts LIVE→STALE transitions since the screen came up.

### The trace

The rolling plot at the bottom is the last ~120 frames, oldest column at the left.

- **Orange/blue bars** — the `bass` band. Orange means the sample cleared its live trigger bar.
- **Dotted line** — that bar (`thrTrace`). Not a static floor: it is the *live* threshold, which
  includes the baseline's proportional margin, so you can watch a rumble lift the bar out of a
  kick's reach.
- **Green line** — the `mid` band, with its own dimmer green line below it: mid's live bar.
- **Purple line** — the `high` band.
- **Ticks below the baseline** — ground truth: white = a beat fired, purple = a spark, green = a
  snare. These come from *differencing the RX-side counters*, so they are what the device actually
  fired, not a re-detection on the render side.

Clearing the dotted line **is** the whole rising test. The only thing between "bar cleared" and
"fired" is the refractory window — which is why the ticks, not the bar colors, are ground truth.

**The snare tick row is bezel-clipped at both ends** — roughly the 4 oldest and 3 newest columns.
A snare landing *as you watch* may not draw its tick. The `n`/`v`/`r` counters are the ground truth;
the ticks are for reading rhythm against the trace.

---

## Waterfall (id 38)

All 64 bins over time: one column per frame, ~4 seconds of history, newest at the right, bass at the
bottom. Colors run black → blue → orange → white with magnitude.

Three dotted guides mark the band edges, so you can see **which bins a given drum actually lights**.
The lower two bracket the mid band (8–23); a snare should visibly repeat *between* them. The gap
between the top guide and the high band is bins 24–45, which deliberately feeds no detector.

Ticks: beats and snares below the plot, sparks above. Same bezel clipping as id 37, worse — the
newest ~23 columns don't draw ticks.

Each column is a **peak-hold across every packet since the last frame**, computed in the RX callback.
This matters: at ~175 packets/s against ~58 fps, a frame-sampled column would miss 2 of every 3
packets, and a snare can live and die inside a single frame. That blindness is the reason this
screen exists.

---

## The detectors

All three run **per packet, in the WiFi RX callback** (`onEspNowRecv`), not per frame. A kick is
~60 ms — it can begin and end entirely within one rendered frame. Detecting on the render side made
which transients fired a function of flush timing, which is why Bloom used to feel arbitrary.

Each is a `BeatDetector`: a rising-edge detector that fires when a sample clears a **live bar**

```
bar = max(floorV, prev + margin, base + margin)      margin = max(rise, base / marginDiv)
```

and is not inside its refractory window. `base` is a slow baseline EMA. Three things are doing work
there:

- **`prev + margin`** — the band must be *rising*, not merely loud. This is what makes a decaying
  kick score negative instead of vetoing everything behind it.
- **`base + margin`** — the band must outrun its own recent history. This is what rejects a
  breakdown's rumble: sustained energy drags the baseline up with it until it stops qualifying.
  What separates a kick from rumble is **duration**, not loudness.
- **`margin` scales with level** — because `audioBin`'s curve is squared, its slope grows with
  magnitude. Up at the top, ordinary band noise moves the value by ~15 counts, and any fixed rise
  threshold gets machine-gunned. `marginDiv` sets how hard that bites, and it differs per band:
  bass needs a harsh `/4` (rumble is common), treble a gentle `/10` (hats *are* transients, and on
  brickwalled material the treble baseline sits near full scale where a `/4` margin demands a jump
  no hi-hat ever makes).

| detector | band | fires | refractory | visual |
|----------|------|-------|------------|--------|
| `rxBeat` | bass | kick | 120 ms | ring expands **outward** from the core |
| `rxMid` | mid | snare | 100 ms | ring collapses **inward** from the bezel |
| `rxSpark` | high | hi-hat | 90 ms | sparks |

Sparks fire on a treble **edge**, never a level: the console peak-holds what it sends, and a
peak-hold is *designed* to keep the level up, so any level threshold over-fires under it.

### The snare's kick veto

A kick's beater click lands in 175–415 Hz too, so an ungated mid detector fires on every kick and
every kick would draw both an outward *and* an inward ring. The guard: **a snare only counts when
its mid transient out-scores the bass transient**, each measured against its own bar.

```cpp
snareWins = midS * SNARE_VS_KICK_NUM > bassS * SNARE_VS_KICK_DEN
```

The semantics are **asymmetric on purpose**: the kick fires unconditionally, and only the snare must
win a comparison. A kick that really happened should draw its ring even when a snare lands on the
same beat.

The semantics work out to:
- **An isolated snare always wins**, at any weight — bass is flat or decaying at snare time, so
  `bassS` is negative. This is a normal backbeat, and it is the case the feature exists for.
- **A snare landing on the same packet as a kick** is the only case the weight arbitrates. At the
  1:1 default the coincident fixture (`[bd,sd]`) already draws both rings 20/20; raise
  `SNARE_VS_KICK_NUM` only if real material shows a same-packet bias.

> **A design-time theory that the offline sweep overturned.** This weight was introduced to correct
> a predicted *scale* bias: bass being 8 near-fully-lit bins high on the squared curve, mid a wider
> average of a band a snare only partly lights, so a kick was expected to out-score a snare and the
> weight was thought to be the only lever. Measured against the `.tap` corpus
> (`tools/snare_sweep.cpp`) the real problem was elsewhere: the mid band **started at bin 8, inside
> the kick's body** (a kick floods bins 0–15 at full scale), so a kick's own mid score beat its bass
> score and fired a phantom snare on *every* kick — 20 false positives on a bare `bd*4`, and the
> veto never engaged (`v=0`). Moving `MID_BIN_LO` to 18 fixed it outright, and the sweep then found
> every weight from 1:4 to 3:1 equally viable — i.e. the weight is **not load-bearing** on this
> corpus. The scale-bias story was plausible and wrong; the band placement was the whole thing.

---

## Tuning: sweep offline first, read the `snare` row second

The constants are in `audio.h`. **The tool of record is now the offline sweep**, not reflash-and-listen:
`tools/snare_capture.py` records the ESP-NOW stream to a `.tap` fixture against a known Strudel
pattern, and `tools/snare_sweep.cpp` replays it through the real `audio.cpp` and grid-searches all
six constants in seconds. The
current values came out of that sweep, and the six `test/fixtures/*.tap` captures are committed so
any future change is a re-run, not another bench session. The `snare` row below is now the *field*
check, for real material the corpus can't cover — target on a plain 120 BPM backbeat: **`n ≈ 2/s`**,
and **no snare tick under a bare kick**.

| reading | what it means | move |
|---------|---------------|------|
| `n=0 v=0 r=0` | mid never cleared its bar | lower `SNARE_MID_FLOOR` (40), then `SNARE_MID_RISE` (15) |
| `n>0 under a bare kick, v=0` | the mid band is catching kick body | raise `MID_BIN_LO` (18) — this, not the weight, is what the sweep found. A kick floods bins 0–15; the band must clear them |
| `n=0 v>0` | the kick out-scores the mid on a *coincident* hit | raise `SNARE_VS_KICK_NUM` (1) — but only if bass and snare truly share the packet; if it's a bare kick, it's the row above |
| `r>0` | a real snare landed inside the refractory window | lower `SNARE_REFRACTORY_MS` (100) |
| `n` machine-guns (≫4/s) | a busy midrange is triggering | lower `SNARE_MARGIN_DIV` (6) toward bass's harsh `/4` |
| backbeat fine, but nothing when kick and snare coincide | the 1:1 default, as designed | raise `SNARE_VS_KICK_NUM` to 2 or 3 |
| snares land in the wrong bins on id 38 | the band is misplaced | move `MID_BIN_LO` / `MID_BIN_HI` — but **not past bin 32**, the console's zone boundary |

`v` and `r` exist precisely so a missing snare is diagnosable. Without them, "no snare fired" has
three indistinguishable causes: it never cleared its bar, the kick out-scored it, or the refractory
window ate it.

### One wart, deliberately kept

A mid transient that gets **vetoed** still arms `rxMid`'s refractory window — `update()` stamps its
timer when it fires, before the veto is applied. So a kick with heavy body silences the snare for the
next 100 ms. This is musically defensible (a kick is a kick even when it has body) and the
alternative threads a band-comparison concept into a struct whose whole job is single-band edge
detection. It is not invisible: **`r` counts exactly this case.** If `r` climbs on real music,
`SNARE_REFRACTORY_MS` is the first suspect.
