# Port target: Waveshare ESP32-S3-Touch-LCD-1.28

Candidate **final hardware** (replacing the breadboard / bare devkit). This note captures the
pinout and the concrete firmware deltas to switch to it.

**APPLIED 2026-07-10** — board arrived and brought up. Env `esp32-s3-touch-128` (PlatformIO forbids
`.` in env names), pins gated behind `-D BOARD_WAVESHARE_128`. Display confirmed working at **80 MHz
SPI**; **`TFT_RST` = 14 confirmed** (12 not needed). Config protocol confirmed over CH343/UART0.
USB port enumerates as `/dev/cu.usbmodem*` (VID:PID `1A86:55D3`, "USB Single Serial"), *not*
`wchusbserial*` as originally guessed — same WCH CH343 either way.

## Why this board
- **Same display** — 1.28" 240×240 round **GC9A01**. The whole rendering path works unchanged, driver-wise.
- **Same chip family** — ESP32-S3R2, 2 MB PSRAM (matches the existing `esp32-s3` env). 16 MB flash.
- **Finished object** — onboard Li charger (ETA6096) + MX1.25 battery header + 2 A regulator + USB-C.
  No breadboard, no jumper-SPI glitches. Drop it in a case and hand it over.
- **Real backlight** on GPIO2 (PWM) — brightness config finally visible.
- **QMI8658 6-axis IMU** — free superpower: auto-flip via accelerometer (replaces the manual
  rotate-180 config toggle), plus tilt/shake-reactive eye modes. This is the reason to pick it over a
  plain devkit.

## Biggest change: USB is CH343 UART, not native USB-Serial/JTAG
The USB-C port routes through the **CH343P → UART0 (TX=GPIO43, RX=GPIO44)**, *not* the S3's native
USB-Serial/JTAG. Consequences:
- **Config transport survives with one build flag.** Set `ARDUINO_USB_CDC_ON_BOOT=0` so `Serial`
  becomes UART0 → CH343 → USB-C. `config.html` Web Serial works unchanged (it's just a serial port;
  enumerates as a `wch` CH343, e.g. `/dev/cu.wchusbserial*`).
- **`setRxBufferSize(2048)` still required** — UART0's default RX buffer is also 256 B (same class of
  bug as the HWCDC 256 B ring).
- **Flashing** is over the CH343 port with auto-reset; point `--upload-port` at the `wchusbserial`
  device. esptool's chip-mismatch check won't help here (it sees S3 regardless).

## Pin map: current `main.cpp` → this board

| define | current (S3 devkit bench) | Waveshare 1.28 | source |
|--------|---------------------------|----------------|--------|
| `TFT_MOSI` | 11 | **11** (same) | TFT_eSPI thread |
| `TFT_SCLK` | 12 | **10** | TFT_eSPI thread |
| `TFT_CS`   | 6  | **9**  | TFT_eSPI thread |
| `TFT_DC`   | 5  | **8**  | TFT_eSPI thread |
| `TFT_RST`  | 7  | **14** ✅ | confirmed on hardware 2026-07-10 (not 12) |
| `TFT_BLK`  | 16 | **2**  | Waveshare docs (confirmed) |
| `BUTTON_PIN` | 15 | **0** (BOOT) or touch | no dedicated user button |

Proposed block:
```c
// --- HARDWARE PINS (Waveshare ESP32-S3-Touch-LCD-1.28) ---
#define TFT_MOSI 11
#define TFT_SCLK 10
#define TFT_CS    9
#define TFT_DC    8
#define TFT_RST  14      // verify vs schematic — 12 seen on the non-touch variant
#define TFT_BLK   2      // real PWM backlight
#define BUTTON_PIN 0     // BOOT button; strapping pin, held-low at reset = download mode
// I2C (CST816 touch + QMI8658 IMU): SDA=6 SCL=7 | battery ADC=1 (reads Vbat/3) | UART0 TX=43 RX=44 -> CH343 -> USB-C
```

## `platformio.ini`: new env
```ini
[env:esp32-s3-touch-1.28]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
build_src_filter = +<*> -<test/>
board_build.flash_size = 16MB
board_build.arduino.memory_type = qio_qspi   ; S3R2 = Quad-SPI PSRAM (NOT opi_*)
build_flags =
    -D BOARD_HAS_PSRAM
    -D ARDUINO_USB_CDC_ON_BOOT=0             ; USB-C is CH343 -> UART0, not native USB-Serial/JTAG
lib_deps =
    moononournation/GFX Library for Arduino @ 1.3.7
    mathertel/OneButton @ ^2.0.3
    bblanchon/ArduinoJson @ ^7.0.0
```

## Other code touches
- **SPI host** — the TFT_eSPI crowd hit a `StoreProhibited` crash needing `USE_HSPI_PORT`. We use
  `Arduino_ESP32SPI`, not TFT_eSPI, so likely fine — but if `begin()` crashes, pass an explicit SPI
  host to the `Arduino_ESP32SPI(...)` constructor.
- **Bump SPI to 80 MHz** — no jumper wires; Waveshare rates the LCD at 80 MHz. `gfx->begin(80000000)`
  should hold on the PCB and roughly halves flush time.
- **Battery gauge** — ADC on GPIO1, multiply by 3 (200K/100K divider). `readBatteryMv()` + a
  `{"cmd":"bat"}` serial reply are **APPLIED**, with a **per-unit correction factor** (×4187/4092,
  calibrated 2026-07-16): the simultaneous on-USB pair read meter 4.187 V vs ADC 4.092 V — 95 mV low,
  classic 1% divider stack-up. Compare a meter against `{"cmd":"bat"}` on each new unit (the
  meter-vs-ADC pair check is **mandatory per shipped unit** — now that stayAwakeUsb rides 4150/4100 mV
  thresholds with only ~20–50 mV margin, an uncalibrated unit can sleep on the desk or never idle-sleep
  on battery); resting (off-USB) readings don't pair with on-USB ADC reads — the charger holds Vbat ~130 mV higher.
  **Low-battery UI shipped 2026-07-16** (verified on the assembled unit, all states): LOW (<3.5 V EMA)
  = drowsy eye + ⅔ backlight + red battery splash (entry + every 5 min), CUTOFF (<3.3 V ×3 raw
  samples, ~15 s) = splash + `powerOff()`; tap-to-wake works from that sleep. No charge UI by design
  (the ETA6096 status line isn't wired to a GPIO — charging can't be detected honestly). Logic in
  `battery.*` (host-tested); `{"cmd":"batsim","mv":N}` fakes the ADC for bench tests (0 = real ADC) —
  **verify the echoed mv**: the UART loses bytes to frame-gap light-sleep naps and a mangled line can
  parse as a truncated number (3100 arrived as 31 on the bench).
  **USB presence** (stayAwakeUsb, 2026-07-16) is also voltage-only: `BatteryMonitor` latches
  usb=true at EMA ≥4150 mV / false <4100 (charger holds the cell ~4.17–4.26 V; the board's own load
  sags a full cell below ~4.13 off USB). A GPIO route doesn't exist: ETA6096 status unwired, and the
  CH343's TX idles high off the **3.3 V rail, not VBUS** (measured — a GPIO44 idle-level sense read
  always-high on battery and was reverted same day).
- **PSRAM** — 2 MB available; the 115 KB canvas can move out of internal DRAM if needed.

## IMU auto-flip (QMI8658) — APPLIED 2026-07-10
Accelerometer-driven 180° auto-flip is live (`imu.cpp`/`imu.h`, gated on `BOARD_WAVESHARE_128`).
- **Chip:** QMI8658C (WHO_AM_I 0x00=0x05, rev 0x01=0x7C), I2C addr **0x6B** on SDA=6/SCL=7.
- **Register-map gotcha (cost hours):** the `CTRLn` names are **not** their addresses — the map is
  sequential `CTRL1=0x02, CTRL2=0x03, CTRL3=0x04 … CTRL7=0x08`. The accel range/ODR (CTRL2) lives at
  **0x03**, not 0x06 (0x06 is CTRL5/LPF). Writing accel config to the wrong reg = enabled-but-never-
  converting (STATUS0 `aDA` stays 0, output frozen). Init: reset `0x60=0xB0`, poll reset-done
  `0x4D==0x80`, `CTRL1(0x02)=0x40`, `CTRL2(0x03)=0x16` (±4g/125Hz), `CTRL7(0x08)=0x01`; accel out at
  0x35–0x3A, little-endian. Note an ESP32 EN-reset doesn't power-cycle the IMU, so the soft reset matters.
- **Orientation:** accel **X is screen-vertical**; flip logic uses a ~0.5g dead zone (hold last decision
  when the board is flat) and is polled every 200 ms in `loop()`. When the IMU is present it **owns**
  `gfx->setRotation()`; the manual `flip` config only applies on non-IMU boards.

## Touch gestures (CST816S) — APPLIED 2026-07-10
Swipe L/R = prev/next enabled anim, tap = jitter (`touch.cpp`/`touch.h`, gated on `BOARD_WAVESHARE_128`,
polled in `buttonReadTask` alongside the button). The chip does gesture recognition itself; we only read it.
- **Chip:** CST816S, I2C addr **0x15** on the shared SDA=6/SCL=7 bus. **Pins confirmed on hardware:**
  RST = **GPIO13**, INT = **GPIO5** (both verified empirically — gestures fire and, with INT-gating on, the
  idle NAK flood stops only when 5 is really INT). Gesture reg `0x01`: `0x03`=swipe-left, `0x04`=swipe-right,
  `0x05`=tap. Finger-count reg `0x02`. Emit-on-release latch (swipe wins over an early tap flag).
- **Stack-overflow gotcha (cost a debug cycle):** `buttonReadTask` shipped with a **2048-byte** stack sized
  for `button.tick()` only. Adding `touchPoll()`'s Wire I2C to it overflowed the stack — every failed read
  hits `Wire.cpp`'s `log_e()` (a stack-hungry `vprintf`), which tipped the 2 KB canary → `Guru Meditation …
  Stack canary watchpoint triggered (ButtonTask)` → reboot, most visible entering audio modes (ESP-NOW load).
  Fix: **4096-byte** stack (`xTaskCreate(..., 4096, ...)`).
- **Idle NAK flood (cost the same cycle):** the CST816S auto-sleeps when idle and **NAKs I2C reads** (`Wire.cpp:499
  i2cWriteReadNonStop returned Error -1`). Polling it every ~17 ms from `buttonReadTask` (priority 2, above the
  priority-1 render loop) burned ~40% of the core on failing reads + serial logging → **Bloom lag** + spam on the
  config serial. Fix: **gate idle polling on INT** — skip the bus while no touch is in progress and INT (GPIO5)
  is de-asserted (HIGH); once `down`, keep polling regardless of INT so finger-up still emits. Idle Wire errors
  went 2065 → **0**.

## Verify against the schematic before switching
- `TFT_RST`: **12 vs 14** — the one ambiguous LCD pin.
- IMU (QMI8658) **INT1/INT2** lines — unused (auto-flip polls over I2C); grab from the schematic only if a
  motion-interrupt feature is ever wanted. Touch INT/RST are confirmed above; I2C bus is SDA=6 / SCL=7.

## Battery & power (selection + measured draw)

### Measured on THIS board, USB inline meter @ 5.17 V (2026-07-12)

Taken after the render/flush work (framebuffer moved out of PSRAM — see CLAUDE.md), so these are
current. Non-audio = eye id 0, 30 fps cap, brightness 120.

| state | draw |
|---|---|
| non-audio, before light sleep | 64 mA (0.33 W) |
| **non-audio, with light sleep in the frame gap** | **52 mA (0.27 W)** ← shipping config |
| audio mode (Bloom, radio up, ~60 fps) | 127 mA (0.65 W) |
| backlight @ brightness 120 | ~15 mA of the above |
| ESP-NOW radio (RX, continuous) | ~50 mA of the audio figure |

Component breakdown that matters:
- **The backlight is NOT the dominant load** (~15 mA of 127). The silicon is. This was measured, not
  assumed — the intuition is wrong on this board.
- **The radio is the single biggest line item in audio modes** (~50 mA), and it can't be power-saved:
  the beat detector needs all ~175 pkt/s. Audio modes only matter 3–4 days a year (DEFCON), so this
  is accepted, not fixed.
- **CPU active power scales with clock; idle is cheap.** 80 MHz pinned flat-out drew *less* (50 mA)
  than 240 MHz idling 42% of the frame (64 mA). Clock is a real lever — but 80 MHz drops the heavy
  modes (Rings/Tunnel) to ~18 fps and 160 MHz leaves them zero margin, so we keep 240 MHz and sleep
  the gap instead.

**Full-discharge runtime, measured (2026-07-17):** a **602030 cell** (~250–300 mAh class), charged
full, running **audio modes** (radio up, no light sleep — the worst case) at **brightness 60**,
lasted **~110 minutes** to CUTOFF. Back-of-envelope: ~120 mA draw (127 audio − a few mA of backlight
saved at 60 vs 120) → ~220 mAh delivered, consistent with ~80% usable on a ~275 mAh cell. Audio is
the heaviest mode; non-audio with light sleep (52 mA) would roughly **2.3×** this on the same cell.
The whole **low-battery UI chain fired correctly on this live discharge** — LOW brightness-cap +
drowsy eyes → CUTOFF full-bright splash → deep sleep, no bench simulation involved.

### Light sleep in the frame gap — and why the backlight PWM must run on RTC8M

Non-audio modes render in ~19 ms against a 33 ms budget (30 fps cap), leaving ~14 ms of dead time per
frame. `loop()` now `esp_light_sleep_start()`s through that gap instead of spinning: **64 → 52 mA, ~19%.**
Gated on `!radioOn`, so it can only ever fire in non-audio modes (ESP-NOW would drop packets across a
sleep, and audio modes run uncapped anyway).

**The trap:** Arduino's `analogWrite()` drives LEDC from the **APB clock**, which light sleep gates off.
The PWM freezes mid-duty for the whole 14 ms nap, the pad holds whatever level it happened to be at, and
the backlight chops at 30 Hz — *very* visible flicker, and `brightness` stops meaning anything.

Fix (`backlightBegin()` in `main.cpp`): configure LEDC directly via `driver/ledc.h` with
`clk_cfg = LEDC_USE_RTC8M_CLK` (the internal 8 MHz oscillator, which keeps ticking through light sleep;
low-speed channels only), plus `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_ON)` to keep that
domain powered. 8 MHz / 2^8 = 31.25 kHz ceiling; we run 5 kHz. **Do not go back to `analogWrite` on the
backlight pin while light sleep is enabled.**

**Second trap:** that `ESP_PD_OPTION_ON` is global — it applies to **deep** sleep too, where it would keep
the oscillator burning in the state a shelved unit spends ~all its life in. `powerOff()` therefore sets it
back to `ESP_PD_OPTION_AUTO` before `esp_deep_sleep_start()`. **Verified: 1 mA in deep sleep** (USB side,
so that's mostly the CH343 + regulator).

**Third trap — this one bites hard:** wakeup sources are **sticky**. The frame-gap light sleep arms a
~13 ms `esp_sleep_enable_timer_wakeup()` *every frame* and nothing clears it, so `esp_deep_sleep_start()`
inherits it, wakes 13 ms later, and the board reboots — a long-press reads as a *restart*, not a power-off.
`powerOff()` must `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)`.
**Second instance of the same trap (2026-07-16):** the touch light-sleep wake (`touchBegin()` arms
`esp_sleep_enable_gpio_wakeup()` so frame-gap naps can't eat swipe INT pulses) is inherited by deep
sleep too — and the CST816S wiggles INT ~5 s after power-off (auto-standby entry), so a powered-off
unit woke itself right back up (`reset=8 wake=7(GPIO) src=cold`). `powerOff()` now also disarms
`ESP_SLEEP_WAKEUP_GPIO` (Waveshare-only — the C3 legitimately deep-sleep-wakes via its GPIO source).
Disarming it at the *top* of `powerOff()` is **not enough**: `powerOff()` runs in the **button task**, and
the `delay()`s that wait for button release let `loop()` run and re-arm the timer. Hence the `gPowerOff`
flag (blocks `loop()` from light-sleeping at all) *and* the disarm placed immediately before
`esp_deep_sleep_start()` with no yield after it.

**Testing gotcha:** `BUTTON_PIN` is GPIO0 (BOOT), which the CH343 auto-reset circuit drives from
DTR/RTS — scripted line-state choices are load-bearing. Measured 2026-07-16: both-deasserted is
**receive-only** (host→device bytes are dropped until DTR asserts); asserting after open still fires
the reset circuit; and a DTR-before-RTS assert order can drop the chip into **download mode**.
Read-only monitoring may open `dtr=False, rts=False`; anything that must *send* should copy
`tools/config_cli.py`'s deterministic esptool-style open (RTS-then-DTR: EN low with IO0 high,
release into both-asserted) and accept the reboot — multi-step device-state work rides one session
(see `raw`'s Enter pacing there).

### Older bench numbers (S3 DevKitC, 2026-07-08 — superseded, kept for the reg-loss note)

Bench draw (S3 DevKitC over USB, inline meter, 2026-07-08): most non-audio modes ~0.49 W
(0.094 A @ 5.18 V); **Tunnel is the heaviest non-audio mode**; audio modes ~0.85 W. Stripping the
DevKitC's AMS1117 linear-reg loss, real 3.3 V load is ~0.29 W non-audio / ~0.52 W audio, i.e.
**~85 mA non-audio, ~155 mA audio at 3.3 V** — audio ≈ **1.8× the drain**. On this board the reg
loss largely disappears, so treat these as the battery-side load. Firmware deep-sleeps after
`sleepMin`, so "runtime" = active watch time, not shelf days (idle standby is µA → weeks/months).

### Connector — verify BEFORE plugging anything in
- Header is **MX1.25 2P** (1.25 mm pitch, mechanically = Molex **PicoBlade**), 3.7 V Li only.
- **JST-PH 2.0 (Adafruit/SparkFun) does NOT fit** — buy a 1.25 mm-lead cell or re-crimp.
- **Have JST tooling but no 1.25 mm crimper?** Build one reusable **MX1.25→JST-PH adapter**: buy a
  **pre-crimped MX1.25 2P pigtail** (no 1.25 mm tool needed), crimp a JST-PH end yourself, splice
  the two (solder + heatshrink) **matching the board polarity** (meter the header +/− first). Then
  any JST cell plugs in. Skip ready-made MX1.25↔JST adapters — their polarity is a gamble and you
  can't re-pin without the tool. This frees cell choice from the connector — pick on fit/capacity only.
- **1.25 mm polarity is not standardized and the + pin is not stated in Waveshare's public docs.**
  Meter the header against the battery lead before first connect — reversed polarity can kill the
  ETA6096/board. Safest: Waveshare's own MX1.25 cell (guaranteed polarity).
- Charge is not a constraint: ETA6096 ~0.5 A class → any 350 mAh+ cell charges at ≤~1.5C.

### Fit in a round enclosure
A W×H cell fits an inner circle of diameter D when **√(W²+H²) ≤ D**. Waveshare LCD-module datasheet
(`docs/ESP32-S3-Touch-LCD-1.28-details-size-1.jpg`): outer glass **Φ38.51 mm**, black edge Φ35.67 mm,
VA (visible) **Φ33.40 mm**, height 38.51 mm round / **40.36 mm incl. the FPC tail** (~1.85 mm past
the circle at the bottom). That's the display module, not the PCB outline — board is ~Φ38.5–40 mm
class; confirm the PCB edge + MX1.25 header location on arrival. A case wrapping the glass gives
~Φ39–40 mm inside → cell diagonal ≤ ~39 mm, so the **500 mAh 30×25 (diag 39 mm) fits** for the unit
build. Bezel budget: (Φ38.51−Φ33.40)/2 ≈ **2.55 mm** of non-visible glass to tuck under a case lip
before clipping pixels. Thickness (typ. 4–8 mm) is a depth-budget call; leave room for the FPC tail.

**Tight boards / round cells:** the current bench round board is only **~36 mm** (≈2 mm past the
glass). A back that hugs it caps the diagonal at ~36 mm → ~250–350 mAh (25×20 / 25×25 footprints),
or a **round LiPo pouch** (Φ~34–35 mm, ~300–400 mAh) that suits a round case natively. Two ways to
keep capacity without a bigger front bezel: **flare the back into a puck** wider than the screen (a
~42–45 mm back reclaims the 500 mAh 30×25 cells), or trade width for **depth** (thicker, smaller-
footprint cell). The battery-bay diameter is a design choice independent of the screen — match the
final target to whichever board ships in the unit; the Waveshare may differ, measure it on arrival.

### Shortlist (runtime at ~80% usable, from ~85 mA / ~155 mA)
| Cell (code) | Cap | Footprint (mm) | Diag | Fits ~40 mm? | Non-audio | Audio |
|---|---|---|---|---|---|---|
| 602530 / 502530 | ~500 mAh | 30×25×6 | 39 mm | ✅ clean | ~4.7 h | ~2.6 h |
| Pimoroni/Pi Hut PicoBlade 500 | 500 mAh | ~36×20×6.5 | 41 mm | ⚠️ needs ~42 mm back | ~4.7 h | ~2.6 h |
| 402530 thin | ~350 mAh | 30×25×4 | 39 mm | ✅ (thin builds) | ~3.3 h | ~1.8 h |
| 502025 / 602025 | ~250–300 mAh | 25×20×5–6 | 32 mm | ✅ even at ~36 mm | ~2.4–2.8 h | ~1.3–1.5 h |
| round pouch | ~300–400 mAh | Φ34–35×5 | ~35 mm | ✅ round case | ~3.0–3.8 h | ~1.7–2.1 h |
| 503035 | ~500–600 mAh | 35×30×5 | 46 mm | ❌ needs ~46 mm back | ~5.5 h | ~3.0 h |

### Picks
1. **Waveshare's own 3.7 V MX1.25 cell** — guaranteed connector + polarity, zero re-pin risk. Safe default.
2. **~500 mAh 30×25 mm cell w/ 1.25 mm lead** — best capacity that clears a 40 mm puck; verify polarity.
3. **Pi Hut / Pimoroni 500 mAh PicoBlade** — reputable Western source, connector already right; size the back a hair over the display, check polarity vs. Waveshare's convention.

### Verify before ordering
- Exact PCB outline diameter (ruler on the board).
- Connector + pin polarity (multimeter on the header).

## Sources
- Product: https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28
- Docs: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28
- LCD SPI pins (GC9A01): https://github.com/Bodmer/TFT_eSPI/discussions/3283
- Battery (1.25 mm PicoBlade): https://thepihut.com/products/500mah-3-7v-lipo-battery-1-25mm-picoblade-connector
- Battery (802035 MX1.25): https://www.aliexpress.com/item/1005004361874420.html
