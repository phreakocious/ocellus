#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <OneButton.h>
#include <string>
#include <algorithm>   // std::sort -- age quantiles on the audio-debug screen
#include <LittleFS.h>
#include "config.h"
#include "config_store.h"
#include "protocol.h"
#include "anim_select.h"
#include "carousel.h"
#include "animations.h"
#include "matrix_name.h"
#include "bounce_splash.h"
#include "fb_roll.h"      // torus-scroll the frame for the EV_WANDER_OFF rare event
#include "dvd_logo.h"
#include "toaster_sprites.h"
#include "yinyang.h"
#include "palette.h"
#include "audio.h"
#include "imu.h"
#include "touch.h"
#include "battery.h"
#include "fluid.h"
#include "slide_proto.h"
#include "slide_store.h"
#include "gif_store.h"
#include <AnimatedGIF.h>
#include "treatcat.h"
#include "greetz.h"
#include "vga_font.h"
#if OCELLUS_AUDIO
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif
#include "driver/ledc.h"   // backlight PWM on RTC8M (survives light sleep) -- see backlightBegin()
#include "esp_system.h"      // esp_reset_reason() --- boot diagnostics for the wake-from-sleep reboot
#include "esp_sleep.h"       // esp_sleep_get_wakeup_cause()
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32
#include "driver/rtc_io.h"   // ext0 deep-sleep wakeup needs RTC-pad pull config (S3/classic path)
#endif

Config gConfig;
void applyConfig();
void resetFluid();
void renderFluid(uint32_t now);
void renderSensorDebug(uint32_t now);
static void backlightSet(uint8_t v);   // audio-independent (Slideshow + onAnimEnter need it regardless of OCELLUS_AUDIO)
static uint8_t effectiveBrightness();  // gConfig.brightness, capped while the battery is LOW
static BatteryMonitor gBatt;           // fed in loop(); read by pickEyeMood/effectiveBrightness
static int gBatSimMv = 0;              // {"cmd":"batsim","mv":N} override for bench testing; 0 = real ADC
// USB presence lives in BatteryMonitor (gBatt.usbPowered()) -- by charge-held voltage alone; see
// battery.cpp for why every GPIO route is dead on this board (GPIO44 attempt measured always-high
// 2026-07-16). Boards without a divider read no-cell -> inert -> false; sleepMin=0 is their tool.

void renderSlideshow(uint32_t now);
static void greetzOnEnter();           // defined near renderGreetz; onAnimEnter calls it on entry
static void gifRelease();              // defined near renderGif; onAnimEnter frees the decoder on exit
static void carouselOverlay();          // defined above loop(); draws the carousel name strip
#if OCELLUS_AUDIO
void resetBloom();
void renderAudioDebug(uint32_t now);
void renderWaterfall(uint32_t now);
void renderEcho(uint32_t now);
void ensureRadio(bool on);
bool handleTapCmd(const std::string& line);
void drainTap();
#else
static inline void ensureRadio(bool) {}       // no stream to receive -> no radio to manage
static constexpr bool radioOn = false;        // light sleep (loop) is never radio-gated
static constexpr uint32_t gAudioRxCount = 0;  // parked at 0 so profTick's pkt/s field keeps its shape
#endif

// Radio + uncapped-fps gate: the audio renderers (38..41) and the audio-debug/waterfall screens
// (43/44), which need the same ESP-NOW stream to measure. The sensor-debug id (42) sits between
// them and must NOT count -- it has no use for the radio.
static inline bool isAudioMode(uint8_t id) {
#if OCELLUS_AUDIO
  return (id >= AUDIO_BASE && id < AUDIO_BASE + AUDIO_COUNT) || id == AUDIO_DEBUG_ID || id == WATERFALL_ID;
#else
  (void)id; return false;
#endif
}

// Last touch gesture the button task saw, surfaced for the sensor-debug screen. Read touch only from
// that task (it owns the shared I2C bus); the render loop just reads these flags.
static volatile int      g_lastGesture   = 0;   // TouchGesture, stored as int for volatile
static volatile uint32_t g_lastGestureMs = 0;

#if defined(BOARD_WAVESHARE_128)
int readBatteryMv() {                         // GPIO1 senses Vbat through a 200k/100k (/3) divider
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(1);
  // x 4187/4092: meter vs ADC on the assembled unit, simultaneous on-USB pair (2026-07-16) --
  // raw read was 95 mV low, consistent with 1% divider-resistor stack-up (~2% ratio error at 4.2 V).
  // Divider tolerance is PER BOARD: the USB-threshold pair check (meter + {"cmd":"bat"}) is
  // MANDATORY per shipped unit — thresholds in battery.h carry only ~20–50 mV margin.
  return (int)((sum / 8) * 3 * 4187 / 4092);
}
#else
int readBatteryMv() { return 0; }             // no battery-sense divider on the devkit/C3 boards
#endif

static std::string g_rxbuf;
static uint32_t gCfgSetMs = 0;   // last config `set` line: opens a render-hold window (see loop) so a
                                 // multi-part save's next lines aren't shredded by the flush/nap RX drops
static volatile int g_pendingAnim = -1;   // pending mode jump (serial "anim" cmd or button task); applied in loop() so onAnimEnter/ensureRadio stay single-threaded
static LittleFsSlideStore gSlideStore;
static SlideUpload gSlideUp;
volatile bool gSlideUploading = false;   // true while a slide upload is mid-flight (loop skips render)
volatile bool gSlidesDirty = true;       // set when slide set changes; renderSlideshow re-scans (Task 6)
static uint32_t gSlideRxMs = 0;          // last slide-command timestamp (upload watchdog)
// GIF player (id GIF_ID). Same shape as the slide plumbing above, with one difference that drives
// everything: clips are keyed by NAME, not index, because `flash.py --gifs SET` writes a whole set
// straight to LittleFS via uploadfs and those files have human names. See gif_store.h.
static LittleFsGifStore gGifStore;
static GifUpload gGifUp;
volatile bool gGifUploading = false;     // true while a gif upload is mid-flight (loop skips render)
volatile bool gGifsDirty    = true;      // clip set changed (or first entry); renderGif re-scans
static uint32_t gGifRxMs = 0;            // last gif-command timestamp (upload watchdog)
// Playback state -- up here for the same reason as the slideshow's: onAnimEnter touches it.
static AnimatedGIF* gGif = nullptr;      // allocated on mode entry, freed on exit -- MEASURED 5,008 B
                                         // on the board, not the ~25KB the spec estimated
static GifMeta  gGifList[GIF_MAX];
static int      gGifIdx = 0, gGifCount = 0;
static bool     gGifOpen = false;        // a clip is currently open on the decoder
static uint32_t gGifClipStartMs = 0;     // when the current clip started (hold timer)
static uint32_t gGifNextFrameMs = 0;     // honor the GIF's own per-frame delay
static int      gGifLoops = 0;           // completed loops of the current clip

// Slideshow (id SLIDESHOW_ID) playback state -- declared up here (not beside renderSlideshow, below)
// because onAnimEnter (well above renderSlideshow in the file) resets gSlideIdx on mode entry.
static int      gSlideIdx = 0, gSlideCount = 0;
static uint32_t gSlideShownMs = 0;
static int      gSlidePhase = 0;    // 0 STEADY, 1 FADE-OUT, 2 FADE-IN
static int      gSlideStep = 0;
static const int SLIDE_FADE_STEPS = 8;
void triggerRareEvent(int which);   // defined after the EyeEvent enum; bench trigger below

// --- Animation carousel (spec 2026-08-01) -------------------------------------------------
// Waveshare-only: it is the only board with a touch panel. The S3-Zero's encoder already does
// this job with stepFavorite().
static Carousel gCarousel;
static volatile bool gCarouselOpen  = false;   // read by buttonReadTask to suppress the legacy swipe handlers
static volatile bool gCarouselReq = false;   // swipe-up latch, set in the button task, drained in loop()
static uint32_t gCarouselIdleMs = 0;      // millis of the last MOVEMENT (down or coasting); the strip hides 2s after it goes still
static uint32_t gCarouselDownMs = 0;      // when the CURRENT continuous finger-down began
static uint32_t gCarouselTickMs = 0;      // for the dt handed to Carousel::tick
static bool     gCarouselWasDown = false; // edge-detect finger-up so release() fires exactly once
static uint8_t  gCarouselApplied = 0xFF;  // last id THIS carousel applied; compared instead of currentAnimId, which other writers (button, serial) also move
static bool     gCarouselMoved = false;   // has the user actually scrubbed since opening?
static const uint32_t CAROUSEL_HIDE_MS = 2000;
static const uint32_t CAROUSEL_STUCK_MS = 30000;  // a single touch held this long is a wedged bus, not a gesture

void pollConfigSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      if (g_rxbuf.find("\"eyeevent\"") != std::string::npos) {   // {"cmd":"eyeevent","ev":"wander|drift|microsleep"} -- bench trigger (eye modes only)
        int which = g_rxbuf.find("drift") != std::string::npos ? 1
                  : g_rxbuf.find("micro") != std::string::npos ? 2 : 0;
        triggerRareEvent(which);
        Serial.printf("{\"type\":\"eyeevent\",\"ev\":%d}\n", which);
        g_rxbuf.clear(); continue;
      }
      if (g_rxbuf.find("\"bat\"") != std::string::npos) {   // {"cmd":"bat"} -> battery millivolts
        Serial.printf("{\"type\":\"bat\",\"mv\":%d,\"usb\":%s}\n", readBatteryMv(), gBatt.usbPowered() ? "true" : "false");
        g_rxbuf.clear(); continue;
      }
      {   // {"cmd":"pet"} / {"cmd":"petsim","full":N,"en":N} -> read or force the pet stats
        char petOut[128];
        if (treatcatPetCmd(g_rxbuf.c_str(), petOut, sizeof petOut)) {
          Serial.println(petOut); g_rxbuf.clear(); continue;
        }
      }
      if (g_rxbuf.find("\"batsim\"") != std::string::npos) {   // {"cmd":"batsim","mv":3400} -> feed a fake mV (0 = real ADC)
        size_t p = g_rxbuf.find("\"mv\"");
        gBatSimMv = (p != std::string::npos) ? atoi(g_rxbuf.c_str() + g_rxbuf.find(':', p) + 1) : 0;
        Serial.printf("{\"type\":\"batsim\",\"mv\":%d}\n", gBatSimMv);
        g_rxbuf.clear(); continue;
      }
#if OCELLUS_AUDIO
      if (handleTapCmd(g_rxbuf)) { g_rxbuf.clear(); continue; }   // {"cmd":"tap",...} -- device state, lives beside "bat", not in protocol.cpp
#endif
      if (isGifCmd(g_rxbuf)) {
        bool isUpload = false, changedGifs = false;
        std::string resp = handleGifLine(g_rxbuf, gGifStore, gGifUp, isUpload, changedGifs);
        gGifUploading = isUpload;
        gGifRxMs = millis();
        if (changedGifs) gGifsDirty = true;
        Serial.println(resp.c_str());
        g_rxbuf.clear();
        continue;
      }
      if (isSlideCmd(g_rxbuf)) {
        bool isUpload = false, changedSlides = false;
        std::string resp = handleSlideLine(g_rxbuf, gSlideStore, gSlideUp, isUpload, changedSlides);
        gSlideUploading = isUpload;
        gSlideRxMs = millis();
        if (changedSlides) gSlidesDirty = true;
        Serial.println(resp.c_str());
        g_rxbuf.clear();
        continue;
      }
      // Even a corrupted set line usually keeps its head intact (drops hit the tail), so the
      // window opens for the retry too -- which is the attempt the hold exists to protect.
      if (g_rxbuf.find("\"set\"") != std::string::npos) gCfgSetMs = millis();
      bool changed = false; int animSel = -1;
      std::string resp = handleLine(g_rxbuf, gConfig, changed, &animSel);
      if (changed) saveConfig(gConfig);
      if (changed) applyConfig();
      if (animSel >= 0) g_pendingAnim = animSel;   // applied in loop() where currentAnimId/onAnimEnter are in scope
      Serial.println(resp.c_str());
      g_rxbuf.clear();
    } else if (ch != '\r') {
      if (g_rxbuf.size() < 2048) g_rxbuf += ch;  // cap: drop overlong garbage from a misbehaving host, resync at next '\n'
      else g_rxbuf.clear();
    }
  }
}

// --- HARDWARE PINS ---
#if defined(BOARD_WAVESHARE_128)   // Waveshare ESP32-S3-Touch-LCD-1.28 (primary target)
#define TFT_MOSI 11
#define TFT_SCLK 10
#define TFT_CS    9
#define TFT_DC    8
#define TFT_RST  14      // ponytail: 14 vs 12 is the one ambiguous LCD pin; if the panel stays dark, try 12
#define TFT_BLK   2      // real PWM backlight
#define BUTTON_PIN 0     // BOOT button (strapping pin); no dedicated user button on this board
// I2C (CST816 touch + QMI8658 IMU): SDA=6 SCL=7 | battery ADC=1 | UART0 TX=43 RX=44 -> CH343 -> USB-C
#elif defined(BOARD_S3_ZERO)       // Waveshare ESP32-S3-Zero, soldered into the Sensory Bridge console
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  13
#define TFT_BLK   8
#define BUTTON_PIN 4     // the EC11's integral push switch IS the button (no discrete one on this board)
#define ENC_A_PIN  5
#define ENC_B_PIN  3     // 3, not 6: GPIO6 read high even shorted to GND on the bench unit -- dead pad.
                         // GPIO3 is a strapping pin (JTAG source select), but that only binds when the
                         // STRAP_JTAG_SEL eFuse is burned, which it isn't -- so it's a plain GPIO here.
// ENC_A is GPIO5, which is also the Waveshare board's CST816S touch-INT pin. That symbol is scoped to
// BOARD_WAVESHARE_128 in touch.h precisely so it can't be referenced here (see the note there).
// All on the castellated edge (5V/GND/3V3 + GPIO1-13 + 43/44). GPIO3 (strapping) and GPIO21
// (onboard WS2812) are left alone. No touch controller, no IMU, no battery divider on this board.
#else                              // S3 DevKitC bench / legacy C3
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   6
#define TFT_DC   5
#define TFT_RST  7
#define TFT_BLK  16
#define BUTTON_PIN 15
#endif

#define SCREEN_RES 240

// --- DISPLAY HARDWARE ---
// DMA bus, not plain Arduino_ESP32SPI: the flush is the frame's floor cost (measured 17.8ms of a
// 27ms frame, rock-steady, in every mode) and the plain bus stages only SPI_MAX_PIXELS_AT_ONCE=32
// pixels per hardware transaction. The DMA variant stages 1024, amortizing the per-transaction
// overhead that put flush ~6ms above the 11.5ms the 115KB canvas physically needs on the wire at 80MHz.
Arduino_DataBus *bus = new Arduino_ESP32SPIDMA(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, -1);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);

// Arduino_Canvas::begin() puts the 115KB framebuffer in PSRAM whenever PSRAM exists, and every flush
// streams all of it back out. It only allocates when _framebuffer is still null, so pre-filling it with
// an internal-RAM block is enough to override that -- no library patch. EXPERIMENT: does the PSRAM read
// path explain why flush costs 17.6ms when 115KB at 80MHz is 11.5ms on the wire?
class InternalCanvas : public Arduino_Canvas {
public:
  using Arduino_Canvas::Arduino_Canvas;
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    // Leave _framebuffer null on failure and let the base class fall back to its own ps_malloc: a
    // fragmented internal heap should cost frame rate, not brick the board into the alloc-failed blink.
    _framebuffer = (uint16_t *)heap_caps_malloc((size_t)_width * _height * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_framebuffer) Serial.println("[boot] WARN: internal fb alloc failed, falling back to PSRAM (~40% slower)");
    return Arduino_Canvas::begin(speed);
  }
};
Arduino_Canvas *canvas = new InternalCanvas(SCREEN_RES, SCREEN_RES, gfx);

// --- POWER CONFIG ---
volatile unsigned long lastInteractionTime = 0;
volatile bool gPowerOffReq = false;   // button long-press latches this; loop() calls powerOff() (flag-only, see powerOff())
OneButton button(BUTTON_PIN, true);

// Light sleep masks GPIO interrupts, so the S3-Zero's encoder would lose any detent turned during the
// ~14ms frame-gap nap (the button is immune only because a press is a level, still held when the 10ms
// poll resumes). That board is mains-powered in a console, so the ~19% saving buys it nothing -- it
// just stays awake rather than arming ENC_A/ENC_B as level wake sources.
#if defined(BOARD_S3_ZERO)
constexpr bool kLightSleepOk = false;
#else
constexpr bool kLightSleepOk = true;
#endif

// The console board's panel is physically mounted 90 degrees round in the chassis, so every rotation
// the code asks for is relative to that mount. 1 vs 3 is a bring-up coin-flip -- if the image comes up
// upside-down on the bench, this is the one value to change.
#if defined(BOARD_S3_ZERO)
constexpr uint8_t kBaseRotation = 3;   // 3, not 1: confirmed on the bench -- 1 came up vertically flipped
#else
constexpr uint8_t kBaseRotation = 0;
#endif

// Rotation currently applied to the panel (0 or 2). Defined here, next to the only writer;
// declared in touch.h because it is the touch layer's normalization input. It lived in
// treatcat.cpp until 2026-08-01, which was an accident of which feature needed it first.
uint8_t gAppliedRot = 0;

// Every setRotation goes through here. Screens that lock an orientation (the debug screens, Fluid) are
// asking for one relative to the mount, not to the panel's native axes -- so the offset has to be added
// at every site, not just in applyConfig().
static inline void setPanelRotation(uint8_t r) { gAppliedRot = r; gfx->setRotation((r + kBaseRotation) & 3); }

// --- RTC MEMORY & MODES ---
RTC_DATA_ATTR uint8_t currentAnimId = 0;   // flat registry id 0-40 (incl. debug); RTC so `resume` survives deep-sleep

// The base a queued anim change should step from: the outstanding pending value if there is one,
// else the live id. loop() writes currentAnimId before clearing g_pendingAnim, so a negative pending
// means currentAnimId is fresh. Without this, two inputs inside one render+flush window clobber each
// other instead of composing (turn-then-click would drop the turn).
static inline uint8_t animBase() { int p = g_pendingAnim; return (p >= 0) ? (uint8_t)p : currentAnimId; }

// --- PHYSICS & ANIMATION VARIABLES ---
float curX = 120, curY = 120, tarX = 120, tarY = 120;
float snapSpeed = 2.0;
bool isWideMovement = true;

enum EyeState { MOVING, PAUSING };
EyeState eyeState = PAUSING;
uint32_t stateEndTime = 0;

// Scripted "special" behaviors that briefly interrupt the wander machine (handled in renderEye).
// The last three are SUPER-rare (own gate, ~4-10 min apart -- see maybeStartRareEvent).
enum EyeEvent { EV_NONE, EV_ROLL, EV_SCAN, EV_DROWSY, EV_SQUINT, EV_DOUBLE_TAKE,
                EV_WANDER_OFF, EV_DRIFT, EV_MICROSLEEP };
EyeEvent eyeEvent = EV_NONE;
uint32_t eventStart = 0, nextEventOk = 0;      // nextEventOk = cooldown gate so the rare events stay rare
uint32_t nextRareOk = 0;                        // separate, much longer gate for the three super-rare events
bool sideEye = false;                          // holding a deliberate off-axis stare -> lids narrow
static const uint32_t ROLL_MS = 1100;          // one full pupil revolution
static const uint32_t SCAN_MS = 720;           // tiny curious pupil-only scan
static const uint32_t DROWSY_MS = 2800;        // downward drift + long blink, not power sleep
static const uint32_t SQUINT_MS = 2000;        // narrow to a slit + inspect something up close
static const uint32_t DOUBLE_TAKE_MS = 640;    // glance away, then whip back wide-eyed
float dtOriginX = 120, dtOriginY = 120;        // double-take: where to snap back to
int dtAwayX = 120, dtAwayY = 120;              // double-take: the distraction it glances at first
// --- super-rare events ---
static const uint32_t WANDER_MS = 1000;        // whole eye scrolls off one edge, wraps in the opposite
static const uint32_t DRIFT_MS  = 1600;        // iris + pupil scroll behind the lids, land back center
static const uint32_t MICRO_MS  = 4200;        // droop -> bloodshot -> ~3s full sleep -> wake
int rareDirX = 1, rareDirY = 0;                // EV_WANDER_OFF scroll direction (8-way, picked at trigger)
uint8_t driftIrisAng = 0, driftPupilAng = 96;  // EV_DRIFT: independent iris/pupil bearings (256 = 2pi)
uint32_t bloodshotSeed = 0;                    // EV_MICROSLEEP: fixed per-event so veins don't flicker

enum EyeMood { MOOD_CALM, MOOD_CURIOUS, MOOD_SKEPTICAL, MOOD_DROWSY };
EyeMood eyeMood = MOOD_CALM;
bool eyeBlinkRequest = false;                  // punctuation blink requested by the motion state machine
uint32_t pauseStartTime = 0;
uint32_t nextMicroSaccade = 0, microUntil = 0;
int microDx = 0, microDy = 0, scanDir = 1;
bool microUsedThisPause = false;
int glintX = 110, glintY = 110;
bool glintInit = false;

uint32_t nextFrameTime = 0;

volatile bool isJittering = false;
volatile uint32_t jitterEndTime = 0;

// --- SECRET MODES & MATRIX LOGIC ---
struct MatrixColumn { int y, speed; uint32_t lastUpdate; };
MatrixColumn matrixCols[24];
// Matrix effect geometry (text size 2 so woven names read). Glyphs are 6*TS wide / 8*TS tall;
// CW/RH space them so they don't overlap. Boot splash keeps its own inline size-1 rain.
constexpr int MATRIX_TS   = 2;
constexpr int MATRIX_CW   = 6 * MATRIX_TS + 2;   // column spacing (glyph width + 2px gap)
constexpr int MATRIX_RH   = 8 * MATRIX_TS;       // row height / streak spacing
constexpr int MATRIX_COLS = 240 / MATRIX_CW;     // visible columns

int16_t sinLUT[256];
void initSinLUT() { for (int i = 0; i < 256; i++) sinLUT[i] = (int16_t)(sin(i * 2.0 * PI / 256.0) * 127); }
inline int16_t fastSin(int16_t angle) { return sinLUT[angle & 0xFF]; }
inline int16_t fastCos(int16_t angle) { return sinLUT[(angle + 64) & 0xFF]; }

// Active-palette color ramp: 256 RGB565 entries indexed by pColor's phase byte.
// Rebuilt only on a palette switch (rotation tick or a config set) --- see refreshPalettes().
uint16_t activePaletteLUT[256];

// SB virtual palette (config sbPalette): the console's FastLED wheel in RGB565, incandescent-
// tinted per its synced filter. gSbActive makes pColor read this wheel by OFFSET ALONE -- no
// millis() time-walk. The walk sweeps the whole palette ~1x/s, which is the point everywhere
// else, but here the offset IS the console's hue and it must hold still to be seen.
#if OCELLUS_AUDIO
static uint16_t sbWheelLUT[256];    // 512B; only reachable through gSbActive, which needs the audio modes
static int      gSbWheelMix = -1;   // incandescent mix the LUT was built at; -1 = never built (ensureSbWheel only)
#endif
static bool     gSbActive   = false;   // set per frame in loop(): sbPalette && audio mode 31..34
static bool     gAudioPalSlow = false; // set per frame in loop(): audio mode 31..34 without the SB wheel

std::vector<uint8_t> gRotList = {0};   // enabled palettes in rotation (ids); {0}=Rainbow
size_t   gRotIndex = 0;
uint32_t gLastPaletteRotate = 0;
uint32_t gLastCycle = 0;          // auto-cycle favorites timer (config.cycleSec); manual picks reset it
uint16_t prevLUT[256], nextLUT[256];   // crossfade scratch: outgoing / incoming palette ramps
uint32_t gPalFadeStart = 0;            // millis() at fade start; 0 = not fading
const uint32_t PAL_FADE_MS = 1000;     // rotation crossfade duration

// Phase byte matches the legacy fastSin(t+offset) & 0xFF, so Rainbow is byte-identical.
uint16_t pColor(uint32_t speed, int offset) {
#if OCELLUS_AUDIO
  if (gSbActive) return sbWheelLUT[(uint8_t)offset];   // SB wheel: offset IS the hue (see above)
#endif
  if (!speed) speed = 1;   // Weave/Mosaic pass v/8+5 which can be 0 -> millis()/0 div-by-zero panic (reboots S3)
  if (gAudioPalSlow) speed *= 8;   // legacy palette in audio modes: ~1 sweep/s read as strobing, 1/8 rate
  return activePaletteLUT[(uint8_t)(millis() / speed + offset)];
}

// Spatial hue spread, compressed 2x under the SB wheel: our full-wheel spreads would put every
// hue in every frame and bury the anchor color the feature exists to show. Started at >>2
// (console-parity quarter wheel, its (i>>1) over 128 LEDs) -- read "a bit mono" on the LCD in
// the hardware pass (2026-07-13), esp. with the console's AUTO_COLOR_SHIFT off (static anchor).
static inline int hspread(int x) { return gSbActive ? x >> 1 : x; }

// Cheap PRNG for VISUAL noise only (never for anything that wants real entropy). Arduino's random()
// goes to esp_random(), a hardware RNG register read, and the Static eye theme calls it 1200x a frame
// (400 pixels x 3): that one line measured ~25ms of a 31ms frame -- the single most expensive thing in
// the whole animation set, and the only reason a mode couldn't hit its own frame cap. Nobody can tell
// hardware-random static from xorshift static.
static inline uint32_t frand() {
  static uint32_t s = 0x9E3779B9;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}
static inline uint16_t dim565(uint16_t c, uint8_t num, uint8_t den);
// Hue-preserving brightness floor: scale an RGB565 up so its brightest channel reaches at least `minTop`
// (0..31). Keeps bass-driven bloom circles visible on palette stops that map to near-black.
static inline uint16_t lift565(uint16_t c, uint8_t minTop) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  uint8_t top = max((uint8_t)r, max((uint8_t)(g >> 1), b));   // brightest channel on the 5-bit scale
  if (top >= minTop) return c;
  if (top == 0) { uint8_t f = minTop; return (uint16_t)((f << 11) | ((f << 1) << 5) | f); } // pure black -> neutral grey floor
  return (uint16_t)(((r * minTop / top) << 11) | ((g * minTop / top) << 5) | (b * minTop / top)); // top<minTop bounds g<=2*top so no field overflow
}

// Linear per-channel blend of two RGB565 colors: t=0 -> a, t=255 -> b.
static inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (br - ar) * t / 255, g = ag + (bg - ag) * t / 255, bl = ab + (bb - ab) * t / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Build the LUT for the currently-selected rotation entry (into `out`, default the live table).
void buildActivePaletteLUT(uint16_t* out = activePaletteLUT) {
  uint8_t id = gRotList.empty() ? 0 : gRotList[gRotIndex % gRotList.size()];
  std::vector<uint16_t> stops = paletteStops(id, gConfig.customPalettes);
  if (id == 0 || stops.empty()) buildRainbowLUT(out);  // Rainbow or malformed -> Rainbow
  else                          buildPaletteLUT(stops, out);
}

// Begin a crossfade to the palette at the (already-advanced) gRotIndex.
void startPaletteFade() {
  for (int i = 0; i < 256; i++) prevLUT[i] = activePaletteLUT[i];  // snapshot what's showing
  buildActivePaletteLUT(nextLUT);                                   // build where we're going
  gPalFadeStart = millis();
}

// Advance the crossfade one frame; rewrites activePaletteLUT. No-op when not fading.
void updatePaletteFade(uint32_t now) {
  if (!gPalFadeStart) return;
  uint32_t e = now - gPalFadeStart;
  if (e >= PAL_FADE_MS) { for (int i = 0; i < 256; i++) activePaletteLUT[i] = nextLUT[i]; gPalFadeStart = 0; return; }
  uint8_t t = (uint8_t)(e * 255 / PAL_FADE_MS);
  for (int i = 0; i < 256; i++) activePaletteLUT[i] = blend565(prevLUT[i], nextLUT[i], t);
}

// Recompute the rotation list from config and rebuild the LUT. Boot + after any set.
void refreshPalettes() {
  gRotList = activeRotationList(gConfig.palettesEnabled, gConfig.customPalettes);
  gRotIndex = 0;
  gLastPaletteRotate = millis();
  gPalFadeStart = 0;              // config/boot change applies instantly, not as a fade
  buildActivePaletteLUT();
}

void initMatrixColumn(int i) { matrixCols[i].y = random(-300, 0); matrixCols[i].speed = random(30, 120); matrixCols[i].lastUpdate = millis(); }

// Matrix effect: NAME_COLS of the 24 rain columns spell gConfig.name instead of random glyphs
// (woven, fixed letters at the leading run). They roam: a name column hands its slot to a fresh
// column when it recycles, so the count stays pinned while positions drift. See matrixNameGlyph().
constexpr int NAME_COLS = 3;
bool matrixNameCol[24] = {};
static void pickMatrixNameCol(int except) {   // promote one random non-name column to a name column
  int c; do { c = random(MATRIX_COLS); } while (matrixNameCol[c] || c == except);
  matrixNameCol[c] = true;
}
void assignMatrixNameCols() {                  // reset to exactly NAME_COLS distinct name columns
  for (int i = 0; i < 24; i++) matrixNameCol[i] = false;
  for (int k = 0; k < NAME_COLS; k++) pickMatrixNameCol(-1);
}

// Name letters "resist" being matrix'd: a few name-letter positions scramble to random glyphs,
// then snap back to the true letter as the glitch hands off to another position. State is keyed by
// name-letter index (shared across the name columns) so a resisting letter flickers everywhere.
struct NameGlitch { int idx; uint32_t until, nextCh; char ch; };
static NameGlitch nameGlitch[3] = {};
static bool nameGlitchOn(int idx, char& out) {   // is name-letter idx currently glitching?
  for (auto& g : nameGlitch) if (g.until && g.idx == idx) { out = g.ch; return true; }
  return false;
}
static void updateNameGlitch(uint32_t now, int nameLen) {
  int active = nameLen >= 4 ? 3 : (nameLen >= 2 ? nameLen - 1 : nameLen);   // keep >=1 letter stable
  for (int s = 0; s < 3; s++) {
    NameGlitch& g = nameGlitch[s];
    if (s >= active) { g.until = 0; continue; }
    if (!g.until || now >= g.until) {              // settled (or first frame) -> glitch a new letter
      int idx, dup; do { idx = random(nameLen); dup = 0;
        for (int k = 0; k < 3; k++) if (k != s && nameGlitch[k].until && nameGlitch[k].idx == idx) dup = 1;
      } while (dup);
      g.idx = idx; g.until = now + random(250, 400); g.nextCh = 0;
    }
    if (now >= g.nextCh) { g.ch = (char)random(33, 126); g.nextCh = now + 55; }   // rescramble ~18Hz
  }
}
static bool g_pipesReset = false;   // Pipes accumulates on a non-cleared canvas; force a clean restart when it becomes active
#if OCELLUS_AUDIO
// Set on entry to the waterfall (id 44; used by onAnimEnter, below), drained (and history cleared)
// at the top of renderWaterfall (defined much later, alongside gWfPend). Both sides run in loop-task
// context only (onAnimEnter and renderWaterfall are never called from the WiFi task), so this is a
// plain bool, not volatile -- declared up here, not next to gWfPend, because onAnimEnter needs it in
// scope first.
static bool gWfEnter = false;
static bool gEchoEnter = false;   // same contract as gWfEnter, for Echo (id 41 = ECHO_ID)

// Drawdown + demo state (see docs/superpowers/specs/2026-07-14-audio-drawdown-demo-design.md).
// Declared up here (like gWfEnter) because onAnimEnter resets them. gDemoSnap/gDemo* are filled
// once per idle frame in loop() and read by the audio idle branches.
static DrawdownState gDrawdown = { true, 0, 0, BACKOFF_BASE_MS };
static SynthState    gSynthState = {};      // seeded on first onAnimEnter into an audio mode
static SbStreamMags  gDemoSnap;             // synthetic spectrum for the idle branches
static bool          gDemoBeat, gDemoSnare, gDemoSpark;
#endif

static uint32_t eyePersonalityHash(uint8_t theme) {
  uint32_t h = 2166136261u;   // FNV-1a; stable per unit/theme, no persisted config needed
  for (char ch : gConfig.name) { h ^= (uint8_t)ch; h *= 16777619u; }
  h ^= (uint32_t)theme + 0x9E3779B9u; h *= 16777619u;
  h ^= ((uint32_t)gConfig.irisColor << 16) | gConfig.skinColor;
  return h;
}

static EyeMood pickEyeMood(uint8_t theme) {
  if (gBatt.state() != BATT_NORMAL) return MOOD_DROWSY;   // low battery: sleepy, whatever the personality says
  uint32_t h = eyePersonalityHash(theme);
  switch (theme) {
    case 1: case 10: case 12: return (h & 1) ? MOOD_CURIOUS : MOOD_SKEPTICAL; // glitchy/noisy eyes feel alert
    case 3: case 7:           return (h & 3) ? MOOD_CALM : MOOD_DROWSY;       // breathe/void can linger
    case 8:                   return (h & 1) ? MOOD_SKEPTICAL : MOOD_CALM;    // box eye stays a little mechanical
    default: {
      int r = h % 100;
      if (r < 42) return MOOD_CALM;
      if (r < 68) return MOOD_CURIOUS;
      if (r < 86) return MOOD_SKEPTICAL;
      return MOOD_DROWSY;
    }
  }
}

static void moodPauseRange(uint16_t& lo, uint16_t& hi) {
  switch (eyeMood) {
    case MOOD_CURIOUS:   lo = 320;  hi = 1050; break;
    case MOOD_SKEPTICAL: lo = 900;  hi = 2300; break;
    case MOOD_DROWSY:    lo = 1300; hi = 3300; break;
    default:             lo = 550;  hi = 1700; break;
  }
}

static uint32_t moodPauseMs() {
  uint16_t lo, hi; moodPauseRange(lo, hi);
  return (uint32_t)random(lo, hi);
}

static uint32_t moodSideEyePauseMs() {
  switch (eyeMood) {
    case MOOD_SKEPTICAL: return (uint32_t)random(2600, 4500);
    case MOOD_DROWSY:    return (uint32_t)random(2400, 3800);
    default:             return (uint32_t)random(1900, 3300);
  }
}

static int moodSideEyeChance() {
  switch (eyeMood) {
    case MOOD_SKEPTICAL: return 18;
    case MOOD_CURIOUS:   return 9;
    case MOOD_DROWSY:    return 4;
    default:             return 7;
  }
}

static int moodWideMoveChance() {
  switch (eyeMood) {
    case MOOD_CURIOUS:   return 72;
    case MOOD_SKEPTICAL: return 48;
    case MOOD_DROWSY:    return 36;
    default:             return 58;
  }
}

static int moodSpeedPct() {
  switch (eyeMood) {
    case MOOD_CURIOUS:   return 112;
    case MOOD_SKEPTICAL: return 122;
    case MOOD_DROWSY:    return 68;
    default:             return 94;
  }
}

static int moodLandingBlinkChance() {
  switch (eyeMood) {
    case MOOD_SKEPTICAL: return 18;
    case MOOD_DROWSY:    return 12;
    default:             return 8;
  }
}

static uint32_t moodEventCooldownMs() {
  switch (eyeMood) {
    case MOOD_CURIOUS:   return (uint32_t)random(22000, 52000);
    case MOOD_DROWSY:    return (uint32_t)random(30000, 70000);
    default:             return (uint32_t)random(28000, 76000);
  }
}

static void finishEyeEvent(uint32_t now, uint32_t dwellMs) {
  eyeEvent = EV_NONE;
  eyeState = PAUSING;
  stateEndTime = now + dwellMs;
  pauseStartTime = now;
  nextMicroSaccade = now + random(1800, 3600);
  microUntil = 0; microDx = microDy = 0;
  microUsedThisPause = false;
}

static bool maybeStartEyeEvent(uint32_t now) {
  if (now <= nextEventOk) return false;
  int roll, scan, drowsy, squint, dtake;         // weights out of 1000; remainder = no event
  switch (eyeMood) {
    case MOOD_CURIOUS:   roll = 16; scan = 70; drowsy = 8;  squint = 40; dtake = 30; break;
    case MOOD_SKEPTICAL: roll = 36; scan = 26; drowsy = 10; squint = 44; dtake = 24; break;
    case MOOD_DROWSY:    roll = 8;  scan = 18; drowsy = 70; squint = 6;  dtake = 4;  break;
    default:             roll = 22; scan = 36; drowsy = 22; squint = 20; dtake = 16; break;
  }
  int r = random(1000), acc = roll;
  if (r < acc) {
    eyeEvent = EV_ROLL;
  } else if (r < (acc += scan)) {
    eyeEvent = EV_SCAN;
    scanDir = random(2) ? 1 : -1;
  } else if (r < (acc += drowsy)) {
    eyeEvent = EV_DROWSY;
  } else if (r < (acc += squint)) {
    eyeEvent = EV_SQUINT;
  } else if (r < (acc += dtake)) {
    eyeEvent = EV_DOUBLE_TAKE;
    dtOriginX = curX; dtOriginY = curY;          // snap-back point = where it's parked now
    int adx = (random(2) ? 1 : -1) * random(28, 58), ady = random(-26, 27);
    dtAwayX = constrain((int)curX + adx, 40, 200);
    dtAwayY = constrain((int)curY + ady, 40, 200);
  } else {
    return false;
  }
  eventStart = now;
  nextEventOk = now + moodEventCooldownMs();
  return true;
}

// Set up a super-rare event and its per-event params. Shared by the rare gate and the
// {"cmd":"eyeevent"} bench trigger.
static void startRareEvent(EyeEvent ev, uint32_t now) {
  eyeEvent = ev;
  if (ev == EV_WANDER_OFF)                        // whole eye scrolls off + wraps in from the opposite
    do { rareDirX = random(-1, 2); rareDirY = random(-1, 2); } while (rareDirX == 0 && rareDirY == 0);
  else if (ev == EV_DRIFT) {                      // iris + pupil drift behind the lids on separate bearings
    driftIrisAng  = (uint8_t)random(256);
    driftPupilAng = (uint8_t)(driftIrisAng + random(64, 192));   // clearly different bearing
  } else if (ev == EV_MICROSLEEP)                 // droop -> bloodshot -> ~3s sleep -> wake
    bloodshotSeed = (uint32_t)random(0x7FFFFFFF) | 1u;
  eventStart = now;
}

// {"cmd":"eyeevent"} bench trigger (see pollConfigSerial): 0/1/2 = wander / drift / microsleep.
void triggerRareEvent(int which) {
  startRareEvent(which == 1 ? EV_DRIFT : which == 2 ? EV_MICROSLEEP : EV_WANDER_OFF, millis());
}

// Super-rare gate: independent of the normal fidget pacing. Fires one of the three big events
// ~4-10 min apart, any mood. Called before maybeStartEyeEvent in the idle branch.
static bool maybeStartRareEvent(uint32_t now) {
  if (now <= nextRareOk) return false;
  int r = random(100);
  startRareEvent(r < 30 ? EV_WANDER_OFF : r < 65 ? EV_DRIFT : EV_MICROSLEEP, now);
  nextRareOk  = now + random(240000, 600000);    // next one 4-10 min out
  nextEventOk = now + moodEventCooldownMs();      // don't let a normal fidget stack right after
  return true;
}

static void chooseMoodExpression(int& topTar, int& botTar) {
  int r = random(100);
  switch (eyeMood) {
    case MOOD_CURIOUS:
      if      (r < 40) { topTar = 40; botTar = 12; }
      else if (r < 75) { topTar = 26; botTar = 10; }
      else if (r < 85) { topTar = 58; botTar = 22; }
      else             { topTar = 46; botTar = 38; }
      break;
    case MOOD_SKEPTICAL:
      if      (r < 35) { topTar = 44; botTar = 16; }
      else if (r < 45) { topTar = 28; botTar = 12; }
      else if (r < 65) { topTar = 62; botTar = 26; }
      else             { topTar = 52; botTar = 42; }
      break;
    case MOOD_DROWSY:
      if      (r < 25) { topTar = 46; botTar = 16; }
      else if (r < 33) { topTar = 30; botTar = 12; }
      else if (r < 85) { topTar = 66; botTar = 30; }
      else             { topTar = 52; botTar = 38; }
      break;
    default:
      if      (r < 55) { topTar = 40; botTar = 12; }
      else if (r < 70) { topTar = 26; botTar = 12; }
      else if (r < 85) { topTar = 62; botTar = 26; }
      else             { topTar = 46; botTar = 40; }
      break;
  }
}

static void resetEyePersonality(uint8_t theme) {
  uint32_t now = millis();
  eyeMood = pickEyeMood(theme);
  eyeEvent = EV_NONE; sideEye = false; eyeBlinkRequest = false;
  pauseStartTime = now; stateEndTime = now + 500;
  nextEventOk = now + random(14000, 30000);
  nextRareOk = now + random(240000, 600000);   // first super-rare event 4-10 min into an eye mode
  nextMicroSaccade = now + random(1800, 3600);
  microUntil = 0; microDx = microDy = 0;
  microUsedThisPause = false;
  scanDir = random(2) ? 1 : -1;
  glintInit = false;
}

static int easeInt(int cur, int tar, int div) {
  int d = tar - cur;
  return cur + (d / div != 0 ? d / div : (d > 0) - (d < 0));
}

static void applyMicroSaccade(uint32_t now, int& puX, int& puY) {
  if (eyeEvent != EV_NONE || eyeState != PAUSING || isJittering || now - pauseStartTime < 1400) {
    if (microUntil && now >= microUntil) { microUntil = 0; microDx = microDy = 0; }
    return;
  }
  if (nextMicroSaccade == 0) nextMicroSaccade = now + random(1800, 3600);
  if (microUntil) {
    if (now < microUntil) { puX += microDx; puY += microDy; return; }
    microUntil = 0; microDx = microDy = 0;
    nextMicroSaccade = now + random(1800, 3600);
  }
  if (!microUsedThisPause && now >= nextMicroSaccade && stateEndTime > now + 260) {
    microDx = random(-2, 3); microDy = random(-1, 2);
    if (microDx == 0 && microDy == 0) microDx = random(2) ? 1 : -1;
    microUntil = now + random(35, 65);
    microUsedThisPause = true;
  }
}

static void applyScanOffset(uint32_t now, int& puX, int& puY) {
  if (eyeEvent != EV_SCAN) return;
  uint32_t p = now - eventStart;
  int sx = 0, sy = 0;
  if      (p < 160) { sx = 7;  sy = -2; }
  else if (p < 320) { sx = -6; sy = 1;  }
  else if (p < 500) { sx = 5;  sy = 4;  }
  else              { sx = 0;  sy = 0;  }
  puX += scanDir * sx; puY += sy;
}

static void applySquintInspect(uint32_t now, int& puX, int& puY) {   // tiny darting focus while squinting
  if (eyeEvent != EV_SQUINT) return;
  static const int8_t jx[4] = {1, -1, 2, -1};
  static const int8_t jy[4] = {-1, 1, 0, 1};
  int step = (int)((now - eventStart) / 130) & 3;   // shift focus every ~130ms
  puX += jx[step]; puY += jy[step];
}

static int moodPupilAim(int heldTarget) {
  int aim = heldTarget;
  bool largeCuriousScan = eyeMood == MOOD_CURIOUS &&
                          (eyeEvent == EV_SCAN || (eyeState == MOVING && isWideMovement && !sideEye));
  bool skepticalFocus = eyeMood == MOOD_SKEPTICAL && (sideEye || eyeState == PAUSING || eyeEvent == EV_SCAN);

  if (largeCuriousScan) aim = max(aim, 29);      // wide pupils while taking in new information
  else if (eyeMood == MOOD_CURIOUS) aim += 2;

  if (skepticalFocus) aim = min(aim, 15);        // hard-focus squint: smaller, sharper pupil
  else if (eyeMood == MOOD_SKEPTICAL) aim -= 3;

  if (eyeMood == MOOD_DROWSY) aim -= 3;
  if (eyeEvent == EV_DROWSY) aim -= 2;
  if (eyeEvent == EV_SQUINT) aim = min(aim, 12); // pinhole: constrict to inspect up close
  if (eyeEvent == EV_DOUBLE_TAKE) aim += 6;      // startled dilation on the whip-back
  if (isJittering) aim += 5;                     // startle dilation still punches through

  if (aim < 11) aim = 11;
  if (aim > 30) aim = 30;
  return aim;
}

// Re-init per-animation state when an animation becomes active.
void onAnimEnter(uint8_t id) {
  ensureRadio(isAudioMode(id));
#if OCELLUS_AUDIO
  if (isAudioMode(id)) {                          // reset the duty-cycle timers to entry time so the
    uint32_t t = millis();                        // first listen window runs a full LISTEN_MS
    gDrawdown = DrawdownState{ true, t, t, BACKOFF_BASE_MS };
    if (!gSynthState.rng) gSynthState.rng = esp_random() | 1;   // one-time non-zero seed per boot
  }
#endif
  if (id < EYE_COUNT) resetEyePersonality(id);
  if (id == EYE_COUNT) { for (int i = 0; i < 24; i++) initMatrixColumn(i); assignMatrixNameCols(); }  // Matrix (first effect): fresh columns + name columns
  if (id == PIPES_ID) g_pipesReset = true;                                // clear stale frame from the prior effect
  if (id == FLUID_ID) resetFluid();                                       // fresh liquid on entry
  if (id == TREATCAT_ID) treatcatOnEnter();   // drop a stale tap + any in-progress fortune (keeps the endless treat count)
  if (id == GREETZ_ID) greetzOnEnter();       // fresh shuffle + offset on every entry
#if OCELLUS_AUDIO
  if (id == WATERFALL_ID) gWfEnter = true;                                // fresh history + drained pend column, not a replay of the last visit
  if (id == ECHO_ID) gEchoEnter = true;                                   // fresh ring + drained pend column
#endif
  // The debug screens only lock an orientation because on the IMU board they must not be auto-flipped out
  // from under the gravity arrow. With no IMU there is nothing to fight, so they take the config frame like
  // every other screen -- locking there just renders them sideways against the panel's mount.
  if      (!imuPresent)          setPanelRotation(gConfig.flip ? 2 : 0); // one frame for everything, debug included
  else if (id == DEBUG_ID)       setPanelRotation(0);                    // lock orientation so the gravity arrow reads true
#if OCELLUS_AUDIO
  else if (id == AUDIO_DEBUG_ID || id == WATERFALL_ID) setPanelRotation(2);  // same lock, flipped: rotation 0 reads upside-down on the Waveshare board
#endif
  else if (id == YINYANG_ID)     setPanelRotation(0);                    // lock the frame: auto-flip would jerk the disc mid-spin
#if OCELLUS_AUDIO
  if (id == AUDIO_BASE) resetBloom();                                     // Bloom (first audio id)
#endif
  backlightSet(effectiveBrightness());          // undo any in-progress slideshow fade so the next mode is full-bright
  if (id == SLIDESHOW_ID) { gSlideIdx = 0; gSlidesDirty = true; }   // fresh scan + first-slide load
  if (id == GIF_ID) { gGifIdx = 0; gGifsDirty = true; }             // fresh scan + first-clip open
  else gifRelease();   // leaving the GIF mode: hand the decoder's 5KB back
}
// fill a quad (corners in perimeter order) via BOTH diagonals --- GFX's scanline fillTriangle can drop a 1px seam
// between two triangles sharing one diagonal; the second split covers whatever the first misses.
void fillQuad(Arduino_GFX *g, int16_t x0,int16_t y0, int16_t x1,int16_t y1, int16_t x2,int16_t y2, int16_t x3,int16_t y3, uint16_t c) {
  g->fillTriangle(x0,y0, x1,y1, x2,y2, c); g->fillTriangle(x0,y0, x2,y2, x3,y3, c); // diagonal x0-x2
  g->fillTriangle(x0,y0, x1,y1, x3,y3, c); g->fillTriangle(x1,y1, x2,y2, x3,y3, c); // diagonal x1-x3
}
// --- ROTARY ENCODER (S3-Zero only) ---
// Step through the debug screens (sensor -> audio -> waterfall), wrapping both ways. Sensor debug
// reads the IMU and the touch panel, so it's a dead screen on a board with neither -- those page
// audio <-> waterfall only. Keyed on the runtime probes, not a board macro: the C3 and the bench S3
// have no sensors either. A base outside the debug range means we're entering: land on page 0.
static uint8_t stepDebug(uint8_t base, int delta) {
  uint8_t pages[DEBUG_COUNT];
  int n = 0;
  if (imuPresent || touchPresent) pages[n++] = DEBUG_ID;
#if OCELLUS_AUDIO
  pages[n++] = AUDIO_DEBUG_ID;
  pages[n++] = WATERFALL_ID;
#endif
  // Sensor-less audio-off build: keep the sensor screen anyway (it still shows battery/presence) --
  // a silently dead gesture reads as a broken button.
  if (n == 0) pages[n++] = DEBUG_ID;
  for (int i = 0; i < n; i++)
    if (pages[i] == base) return pages[((i + delta) % n + n) % n];
  return pages[0];
}

// EC11 quadrature on ENC_A/ENC_B; its push switch is BUTTON_PIN and stays OneButton's.
#if defined(BOARD_S3_ZERO)
#include <RotaryEncoder.h>
// B before A: as wired, CW decremented the library position (TODO #18). Swapping at the wiring
// layer (the library's orientation knob) fixes position/direction for EVERY consumer, not just
// encoderPoll -- a delta negation there would leave getPosition()/getDirection() inverted.
static RotaryEncoder encoder(ENC_B_PIN, ENC_A_PIN, RotaryEncoder::LatchMode::FOUR3);  // FOUR3 = standard EC11 detent
static long encLastPos = 0;

// ISR, not a poll: buttonReadTask ticks at 10ms, which drops quadrature edges on a fast twist, and a
// framebuffer flush can hold the CPU for longer still.
// The IRAM_ATTR here is a no-op today, and that's load-bearing, not decorative: the framework sdkconfig
// has CONFIG_ARDUINO_ISR_IRAM unset, so attachInterrupt() allocates this GPIO interrupt WITHOUT
// ESP_INTR_FLAG_IRAM -- meaning the interrupt is simply masked during a flash write (cache disabled)
// rather than dispatched through it. That's why a detent turned during an NVS config save is merely
// dropped instead of panicking. If CONFIG_ARDUINO_ISR_IRAM is ever turned on (or this interrupt is
// hand-allocated with ESP_INTR_FLAG_IRAM), this handler WILL dispatch with the flash cache disabled,
// and it will panic: encoder.tick(), digitalRead(), millis(), and the RotaryEncoder library's KNOBDIR[]
// direction table are all flash-resident, not IRAM. A hand-rolled IRAM-resident state machine is the
// upgrade path if that ever changes.
static void IRAM_ATTR encoderISR() { encoder.tick(); }

static void encoderBegin() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  encoder.tick();                        // resync _oldState to the real idle level: the ctor sampled it
  encLastPos = encoder.getPosition();    // at static-init, before the pull-ups had settled
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderISR, CHANGE);
}

// Drain the detents the ISR accumulated into one mode change. Called from buttonReadTask, so it must
// only set g_pendingAnim -- loop() applies it, keeping onAnimEnter/ensureRadio single-threaded.
static void encoderPoll() {
  long pos = encoder.getPosition();
  if (pos == encLastPos) return;
  int delta = (int)(pos - encLastPos);
  encLastPos = pos;
  // Base the step on animBase() (outstanding pending value if any, else currentAnimId): loop() only
  // refreshes currentAnimId once it drains g_pendingAnim (writing currentAnimId BEFORE clearing
  // g_pendingAnim), and that can be stuck mid-render+flush (~17ms, historically ~46ms) while this task
  // keeps polling every 10ms. Re-deriving from stale currentAnimId across that window would overwrite
  // g_pendingAnim with the same next-step value each poll and swallow every detent but the last.
  uint8_t base = animBase();
  // On a debug screen the knob pages the debug screens instead of stepping favorites -- the text
  // telemetry and the waterfall are what you want to flip between while looking at either. A click
  // still escapes back into the favorites rotation (nextFavorite is % ANIM_COUNT).
  g_pendingAnim = (base >= DEBUG_ID) ? stepDebug(base, delta)
                                     : stepFavorite(gConfig.favoritesMask, base, delta);
  lastInteractionTime = millis();
}
#else
static void encoderBegin() {}
static void encoderPoll() {}
#endif

// separate task, higher prio than loop(): the 20MHz framebuffer flush takes ~50ms/frame and would otherwise starve button polling
void buttonReadTask(void *pvParameters) {
  while (true) {
    button.tick();
    encoderPoll();                             // S3-Zero rotary encoder; no-op elsewhere
    TouchGesture tg = touchPoll();             // Waveshare touch; TOUCH_NONE elsewhere
    if (tg != TOUCH_NONE) { g_lastGesture = (int)tg; g_lastGestureMs = millis(); }  // for the sensor-debug screen
    switch (tg) {
      case TOUCH_SWIPE_UP:
        gCarouselReq = true;                    // flag-only; loop() builds the list and opens it
        lastInteractionTime = millis();
        break;
      case TOUCH_SWIPE_RIGHT:                   // next enabled anim (mirrors singleClick)
        if (gCarouselOpen) break;               // the carousel owns the horizontal axis while it is up:
        g_pendingAnim = nextFavorite(gConfig.favoritesMask, animBase());   // a drag ends in a swipe on release,
        lastInteractionTime = millis();                                    // which would jump one step PAST the flick
        break;
      case TOUCH_SWIPE_LEFT:                    // previous enabled anim
        if (gCarouselOpen) break;
        g_pendingAnim = prevFavorite(gConfig.favoritesMask, animBase());
        lastInteractionTime = millis();
        break;
      case TOUCH_TAP:
        lastInteractionTime = millis();
        if (gCarouselOpen) break;               // no confirm gesture, and a tap through the strip must not feed the cat
        if (currentAnimId == TREATCAT_ID) {                 // feed instead of jitter; flag-only (SPI-safe)
          int tx = touchLastX, ty = touchLastY;
          tx = tx < 0 ? 0 : (tx > 239 ? 239 : tx); ty = ty < 0 ? 0 : (ty > 239 ? 239 : ty);
          gTreatTap = (1u << 31) | ((uint32_t)tx << 12) | (uint32_t)ty;
        } else if (currentAnimId < EYE_COUNT) { isJittering = true; jitterEndTime = millis() + 500; }
        break;
      case TOUCH_SWIPE_DOWN:                    // reserved for the on-device anim config (own TODO)
      case TOUCH_NONE:
        break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Backlight PWM on the RTC 8MHz oscillator, NOT the APB clock that Arduino's analogWrite() uses.
// Light sleep gates APB, which froze LEDC mid-duty for the whole ~14ms frame gap: the pad held whatever
// level it happened to be at, so the backlight chopped at 30Hz (visible flicker) and ignored `brightness`.
// RTC8M keeps ticking through light sleep, and esp_sleep_pd_config keeps its power domain alive, so the
// PWM just... continues. 8MHz / 2^8 = 31.25kHz ceiling; 5kHz is well inside it and far above flicker.
static void backlightBegin() {
  ledc_timer_config_t t = {};
  t.speed_mode      = LEDC_LOW_SPEED_MODE;   // RTC8M is low-speed-channels-only
  t.duty_resolution = LEDC_TIMER_8_BIT;      // matches the 0..255 brightness config
  t.timer_num       = LEDC_TIMER_0;
  t.freq_hz         = 5000;
  t.clk_cfg         = LEDC_USE_RTC8M_CLK;
  ledc_timer_config(&t);
  ledc_channel_config_t c = {};
  c.gpio_num   = TFT_BLK;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel    = LEDC_CHANNEL_0;
  c.timer_sel  = LEDC_TIMER_0;
  c.duty       = 0;
  c.hpoint     = 0;
  ledc_channel_config(&c);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_ON);   // don't power down the 8M osc in light sleep
}
static void backlightSet(uint8_t v) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, v);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// All "configured brightness" call sites go through here so a LOW battery dims uniformly --
// including the slideshow fade math, which scales off the same number.
static uint8_t effectiveBrightness() {
  uint8_t b = gConfig.brightness;
  if (gBatt.state() == BATT_LOW) { b = (uint8_t)(b * 2 / 3); if (!b) b = 1; }
  return b;
}

// loop() task ONLY. displayOff() is an SPI transaction and the IDF SPI master is not thread-safe
// per device: running this from the button task while loop() was mid-flush corrupted the driver's
// descriptor bookkeeping -> CORRUPT HEAP panic in the flush's free() (backtrace decoded 2026-07-16;
// the flush is ~14ms of a ~32ms frame, so it was a ~44% dice roll per press). The button long-press
// therefore latches gPowerOffReq and loop() calls this at its top.
void powerOff() {
  Serial.println("[sleep] deep-sleep entered"); Serial.flush();  // observable marker for tools/touchwake_test.py
  gfx->displayOff(); backlightSet(0);
  while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
  delay(100);
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32
  // S3/classic have RTC IO: wake on the button pin going LOW, holding it high in the RTC domain.
  rtc_gpio_pullup_en((gpio_num_t)BUTTON_PIN);
#if defined(BOARD_WAVESHARE_128)
  if (touchPresent) {
    // Also wake on a screen tap: the CST816S pulses INT (GPIO5) LOW on touch. ext0 is single-pin, so
    // use ext1 ANY_LOW (S3-only mode) to watch button OR touch. Hold INT high in RTC while asleep.
    rtc_gpio_pullup_en((gpio_num_t)TOUCH_INT_PIN);
    esp_sleep_enable_ext1_wakeup((1ULL << BUTTON_PIN) | (1ULL << TOUCH_INT_PIN), ESP_EXT1_WAKEUP_ANY_LOW);
  } else
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
#else
  // S3-Zero: press-to-wake only (BUTTON_PIN is the EC11's own push switch) -- no turn-to-wake. A prior
  // attempt armed ext1 on BUTTON_PIN|ENC_A_PIN, but ext1 leaves ESP_PD_DOMAIN_RTC_PERIPH at AUTO, and
  // esp_sleep.h documents that internal pullups/pulldowns don't hold with that domain powered down --
  // ext1_wakeup_prepare() calls pullup_disable() on every pin it's given. The EC11 has no external
  // pull-ups (the driver relies on INPUT_PULLUP), so ENC_A would float during deep sleep, ANY_LOW would
  // trip on the drift, and the board would wake instantly and reboot-loop -- the exact bug 9342207
  // already fixed once. ext0 forces RTC_PERIPH ON, which is why button-wake works. A real turn-to-wake
  // would need RTC_PERIPH pinned ON plus care around ext1's RTC pad-hold latching its pins; the console
  // unit's actual answer is `sleepMin = 0` (never sleep) -- an existing config field, no code needed.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
#endif
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);  // C3: GPIO deep-sleep wakeup
#endif
  // These two MUST be the last thing before sleeping, with no delay()/yield after them.
  //
  // Disarm the frame-gap timer: wakeup sources are STICKY. loop()'s light sleep arms a ~13ms timer
  // wakeup every nap and nothing clears it, so deep sleep inherits whatever the last nap armed, wakes
  // 13ms later, and the board reboots -- a long-press looked like a restart, not a power-off.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#if defined(BOARD_WAVESHARE_128)
  // Same sticky-source trap, second instance (measured 2026-07-16): touchBegin() arms a GPIO wakeup
  // source (touch INT low, esp_sleep_enable_gpio_wakeup) so the frame-gap LIGHT sleep can't eat swipe
  // INT pulses -- deep sleep inherits that source too, and the CST816S wiggles INT ~5s after power-off
  // (auto-standby entry), so a shelved unit woke itself right back up: `reset=8 wake=7(GPIO) src=cold`
  // in the boot log. ext1 (button|touch, armed above) is the only wake deep sleep should honor.
  // Scoped to this board: the C3 path legitimately wakes deep sleep via its own GPIO source.
  if (touchPresent) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
#endif
  // Hand the 8M oscillator back. backlightBegin() pins it ON so the backlight PWM survives LIGHT sleep,
  // but that pd setting is global -- left ON it would keep the oscillator burning through DEEP sleep too,
  // which is where a shelved unit spends ~all its life and is supposed to draw microamps.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_AUTO);
  esp_deep_sleep_start();
}

void powerOffRequest() { gPowerOffReq = true; }   // button task: flag-only, powerOff() does SPI (see its header)

void singleClick() {
  lastInteractionTime = millis();
  g_pendingAnim = nextFavorite(gConfig.favoritesMask, animBase());   // defer apply to loop() (button task must not touch onAnimEnter/radio)
}

void doubleClick() {
  lastInteractionTime = millis();
  if (currentAnimId < EYE_COUNT) { isJittering = true; jitterEndTime = millis() + 500; }  // jitter is eye-only
}

void multiClick() {
  lastInteractionTime = millis();
  uint8_t base = animBase();
  int clicks = button.getNumberClicks();
  if (clicks == 3) {  // debug screens: outside ANIM_COUNT, so nothing else can reach them.
    // Any later click escapes on its own -- nextFavorite is % ANIM_COUNT, so debug -> 1.
    // Triple again pages to the next debug screen (sensor -> audio -> waterfall -> wrap); on the
    // console the encoder does the same paging, which is the gesture you actually want there.
    g_pendingAnim = stepDebug(base, 1);
  } else if (clicks == 4) {  // easter egg: hop into / advance the effect group (13..37, then 45..55, wrap)
    g_pendingAnim = (base < EYE_COUNT) ? EYE_COUNT
                  : (base == EYE_COUNT + EFFECT_COUNT - 1) ? SWIRL_ID   // 37 -> 45
                  : (base == SWIRL_ID) ? TREATCAT_ID                    // 45 -> 46
                  : (base == TREATCAT_ID) ? GREETZ_ID                   // 46 -> 47
                  : (base == GREETZ_ID) ? GIF_ID                        // 47 -> 48
                  : (base == GIF_ID) ? ATLAS_BASE                       // 48 -> 49 (first ported effect)
                  : (base >= ATLAS_BASE) ? (base + 1 < ANIM_COUNT ? (uint8_t)(base + 1) : EYE_COUNT)  // 49..54 -> +1; 55 wraps to Matrix
                  : (uint8_t)(EYE_COUNT + ((base - EYE_COUNT + 1) % EFFECT_COUNT));
  }
}

// Push runtime prefs to hardware. Call at boot and after any config change.
void applyConfig() {
  backlightSet(effectiveBrightness());
  if (!imuPresent) setPanelRotation(gConfig.flip ? 2 : 0);   // 2 = MADCTL 0xC8, whole-image 180 flip; IMU owns rotation when present (auto-flip in loop)
  refreshPalettes();   // resets rotation to the first enabled palette and rebuilds the LUT
  gLastCycle = millis();   // config just changed: a fresh cycleSec gets a full first window (an hours-old stamp would fire an immediate hop mid-save)
}

// Blocking ~2s low-battery splash. Unlike the boot splashes (setup(), before the button task
// exists) this holds with the button task LIVE: a long-press during the hold just latches
// gPowerOffReq (flag-only), honored at the next loop() top -- or moot, if the cutoff caller
// below powers off anyway.
// Ends on a black canvas so trail modes (matrix/fade effects) don't spend the next seconds
// fading a battery glyph out.
static void batterySplash() {
  canvas->fillScreen(BLACK);
  int x = 60, y = 92, w = 120, h = 56;                          // body, centered on the 240px round panel
  for (int i = 0; i < 3; i++) canvas->drawRect(x - i, y - i, w + 2 * i, h + 2 * i, RED);
  canvas->fillRect(x + w + 3, y + h / 2 - 10, 8, 20, RED);      // terminal nub
  canvas->fillRect(x + 6, y + 6, 14, h - 12, RED);              // the ~10% sliver that's left
  canvas->flush();
  delay(2000);
  canvas->fillScreen(BLACK);
}

// Matrix name reveal: green rain (~2s) while each letter scrambles random glyphs, then
// settles to its bright final char at a per-letter random time -> letters lock in random
// order. Rain fades under the held name. Blocking; runs once at boot before loop().
void bootSplash(const std::string& name) {
  for (int i = 0; i < 24; i++) initMatrixColumn(i);
  int len = name.size() < 1 ? 1 : (int)name.size();
  if (len > 40) len = 40;                  // 240px / 6px caps the slot count at ts 1
  int ts = 240 / (len * 6 + 6);            // 6px per glyph at text size 1
  if (ts < 1) ts = 1; if (ts > 6) ts = 6;
  int gw = 6 * ts, th = 8 * ts;
  int tx = (240 - len * gw) / 2, ty = (240 - th) / 2;

  // Per-letter scramble char + its random settle time; random order falls out of these.
  const uint32_t SCRAMBLE_START = 1000, SETTLE_SPAN = 1300;
  char scr[40]; uint32_t scrAt[40], settleAt[40];
  for (int i = 0; i < len; i++) {
    scr[i] = (char)random(33, 126); scrAt[i] = 0;
    settleAt[i] = SCRAMBLE_START + 200 + random(0, SETTLE_SPAN);
  }

  uint32_t start = millis();
  while (millis() - start < 3400) {
    uint32_t el = millis() - start;
    canvas->fillScreen(BLACK);

    // Rain: full until 2000ms, fade out to 2800ms, gone after.
    int rainA = el < 2000 ? 255 : (el < 2800 ? 255 - (int)(el - 2000) * 255 / 800 : 0);
    if (rainA > 0) {
      canvas->setTextSize(1);
      for (int i = 0; i < 24; i++) {
        if (millis() - matrixCols[i].lastUpdate > (uint32_t)matrixCols[i].speed) {
          matrixCols[i].y += 10; matrixCols[i].lastUpdate = millis();
        }
        int x = i * 10, y = matrixCols[i].y;
        for (int j = 0; j < 12; j++) {
          int tY = y - j * 10;
          if (tY > -10 && tY < 240) {
            int g = (255 - j * 20) * rainA / 255; if (g < 20) g = 20;
            canvas->setCursor(x, tY);
            canvas->setTextColor(gfx->color565(0, g, 0));
            canvas->print((char)random(33, 126));
          }
        }
        if (matrixCols[i].y > 390) initMatrixColumn(i);
      }
    }

    // Name: each slot scrambles random green glyphs (~70ms/change), then locks to its
    // bright letter (dim-green glow behind a bright core) once past its settle time.
    if (el > SCRAMBLE_START) {
      canvas->setTextSize(ts);
      for (int i = 0; i < len; i++) {
        int lx = tx + i * gw;
        if (el >= settleAt[i]) {
          canvas->setTextColor(gfx->color565(0, 85, 0));
          for (int oy = -2; oy <= 2; oy += 2)
            for (int ox = -2; ox <= 2; ox += 2) { canvas->setCursor(lx + ox, ty + oy); canvas->print(name[i]); }
          canvas->setTextColor(gfx->color565(64, 255, 64));
          canvas->setCursor(lx, ty); canvas->print(name[i]);
        } else {
          if (el - scrAt[i] > 70) { scr[i] = (char)random(33, 126); scrAt[i] = el; }
          canvas->setTextColor(gfx->color565(0, 160, 0));
          canvas->setCursor(lx, ty); canvas->print(scr[i]);
        }
      }
    }

    canvas->flush();
    delay(16);
  }
}

// Pick a random enabled palette and spread it across the name as a per-letter gradient:
// core[i] = a stop of that palette, glow[i] = its ~1/3-brightness halo. Builds a scratch LUT
// so the post-boot animation's palette (activePaletteLUT) is left untouched.
static void splashLetterColors(uint16_t core[], uint16_t glow[], int len) {
  uint16_t lut[256];
  uint8_t id = gRotList[esp_random() % gRotList.size()];   // gRotList is never empty ({0}=Rainbow)
  buildPaletteLUT(paletteStops(id, gConfig.customPalettes), lut);
  for (int i = 0; i < len; i++) {
    uint8_t phase = len > 1 ? (uint8_t)(i * 255 / (len - 1)) : 128;
    core[i] = lift565(lut[phase], 20);   // floor brightness so dark palette stops stay legible
    glow[i] = dim565(core[i], 1, 3);
  }
}

// Fade the whole framebuffer toward black by num/den instead of hard-clearing, so moving glyphs
// leave a decaying vapor trail. dim565 truncates, so trails reach true black and vanish cleanly.
static void fadeFrame(uint8_t num, uint8_t den) {
  uint16_t* fb = canvas->getFramebuffer();
  for (int i = 0; i < SCREEN_RES * SCREEN_RES; i++) fb[i] = dim565(fb[i], num, den);
}

// 50% fade, two pixels per op: >>1 within each 565 field, mask clears the bit that bled across
// the field boundary. Identical result to fadeFrame(1,2) (both truncate -> trails reach true
// black), ~10x faster -- boids pays this every frame, the splashes don't care.
static void fadeFrameHalf() {
  uint32_t* fb = (uint32_t*)canvas->getFramebuffer();   // internal-RAM alloc is 4-byte aligned
  for (int i = 0; i < SCREEN_RES * SCREEN_RES / 2; i++) fb[i] = (fb[i] >> 1) & 0x7BEF7BEFu;
}

// Slide name reveal: each letter flies in from a random off-screen point (varied incoming
// angle) and eases into its slot. Angle variety comes from the random start point, so no
// per-frame trig (C3-safe). Blocking; runs once at boot before loop().
void slideSplash(const std::string& name) {
  int len = name.size() < 1 ? 1 : (int)name.size();
  if (len > 40) len = 40;
  int ts = 240 / (len * 6 + 6);
  if (ts < 1) ts = 1; if (ts > 6) ts = 6;
  int gw = 6 * ts, th = 8 * ts;
  int tx = (240 - len * gw) / 2, ty = (240 - th) / 2;

  uint16_t core[40], glow[40];
  splashLetterColors(core, glow, len);

  // Per-letter start point off one of the four edges + a small random launch delay.
  int sx[40], sy[40]; uint32_t launch[40];
  for (int i = 0; i < len; i++) {
    switch (random(4)) {
      case 0: sx[i] = random(-260, 500); sy[i] = random(-400, -240); break;  // from top
      case 1: sx[i] = random(-260, 500); sy[i] = random(480, 640);   break;  // from bottom
      case 2: sx[i] = random(-400, -240); sy[i] = random(-260, 500); break;  // from left
      default: sx[i] = random(480, 640);  sy[i] = random(-260, 500); break;  // from right
    }
    launch[i] = random(0, 500);
  }

  const uint32_t SLIDE = 1200;             // travel time per letter after its launch delay
  uint32_t start = millis();
  bool first = true;
  while (millis() - start < 2600) {
    uint32_t el = millis() - start;
    if (first) { canvas->fillScreen(BLACK); first = false; }   // clean start; then fade to leave vapor trails
    else       fadeFrame(13, 16);
    canvas->setTextSize(ts);
    for (int i = 0; i < len; i++) {
      int fx = tx + i * gw, cx, cy;
      if (el <= launch[i]) { cx = sx[i]; cy = sy[i]; }
      else {
        uint32_t t = el - launch[i];
        if (t >= SLIDE) { cx = fx; cy = ty; }
        else {                               // ease-out: 1-(1-u)^2, in 0..256 fixed point
          int inv = 256 - (int)(t * 256 / SLIDE);
          int e = 256 - inv * inv / 256;
          cx = sx[i] + (fx - sx[i]) * e / 256;
          cy = sy[i] + (ty - sy[i]) * e / 256;
        }
      }
      canvas->setTextColor(glow[i]);                   // palette-tinted glow behind bright core
      for (int oy = -2; oy <= 2; oy += 2)
        for (int ox = -2; ox <= 2; ox += 2) { canvas->setCursor(cx + ox, cy + oy); canvas->print(name[i]); }
      canvas->setTextColor(core[i]);
      canvas->setCursor(cx, cy); canvas->print(name[i]);
    }
    canvas->flush();
    delay(16);
  }
}

// Bounce name reveal: letters fly in off-screen, rattle off the round bezel a few times under
// gravity, and settle into a bottom arc *in order*. Trajectories are reverse-simulated from the
// finished arc (see bounce_splash.h) so the landing is exact -- never a snap. Blocking; runs once
// at boot before loop().
void bounceSplash(const std::string& name) {
  int len = (int)name.size();
  if (len < 1) return;
  if (len > bounce::MAX_LETTERS) { slideSplash(name); return; }   // arc too crowded -> reuse slide

  static bounce::Trajectories T;             // ~13KB -> static, never on the stack
  bounce::compute(T, len, esp_random());
  int ts = bounce::geometryFor(len).ts;

  uint16_t core[40], glow[40];
  splashLetterColors(core, glow, len);

  for (int f = 0; f < T.maxFrames; f++) {
    if (f == 0) canvas->fillScreen(BLACK);             // clean start; then fade to leave vapor trails
    else        fadeFrame(13, 16);
    canvas->setTextSize(ts);
    for (int i = 0; i < len; i++) {
      int local = f - (T.maxFrames - T.frames[i]);   // align every letter's arrival on the last frame
      if (local < 0) continue;                        // this one hasn't entered yet
      int lx = T.x[i][local] - 3 * ts, ly = T.y[i][local] - 4 * ts;  // center the 6ts x 8ts glyph on its point
      canvas->setTextColor(glow[i]);                  // palette-tinted glow behind a bright core
      for (int oy = -2; oy <= 2; oy += 2)
        for (int ox = -2; ox <= 2; ox += 2) { canvas->setCursor(lx + ox, ly + oy); canvas->print(name[i]); }
      canvas->setTextColor(core[i]);
      canvas->setCursor(lx, ly); canvas->print(name[i]);
    }
    canvas->flush();
    delay(16);
  }
  // Let the final motion trails decay (settled letters redrawn crisp on top), then hold the name.
  for (int f = 0; f < 24; f++) {                      // ~380ms: trails reach true black
    fadeFrame(13, 16);
    canvas->setTextSize(ts);
    for (int i = 0; i < len; i++) {
      int lx = T.x[i][T.frames[i] - 1] - 3 * ts, ly = T.y[i][T.frames[i] - 1] - 4 * ts;
      canvas->setTextColor(glow[i]);
      for (int oy = -2; oy <= 2; oy += 2)
        for (int ox = -2; ox <= 2; ox += 2) { canvas->setCursor(lx + ox, ly + oy); canvas->print(name[i]); }
      canvas->setTextColor(core[i]);
      canvas->setCursor(lx, ly); canvas->print(name[i]);
    }
    canvas->flush();
    delay(16);
  }
  delay(3600);  // dwell on the fully-settled, trail-free name -- it's the payoff
}

#if OCELLUS_AUDIO
// ESP-NOW spectrum from the Sensory Bridge console. Written only by the WiFi task,
// read only by loop(). Lock-free by design (SB TODO #10) --- a torn read is a benign 1-frame blend.
static SbStreamMags gAudioRx = {};
static volatile uint32_t gAudioRxMillis = 0;
static volatile uint32_t gAudioRxCount  = 0;   // accepted packets, ever; differenced over a window by renderAudioDebug
static volatile uint32_t gRejCount = 0;   // Junk frames only (see classifySbFrame): foreign senders on ch1 or wire drift.
                                          // The console's own sync/identify chatter (12.5/s!) is recognized and NOT counted --
                                          // counting it pegged this at the display cap in minutes and hid the real anomalies.

// Transients are detected HERE, per packet, not in the renderer. The console streams ~175 pkt/s
// while we render 30-60 fps, so a renderer that only looks at the newest packet each frame never
// sees 2-4 of every 5 --- and a kick is ~60ms, i.e. it can live and die entirely inside one frame.
// That made which transients fired a function of flush timing, which is why Bloom felt arbitrary.
// update() is integer math (audio.cpp) and this callback already runs per packet on the WiFi task,
// so detection is free here. The renderer just drains the flags. A beat/spark that fires twice
// between two frames still draws one ring: refractory (120/90ms) > frame time, so it can't happen.
static BeatDetector rxBeat;                                                           // bass kicks -> outward rings
static BeatDetector rxMid{SNARE_MID_FLOOR, SNARE_MID_RISE, SNARE_REFRACTORY_MS, SNARE_MARGIN_DIV};  // snare bodies -> inward rings
static BeatDetector rxSpark{SPARK_HIGH_FLOOR, SPARK_HIGH_RISE, SPARK_REFRACTORY_MS, SPARK_MARGIN_DIV};  // treble edges -> sparks
static volatile bool     gBeatPend = false, gSnarePend = false, gSparkPend = false;   // set by the WiFi task, drained by the renderer
static volatile uint32_t gBeatCount = 0, gSnareCount = 0, gSparkCount = 0;            // ever; differenced over a window by renderAudioDebug
// Why a snare DIDN'T fire, counted separately -- without these the debug screen shows a missing
// snare and cannot say whether the mid never cleared its bar, the kick out-jumped it, or the
// refractory window ate it. All three look identical from the outside.
static volatile uint32_t gSnareVeto = 0;   // mid fired, but the kick out-jumped it: kick body, not a snare
static volatile uint32_t gSnareRefr = 0;   // mid would have won (cleared its bar AND out-scored the kick) but the refractory window ate it
// The bar each band's packet actually faced (bar == v - score(v)), stashed HERE because score() is
// only valid before update() folds the sample in -- by the time renderAudioDebug snapshots gAudioRx,
// rxBeat/rxMid.prev already equals this same packet's value and a render-side score() call would be
// scoring the sample against itself. No such trap exists on the RX side, where bassS/midS are already
// computed pre-update -- so derive the bar there too and let the screen just read it.
static volatile uint8_t gBassBar = 0, gMidBar = 0;

// Inter-packet gaps, recorded in the WiFi RX callback. Frame-sampled `age` can only ever see the gaps a
// frame happens to land on -- at 58fps against a 176/s stream that is one packet in three, so most
// dropouts are invisible to it. This ring sees every one. Single writer (WiFi task), single reader
// (render); a torn read would cost one wrong pixel on a debug screen, so it goes unlocked on purpose.
static constexpr int GAPN = 256;
static volatile uint16_t gGapRing[GAPN] = {0};
static volatile uint16_t gGapHead = 0, gGapFill = 0;

// Waterfall (id 44) pending column: raw-bin peak-hold between renderer drains. ~175 pkt/s against
// ~58 fps means a frame-sampled column misses 2 of every 3 packets -- and a snare can live and die
// inside one frame, which is the exact blindness id 44 exists to remove (and now feeds Echo, id 41,
// too). Raw uint16 max only
// (integer, WiFi task); audioBin's curve is monotonic, so curving at drain time is identical.
static volatile uint16_t gWfPend[NUM_FREQS] = {0};

// --- serial spectrum tap (snare tuning rig, spec docs/superpowers/specs/2026-07-14-snare-tuning-rig-design.md §1) ---
// SPSC ring: onEspNowRecv (WiFi task) owns head, drainTap (loop task) owns tail. 64 entries is
// ~366ms at ~175 pkt/s -- headroom for a slow frame, not a working set (steady state ~3 deep).
// Always resident (8.4KB static internal RAM, C3 included): a debug facility that must be
// rebuilt to use is a debug facility nobody uses.
struct TapEntry { uint32_t ms; uint16_t mags[NUM_FREQS]; };
static TapEntry gTapRing[64];
static volatile uint16_t gTapHead = 0, gTapTail = 0;
static volatile bool     gTapOn = false;
static volatile uint32_t gTapDrops = 0;          // WiFi task writes, loop reads; any drop invalidates the capture
static uint32_t gTapSent = 0, gTapStartMs = 0;   // loop-task only

static SbSyncSettings gSbSync = {};   // latest console knob/flag broadcast (12.5/s); no staleness:
static volatile bool  gSbSyncSeen = false;   // last-known-good is right across console reboots

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  switch (classifySbFrame(data, len)) {
    case SbFrame::Junk:    gRejCount++; return;   // foreign ident, unknown command, or mags at the wrong size
    case SbFrame::Chatter: return;                // console's identify broadcast and other recognized chatter -- healthy, not counted
    case SbFrame::Sync:                           // knob/flag broadcast: latest-wins store, no math here
      memcpy(&gSbSync, data, sizeof(SbSyncSettings));
      gSbSyncSeen = true;   // flag set after memcpy with no barrier: render core can see flag with partially-visible struct for one frame, same torn-read class gAudioRx tolerates; values range-guarded downstream + next broadcast (80ms) self-corrects
      return;
    case SbFrame::Mags:    break;
  }
  memcpy(&gAudioRx, data, sizeof(SbStreamMags));
  uint32_t t = millis();
  // Every inter-packet gap, not just the ones a frame samples. A clean stream sits at the send interval
  // (~5.7ms at 176/s); a dropped packet shows up as a multiple of it, so the gap tail IS the loss rate.
  if (gAudioRxMillis) {
    uint32_t gap = t - gAudioRxMillis;
    gGapRing[gGapHead] = (uint16_t)(gap > 65535u ? 65535u : gap);
    gGapHead = (uint16_t)((gGapHead + 1) % GAPN);
    if (gGapFill < GAPN) gGapFill++;
  }
  gAudioRxMillis = t;
  gAudioRxCount++;
  for (int i = 0; i < NUM_FREQS; i++)
    if (gAudioRx.spectrogram[i] > gWfPend[i]) gWfPend[i] = gAudioRx.spectrogram[i];
  if (gTapOn) {                       // spectrum tap: never block, never touch Serial from this task
    uint16_t next = (uint16_t)((gTapHead + 1) & 63);
    if (next == gTapTail) gTapDrops++;               // full -> count it; the driver aborts on any drop
    else {
      gTapRing[gTapHead].ms = t;
      memcpy(gTapRing[gTapHead].mags, gAudioRx.spectrogram, sizeof gTapRing[0].mags);
      gTapHead = next;
    }
  }
  // Score both bands BEFORE either update() folds this packet into its history, then let the snare
  // fire only if its transient out-jumped the kick's, each measured against its OWN bar (bass sits
  // far louder in absolute counts, so a raw mid > bass test would silence the snare forever). A
  // kick's beater click lands in the mid band too; its fundamental clears the bass bar by more than
  // its body clears the mid bar, so the phantom snare loses. ASYMMETRIC ON PURPOSE: the kick fires
  // unconditionally and only the snare must win a comparison -- a kick that really happened should
  // draw its ring even when a snare lands on the same beat. score(v) > 0 IS update()'s rising test
  // (audio.cpp), which is what makes the two `else` branches below exact rather than guesses.
  BloomParams p = bloomParamsFromMags(gAudioRx);
  int bassS = rxBeat.score(p.bass), midS = rxMid.score(p.mid);
  // bar == v - score(v) (see BeatDetector::score), valid only here, before update() below folds this
  // packet into prev/baselineQ4. renderAudioDebug reads these rather than re-deriving on the render side.
  gBassBar = (uint8_t)(p.bass - bassS < 0 ? 0 : p.bass - bassS > 255 ? 255 : p.bass - bassS);
  gMidBar  = (uint8_t)(p.mid  - midS  < 0 ? 0 : p.mid  - midS  > 255 ? 255 : p.mid  - midS);
  // Weighted, not raw (see SNARE_VS_KICK_NUM in audio.h). At the 1:1 default this is bit-identical
  // to a raw midS > bassS; the corpus sweep found the weight non-load-bearing once the mid band was
  // moved off the kick body, so it stays 1:1 for the same-packet [bd,sd] case and nothing else.
  bool snareWins = midS * (int)SNARE_VS_KICK_NUM > bassS * (int)SNARE_VS_KICK_DEN;
  bool kick = rxBeat.update(p.bass, t);
  bool mid  = rxMid.update(p.mid,  t);          // ALWAYS called: it owns prev + baseline + refractory
  if (kick)               { gBeatPend  = true; gBeatCount++;  }
  if (mid && snareWins)   { gSnarePend = true; gSnareCount++; }
  else if (mid)           { gSnareVeto++; }     // fired, lost to the kick
  else if (midS > 0 && snareWins) { gSnareRefr++; }    // would have won the kick comparison too -- the ONLY thing that stopped it was the refractory window
  if (rxSpark.update(p.high, t)) { gSparkPend = true; gSparkCount++; }
}

// Drain the WiFi task's peak-hold column into out[] (post-curve). Copy-then-zero is NOT atomic:
// a packet landing between the read and the clear usually donates its peak to the NEXT column,
// but if its store lands in the read-to-clear window that packet's peak is simply lost. Rare
// (few-instruction window, ~3 packets/frame) and bounded to one column -- accepted instead of a
// per-element atomic exchange across four targets. Shared by Echo (id 41) and the waterfall
// debug screen (id 44); only one mode runs at a time, so sharing the pend column is safe.
static void drainWfColumn(uint8_t out[NUM_FREQS]) {
  for (int i = 0; i < NUM_FREQS; i++) {
    uint16_t raw = gWfPend[i]; gWfPend[i] = 0;
    out[i] = audioBin(raw);
  }
}

static void tapEmitRing() {
  while (gTapTail != gTapHead) {
    const TapEntry& e = gTapRing[gTapTail];
    static char line[4 + 10 + 1 + NUM_FREQS * 4 + 2];    // "tap " + millis + ' ' + 256 hex + '\n' + NUL
    char* w = line + sprintf(line, "tap %lu ", (unsigned long)e.ms);
    for (int i = 0; i < NUM_FREQS; i++) { sprintf(w, "%04x", e.mags[i]); w += 4; }   // big-endian hex, matches parseTapLine
    *w++ = '\n';
    Serial.write((const uint8_t*)line, w - line);
    gTapSent++;
    gTapTail = (uint16_t)((gTapTail + 1) & 63);
  }
}

static void tapStop() {
  gTapOn = false;      // stop the WiFi-task pushes first, then flush, so the summary is the capture's last line
  tapEmitRing();
  Serial.printf("{\"type\":\"tap\",\"on\":false,\"sent\":%lu,\"drops\":%lu}\n",
                (unsigned long)gTapSent, (unsigned long)gTapDrops);
}

void drainTap() {
  if (!gTapOn && gTapTail == gTapHead) return;
  tapEmitRing();
  if (gTapOn && millis() - gTapStartMs > 60000UL) tapStop();   // dead-host insurance (spec §1): with
                                                               // nobody draining CDC every write times out
                                                               // and stalls loop() -- cap the exposure
}

// {"cmd":"tap","on":true|false}. COMPACT-JSON substring match only (tools/snare_capture.py sends
// exactly these bytes); a hand-typed spaced variant falls through to handleLine's "unknown cmd".
// Returns true iff the line was consumed.
bool handleTapCmd(const std::string& line) {
  if (line.find("\"cmd\":\"tap\"") == std::string::npos) return false;
  if (line.find("\"on\":true") != std::string::npos) {
    gTapTail = gTapHead;                // discard anything stale from a previous run
    gTapSent = 0; gTapDrops = 0;        // safe: gTapOn is false here, the WiFi task isn't writing
    gTapStartMs = millis();
    gTapOn = true;                      // LAST -- the WiFi task starts pushing the moment this flips
    Serial.println("{\"type\":\"tap\",\"on\":true}");
    return true;
  }
  if (line.find("\"on\":false") != std::string::npos) { tapStop(); return true; }
  return false;                         // has "cmd":"tap" but no recognizable "on" -> let handleLine err it
}

// One instance for all four audio modes: hue stays continuous when switching between them.
static HueSlew gHueSlew;
// Shared band AGC (TODO #9): one instance per band, shared across the audio modes so the
// envelopes stay continuous over a mode switch. Render-side only -- the RX detectors above
// stay on raw values (see BandAGC in audio.h). Updated by whichever audio renderer is live.
static BandAGC gAgcBass, gAgcLevel;
// The one choke point for the stretch: every renderer (and the debug screen's `s` readout) goes
// through this, so the next audio mode can't half-wire the AGC. High and mid pass through raw on
// purpose -- nothing renders a stretched value of either (sparks and snares are edge-triggered on
// raw values, and a detector behind an AGC would double-adapt).
static BloomParams agcStretch(const BloomParams& p, uint32_t now) {
  return { gAgcBass.update(p.bass, now), p.mid, p.high, gAgcLevel.update(p.level, now) };
}
static NoteHue gNoteHue;      // chromagram -> note hue, shared by the audio modes (one runs at a time)
static HueSlew gSbHueSlew;    // fast-rate instance for the SB wheel (ratePerS set in setup())

// Rebuild the SB wheel only when the synced incandescent filter actually moves (256 entries of
// integer math -- cheap, but 12.5 sync frames/s would still be pointless churn).
static void ensureSbWheel() {
  float f = gSbSyncSeen ? gSbSync.INCANDESCENT_FILTER : 0.0f;
  if (!(f >= 0.0f && f <= 1.0f)) f = 0.0f;        // radio float: NaN/Inf/junk -> plain wheel
  int mix = (int)(f * 255.0f + 0.5f);
  if (mix == gSbWheelMix) return;
  buildWheelLUT(sbWheelLUT, (uint8_t)mix);
  gSbWheelMix = mix;
}

// The console's displayed hue right now, byte-hue units: CHROMA-knob base, pulled toward the
// detected note hue when it runs NOTE_COLOR (atonal music collapses back to the knob), plus
// hue_shift x1 -- these ARE the console's units, unlike the x40 phase-noise scaling of the
// legacy path. Before the first sync frame (<100ms after join): hue_shift alone from base 0.
static float sbHueTarget(const SbStreamMags& snap) {
  float base = 0.0f;
  if (gSbSyncSeen) {
    float knob = gSbSync.CHROMA_KNOB;
    if (!(knob >= 0.0f && knob <= 1.0f)) knob = 1.0f;             // radio float guard
    float chromaVal = knob < 0.95f ? knob * 1.05263157f : 1.0f;   // console knobs.h; its chromatic
    base = 255.0f * chromaVal;                                    // zone (>=0.95) simplified to 1.0
    if (gSbSync.NOTE_COLOR) {
      gNoteHue.update(snap.chromagram);
      base = noteColorBase(base, gNoteHue.hue, gNoteHue.gate);
    }
  }
  return base + snap.hue_shift;   // HueSlew fmods the wrap and guards non-finite
}

static bool radioOn = false;
// Power the radio only while an audio mode is active (battery gate). Idempotent.
void ensureRadio(bool on) {
  if (on == radioOn) return;
  if (on) {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);   // match the console's default STA channel
    bool ok = (esp_now_init() == ESP_OK);
    Serial.printf("[espnow] init: %s\n", ok ? "OK" : "FAIL");
    esp_now_register_recv_cb(onEspNowRecv);
  } else {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);                              // radio powered down
  }
  radioOn = on;
}
#endif  // OCELLUS_AUDIO

void setup() {
  Serial.setTxBufferSize(4096);  // tap lines are ~800B/frame at ~175pkt/s; the HWCDC default 256B would make drainTap block on USB flush mid-frame
  Serial.setRxBufferSize(2048);  // default HWCDC RX ring is 256B; full config `set` (~400-700B) overflows it between our once-per-frame drains, dropping the trailing '\n' so the line is silently lost. Match the g_rxbuf cap.
  Serial.begin(115200);
  if (!LittleFS.begin(true))   // formatOnFail: first boot on a fresh partition formats it LittleFS
    Serial.println("[boot] WARN: LittleFS mount failed");
  else
    Serial.printf("[boot] LittleFS %u/%u bytes used\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  backlightBegin(); backlightSet(0);   // backlight OFF before anything touches the panel -> on wired-BLK hardware this hides the whole power-on/reset window where the uninitialized panel shows white
  bool coldBoot = esp_reset_reason() != ESP_RST_DEEPSLEEP;   // false on deep-sleep wake -> skip the boot splash so resume is instant
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const char* wakeSrc = "cold";                              // decode WHAT woke us -- tools/touchwake_test.py greps this line
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) wakeSrc = "button";
#if defined(BOARD_WAVESHARE_128)                           // only this board arms ext1 (button+touch)
  else if (wakeCause == ESP_SLEEP_WAKEUP_EXT1)             // the status mask says which of the two fired
    wakeSrc = (esp_sleep_get_ext1_wakeup_status() & (1ULL << TOUCH_INT_PIN)) ? "touch" : "button";
#endif
#else
  if (wakeCause == ESP_SLEEP_WAKEUP_GPIO) wakeSrc = "button";  // C3 wakes on the button GPIO only (no ext1/touch wake)
#endif
  Serial.printf("\n[boot] reset=%d wake=%d src=%s cold=%d\n", (int)esp_reset_reason(), (int)wakeCause, wakeSrc, (int)coldBoot);
  // Panel to black FIRST --- before the slow config + radio work --- so a wake shows minimal white even on
  // hardware where the backlight pin isn't wired (always-on) and can't be gated off.
  // This list is "boards MEASURED clean at 80MHz", not "boards with nice wiring" -- SPI speed here is
  // wiring-dependent and the only way to know is to try it. The GC9A01 is rated for 80MHz; 40 is a fallback
  // from one early C3 breadboard rig that glitched, and it is expensive: the flush IS the frame in audio
  // modes (render ~3ms), and 115KB at 40MHz is 23ms on the wire vs 11.5 at 80. Measured on the S3-Zero
  // (breadboard, and clean at 80): flush 25.5 -> 14.0ms, audio modes 35 -> 58fps.
  // Worth trying on the remaining boards; move one up here once its panel is confirmed glitch-free.
#if defined(BOARD_WAVESHARE_128) || defined(BOARD_S3_ZERO)
  gfx->begin(80000000);
#else
  gfx->begin(40000000); // ponytail: 80MHz was out of spec for breadboard jumpers; old fallback-to-40M was dead code (begin() returns true even when SPI is marginal)
#endif
  gfx->displayOff();        // DISPOFF outputs hardware black regardless of GRAM -> hides the power-on garbage / retained pre-sleep frame even where BLK is unwired/always-on
  gfx->fillScreen(BLACK);   // clear GRAM to black while the display is blanked (nothing on screen yet)

  loadConfig(gConfig);
  initSinLUT();
  if (!isPlayableId(currentAnimId)) currentAnimId = 0;   // guard RTC garbage / reserved holes on cold boot
  currentAnimId = resolveStartupId(gConfig.startupMode, gConfig.startupId, currentAnimId,
                                   ANIMS[random(PLAYABLE_ENTRY_COUNT)].id);   // pick from real entries -- id space has holes
  onAnimEnter(currentAnimId);   // radio init (audio modes) now runs with the panel already black

  // blink backlight forever if the 115KB canvas alloc failed (was a silent hang)
  // GFX_SKIP_OUTPUT_BEGIN: gfx->begin() already ran above; default canvas->begin() re-inits the panel -> a second power-on-garbage flash
  if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) { while(1) { backlightSet(255); delay(200); backlightSet(0); delay(200); } }
  // Where the framebuffer landed decides whether flush is worth optimizing: Arduino_Canvas prefers
  // ps_malloc when PSRAM exists, and every flush reads all 115KB back out of it.
  // Largest free BLOCK, not just total free: ESP-NOW/WiFi comes up later (gated on audio modes), and what
  // it needs is a contiguous internal block. Total free can look healthy while fragmentation starves it.
  Serial.printf("[boot] canvas fb in %s, internal free %lu (largest block %lu), psram free %lu\n",
                esp_ptr_external_ram(canvas->getFramebuffer()) ? "PSRAM" : "internal RAM",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned long)ESP.getFreePsram());
  canvas->fillScreen(BLACK); canvas->flush();   // clean black frame into GRAM...
  gfx->displayOn();                             // ...now safe to unblank; panel has shown nothing since begin()
  applyConfig();                                // bring rotation, palettes, and backlight up to brightness
#if OCELLUS_AUDIO
  gSbHueSlew.ratePerS = SB_HUE_SLEW_PER_S;   // SB wheel chases the console's real hue (see audio.h)
#endif
  imuBegin();   // QMI8658 accel for auto-flip (Waveshare board); no-op / imuPresent=false elsewhere
  touchBegin();  // CST816S gestures (Waveshare board); no-op / touchPresent=false elsewhere
  encoderBegin();  // EC11 quadrature ISRs (S3-Zero); no-op elsewhere

  button.setClickMs(600); button.setDebounceMs(80);
  button.attachClick(singleClick); button.attachDoubleClick(doubleClick);
  button.attachMultiClick(multiClick); button.attachLongPressStart(powerOffRequest);

  if (coldBoot && gConfig.nameBootSplash && !gConfig.name.empty()) {            // name reveal only on real power-on, not on every wake
    const std::string& s = gConfig.bootSplashStyle;
    (s == "slide" ? slideSplash : s == "bounce" ? bounceSplash : bootSplash)(gConfig.name);
  }

  uint32_t readyAt = millis();
  lastInteractionTime = readyAt; pauseStartTime = readyAt; stateEndTime = readyAt + 500; nextFrameTime = readyAt;
  xTaskCreate(buttonReadTask, "ButtonTask", 4096, NULL, 2, NULL);  // 4KB: touchPoll() runs Wire I2C in this task; the CST816S idle-NAK error path (Wire log_e -> vprintf) overflowed a 2KB stack -> canary panic in audio modes
  Serial.printf("[boot] cpu %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
}

// DOOM corridor tube, drawn with a vertical pixel offset (yOff) so the trap-door drop can pan between
// two stacked copies. holeSeg = segment whose floor to omit (-1 = solid). Colors come from the active palette.
static void drawCorridor(float speed, float turn, float flicker, int yOff, int holeSeg) {
  const int NUM_SEG = 15;
  const float spacing = 1.0f, max_z = (float)NUM_SEG * spacing;
  struct Seg { int x1, y1, x2, y2, size; uint16_t col, dark; };
  Seg s[NUM_SEG];
  float offset = fmod(speed, spacing);
  int cy = 120 + yOff;
  for (int i = 0; i < NUM_SEG; i++) {
    float z = max_z - (i * spacing) - offset;
    if (z <= 0.1f) z = 0.1f;
    s[i].size = (int)(240.0f / z);
    int cx = 120 + (int)(turn * (z * z) * 1.5f);
    s[i].x1 = cx - s[i].size/2; s[i].y1 = cy - s[i].size/2;
    s[i].x2 = cx + s[i].size/2; s[i].y2 = cy + s[i].size/2;
    uint16_t base = pColor(15, (int)(speed * 10 + i * 20));
    uint8_t r = (base >> 11) << 3, g = ((base >> 5) & 0x3F) << 2, b = (base & 0x1F) << 3;
    s[i].col = gfx->color565(r * flicker, g * flicker, b * flicker);
    s[i].dark = gfx->color565(r * flicker / 5, g * flicker / 4, b * flicker / 3);
  }
  for (int i = 0; i < NUM_SEG - 1; i++) {
    if (s[i].size < 2) continue; // don't skip the nearest (huge) wall: that left the screen edges black. GFX clips offscreen.
    fillQuad(canvas, s[i+1].x1,s[i+1].y1, s[i].x1,s[i].y1, s[i].x1,s[i].y2, s[i+1].x1,s[i+1].y2, s[i].dark); // left
    fillQuad(canvas, s[i+1].x2,s[i+1].y1, s[i].x2,s[i].y1, s[i].x2,s[i].y2, s[i+1].x2,s[i+1].y2, s[i].dark); // right
    fillQuad(canvas, s[i+1].x1,s[i+1].y1, s[i].x1,s[i].y1, s[i].x2,s[i].y1, s[i+1].x2,s[i+1].y1, s[i].dark); // top
    if (i != holeSeg)   // omit the floor on the trap tile -> a real gap you fall through
      fillQuad(canvas, s[i+1].x1,s[i+1].y2, s[i].x1,s[i].y2, s[i].x2,s[i].y2, s[i+1].x2,s[i+1].y2, s[i].dark); // bottom
    canvas->drawLine(s[i+1].x1, s[i+1].y1, s[i].x1, s[i].y1, s[i].col);
    canvas->drawLine(s[i+1].x1, s[i+1].y2, s[i].x1, s[i].y2, s[i].col);
    canvas->drawLine(s[i+1].x2, s[i+1].y1, s[i].x2, s[i].y1, s[i].col);
    canvas->drawLine(s[i+1].x2, s[i+1].y2, s[i].x2, s[i].y2, s[i].col);
    canvas->drawRect(s[i].x1, s[i].y1, s[i].size, s[i].size, s[i].col);
  }
  canvas->drawRect(s[NUM_SEG-1].x1, s[NUM_SEG-1].y1, s[NUM_SEG-1].size, s[NUM_SEG-1].size, s[NUM_SEG-1].col);
}

// FRACTAL CLOCK: recursive hand-tree (Rob Mayoff). Two child rotators built once per frame from
// fastCos/fastSin (the only trig); the recursion below is pure multiply-add. Free-runs off millis()
// (no RTC) so it morphs endlessly. Depth-capped and sub-pixel-pruned.
namespace {
  constexpr int FRACTAL_DEPTH = 10;               // 2^(d+1)-2 = 2046 segments/frame
  float    fr0x, fr0y, fr1x, fr1y;                // per-frame child rotators (cos/sin * scale)
  uint16_t fracDepthCol[FRACTAL_DEPTH + 1];       // per-depth palette color, rebuilt each frame
}
void fractalBranch(float x, float y, float dx, float dy, int depth) {
  float ex = x + dx, ey = y + dy;
  canvas->drawLine((int)x, (int)y, (int)ex, (int)ey, fracDepthCol[depth]);
  if (depth >= FRACTAL_DEPTH || dx * dx + dy * dy < 1.0f) return;   // depth cap or sub-pixel prune
  fractalBranch(ex, ey, dx * fr0x - dy * fr0y, dx * fr0y + dy * fr0x, depth + 1);
  fractalBranch(ex, ey, dx * fr1x - dy * fr1y, dx * fr1y + dy * fr1x, depth + 1);
}

// --- GIF PLAYER (id GIF_ID) : animated GIFs from LittleFS via bitbank2/AnimatedGIF --------------
// Clips are baked host-side to 240x240 / 12 fps / 64 colours by tools/bake_gif.py; the device only
// decodes. Measured on the board 2026-08-01: 38 ms/frame worst case (busy photographic
// content) against 12 fps's 83 ms budget, so decode is not the constraint -- see the spec.
//
// The framebuffer persists between frames on purpose: that is what makes the GIF disposal modes
// (restore-to-background / restore-to-previous) work without a second buffer, so GIF_ID is in the
// no-clear list in renderFrame.
static File gGifFile;

static void* gifOpenCb(const char* fname, int32_t* pSize) {
  gGifFile = LittleFS.open(fname, "r");
  if (!gGifFile) return nullptr;
  *pSize = gGifFile.size();
  return (void*)&gGifFile;
}
static void gifCloseCb(void*) { if (gGifFile) gGifFile.close(); }
static int32_t gifReadCb(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  int32_t want = iLen;
  if (pFile->iSize - pFile->iPos < want) want = pFile->iSize - pFile->iPos;
  if (want <= 0) return 0;
  int32_t got = gGifFile.read(pBuf, want);
  pFile->iPos = gGifFile.position();
  return got;
}
static int32_t gifSeekCb(GIFFILE* pFile, int32_t iPosition) {
  gGifFile.seek(iPosition);
  pFile->iPos = (int32_t)gGifFile.position();
  return pFile->iPos;
}

// Palette index -> RGB565 straight into the canvas framebuffer. Clipped on every side: a clip that
// was not produced by bake_gif.py can be any size, and a hand-uploaded one is entirely possible.
static void gifDrawCb(GIFDRAW* pDraw) {
  int y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= 240) return;
  int x0 = pDraw->iX, w = pDraw->iWidth;
  if (x0 < 0) { w += x0; x0 = 0; }
  if (x0 + w > 240) w = 240 - x0;
  if (w <= 0) return;

  uint16_t* d = (uint16_t*)canvas->getFramebuffer() + y * 240 + x0;
  uint8_t*  s = pDraw->pPixels + (x0 - pDraw->iX);
  uint16_t* pal = pDraw->pPalette;
  if (pDraw->ucHasTransparency) {
    const uint8_t tr = pDraw->ucTransparent;
    for (int x = 0; x < w; x++) { uint8_t p = s[x]; if (p != tr) d[x] = pal[p]; }
  } else {
    for (int x = 0; x < w; x++) d[x] = pal[s[x]];
  }
}

static void gifCloseClip() {
  if (gGifOpen && gGif) gGif->close();
  gGifOpen = false;
}

// Free the decoder when leaving the mode. Measured 5,008 B returned on the board (heap
// 109,428 -> 114,436 switching away) -- the spec guessed ~25KB, which would have been worth
// worrying about; 5KB is not, and this stays only because it is two lines.
static void gifRelease() {
  gifCloseClip();
  if (gGif) { delete gGif; gGif = nullptr; }
}

static bool gifOpenClip(int i) {
  gifCloseClip();
  if (!gGif || i < 0 || i >= gGifCount) return false;
  char path[GIF_NAME_MAX + 8]; gifPath(path, sizeof path, gGifList[i].name);
  if (!gGif->open(path, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) return false;
  gGifOpen = true;
  gGifLoops = 0;
  gGifClipStartMs = millis();
  gGifNextFrameMs = 0;                 // decode the first frame immediately
  canvas->fillScreen(BLACK);           // a clip smaller than the panel must not sit on the last one
  return true;
}

static void gifNote(const char* a, const char* b) {
  canvas->fillScreen(BLACK);
  canvas->setTextColor(gfx->color565(130, 130, 130));
  canvas->setTextSize(2); canvas->setCursor(48, 104); canvas->print(a);
  if (b) { canvas->setTextSize(1); canvas->setCursor(52, 140); canvas->print(b); }
}

void renderGif(uint32_t now) {
  if (!gGif) {                                   // allocated here, not in onAnimEnter, so a failed
    gGif = new (std::nothrow) AnimatedGIF();      // alloc redraws its notice every frame instead of
    if (gGif) {                                   // leaving whatever was on screen before
      gGif->begin(GIF_PALETTE_RGB565_LE);
      gGifsDirty = true;
    }
  }
  if (!gGif) { gifNote("No memory", "for GIF decoder"); return; }

  if (gGifsDirty) {
    gifCloseClip();
    gGifCount = gGifStore.list(gGifList, GIF_MAX);
    if (gGifIdx >= gGifCount) gGifIdx = 0;
    gGifsDirty = false;
    if (gGifCount > 0 && !gifOpenClip(gGifIdx)) gGifCount = 0;   // unreadable -> fall to empty state
  }
  if (gGifCount == 0) { gifNote("No GIFs", "upload via config"); return; }

  if (now < gGifNextFrameMs) return;             // not due yet; the framebuffer still holds the frame

  int delayMs = 0;
  int rc = gGif->playFrame(false, &delayMs);     // false: we do our own pacing, never block the loop
  if (delayMs < 20) delayMs = 20;                // a 0ms-delay clip would spin the decoder flat out
  gGifNextFrameMs = now + (uint32_t)delayMs;

  if (rc <= 0) {                                 // end of clip
    gGifLoops++;
    // "N loops or the hold, whichever is longer" -- a 1s clip loops until the hold elapses, a long
    // one always gets at least one full pass.
    uint32_t hold = (uint32_t)gConfig.gifSec * 1000UL;
    bool done = (now - gGifClipStartMs) >= hold;
    if (done && gGifCount > 1) {
      gGifIdx = (gGifIdx + 1) % gGifCount;
      if (!gifOpenClip(gGifIdx)) { gGifsDirty = true; }   // bad file -> re-list next frame
    } else {
      gGif->reset();                             // same clip again
      gGifNextFrameMs = 0;
    }
  }
}

// --- SLIDESHOW (id SLIDESHOW_ID) : full-frame RGB565 slides from LittleFS, fade through black ---
// (state variables declared near gSlidesDirty, above -- onAnimEnter resets gSlideIdx before this
// function is even defined in the file, so they can't live down here)
static void loadSlideToFb(int i) {
  char path[24]; snprintf(path, sizeof path, "/slide%d.raw", i);
  File f = LittleFS.open(path, "r");
  if (!f) return;
  f.read((uint8_t*)canvas->getFramebuffer(), SLIDE_BYTES);   // little-endian RGB565, row-major
  f.close();
}

void renderSlideshow(uint32_t now) {
  if (gSlidesDirty) {                                        // slide set changed (or first entry)
    SlideMeta m[SLIDE_MAX];
    gSlideCount = gSlideStore.list(m, SLIDE_MAX);
    if (gSlideIdx >= gSlideCount) gSlideIdx = 0;
    gSlidePhase = 0; gSlideStep = 0; gSlideShownMs = now;
    gSlidesDirty = false;
    backlightSet(effectiveBrightness());   // undo any interrupted fade -- else a mid-fade slide_clear leaves the empty-state text on a dark panel
    if (gSlideCount > 0) loadSlideToFb(gSlideIdx);
  }
  if (gSlideCount == 0) {                                    // empty state (this branch clears)
    canvas->fillScreen(BLACK);
    canvas->setTextColor(gfx->color565(130, 130, 130));
    canvas->setTextSize(2); canvas->setCursor(48, 104); canvas->print("No slides");
    canvas->setTextSize(1); canvas->setCursor(52, 140); canvas->print("upload via config");
    return;                                                 // framebuffer persists between frames otherwise
  }

  uint32_t holdMs = (uint32_t)gConfig.slideshowSec * 1000UL;
  switch (gSlidePhase) {
    case 0:                                                  // STEADY
      if (gSlideCount > 1 && now - gSlideShownMs >= holdMs) { gSlidePhase = 1; gSlideStep = SLIDE_FADE_STEPS; }
      break;
    case 1:                                                  // FADE-OUT
      gSlideStep--;
      backlightSet((uint8_t)((uint32_t)effectiveBrightness() * gSlideStep / SLIDE_FADE_STEPS));
      if (gSlideStep <= 0) {
        gSlideIdx = (gSlideIdx + 1) % gSlideCount;
        loadSlideToFb(gSlideIdx);
        gSlidePhase = 2; gSlideStep = 0;
      }
      break;
    case 2:                                                  // FADE-IN
      gSlideStep++;
      backlightSet((uint8_t)((uint32_t)effectiveBrightness() * gSlideStep / SLIDE_FADE_STEPS));
      if (gSlideStep >= SLIDE_FADE_STEPS) { backlightSet(effectiveBrightness()); gSlidePhase = 0; gSlideShownMs = now; }
      break;
  }
  // No per-frame redraw: the slide sits in the framebuffer; renderFrame's flush pushes it each frame.
}

// QR (id 34, spec 2026-07-07): blit the config.html-encoded module bitmap. The GC9A01 is physically
// round, so the whole code (finder corners included) must fit the inscribed square ~170px, quiet
// zone 2 modules (bump toward 4 if scanning proves flaky). Static frame; loop()'s fillScreen(BLACK)
// leaves everything outside the white field dark.
void renderQR() {
  int size = gConfig.qrSize;
  bool ok = size >= 21 && (int)gConfig.qrBits.size() >= ((size * size + 7) / 8) * 2;  // codec clamps size; short bits = truncated save
  if (!ok) {
    canvas->setTextSize(1);
    canvas->setTextColor(gfx->color565(120, 200, 255));
    canvas->setCursor(54, 116);
    canvas->print("Set QR in config.html");
    return;
  }
  const int quiet = 2, inscribed = 170;
  int module = inscribed / (size + 2 * quiet);
  if (module < 1) module = 1;                       // version ~40 won't scan on 240px anyway, but don't div-zero the layout
  int span = module * (size + 2 * quiet);
  int x0 = (240 - span) / 2, y0 = (240 - span) / 2;
  canvas->fillRect(x0, y0, span, span, WHITE);      // scanners require the light border
  for (int r = 0; r < size; r++)
    for (int c = 0; c < size; c++)
      if (qrModule(gConfig.qrBits, size, r, c))
        canvas->fillRect(x0 + (quiet + c) * module, y0 + (quiet + r) * module, module, module, BLACK);
}

// FLYING TOASTERS (id 35): the After Dark classic. Fleet drifts upper-right -> lower-left at a
// 2:1 slope, wings flapping, the occasional slice of toast along for the ride. Sprites are
// RGB565 with a magenta transparent key, generated by tools/toaster_convert.py from the GIFs
// in references/toasters/ (bryanbraun/after-dark-css; artwork (c) Berkeley Systems).
struct Toaster { int16_t x4, y4; uint8_t speed, kind, frame; uint32_t lastFlap; };  // x4/y4 = quarter-px
static Toaster gToasters[7];

static void toasterSpawn(Toaster& t, bool onscreen = false) {
  t.speed = random(2, 6);                                  // quarter-px per frame; dx = speed/2 px
  t.kind = random(4) ? 0 : (uint8_t)random(1, 5);          // 0 = toaster, 1..4 = toast burn level
  t.frame = random(4); t.lastFlap = 0;
  if (onscreen) { t.x4 = random(0, 200) * 4; t.y4 = random(-40, 180) * 4; return; }  // first seed: mid-flight
  int lane = random(0, 60 + 240 + 120);                    // top edge (with left overhang) + upper right edge
  if (lane < 300) { t.x4 = (lane - 60) * 4; t.y4 = -TOASTER_SPRITE_H * 4; }
  else            { t.x4 = 240 * 4; t.y4 = (lane - 300) * 4; }
}

static void renderToasters(uint32_t now) {
  static bool seeded = false;
  if (!seeded) { for (Toaster& t : gToasters) toasterSpawn(t, true); seeded = true; }
  for (Toaster& t : gToasters) {
    t.x4 -= 2 * t.speed; t.y4 += t.speed;
    if (t.x4 < -TOASTER_SPRITE_W * 4 || t.y4 > 240 * 4) toasterSpawn(t);
    if (!t.kind && now - t.lastFlap >= 200u - t.speed * 30u) { t.frame = (t.frame + 1) & 3; t.lastFlap = now; }
    const uint16_t* spr = t.kind ? kToastFrames[t.kind - 1] : kToasterFrames[t.frame];
    int x = t.x4 >> 2, y = t.y4 >> 2;
    for (int j = 0; j < TOASTER_SPRITE_H; j++)             // drawPixel clips the offscreen margins
      for (int i = 0; i < TOASTER_SPRITE_W; i++) {
        uint16_t v = spr[j * TOASTER_SPRITE_W + i];
        if (v != TOASTER_TRANSPARENT) canvas->drawPixel(x + i, y + j, v);
      }
  }
}

// BOIDS (id 36): partner's flocking sketch, ported 1:1 from references/catsoup_pkg/effects.js
// ("boids"): alignment + cohesion + short-range separation within a 28px radius, speed-capped,
// toroidal wrap, triangle facing velocity (unit vector from the speed clamp -- no atan2).
// In-place update order kept from the JS. O(n^2) with n=32; per-boid float is C3-safe (the
// no-FPU discipline bans per-pixel trig, not 32 sqrtf/frame). Trails: loop() skips the clear
// for BOIDS_ID and fadeFrame halves the framebuffer instead -- the lab's translucent fill.
struct Boid { float x, y, vx, vy; };
constexpr int BOID_N = 32;                       // lab default (count 4 * 8); O(n^2) budget
static Boid gBoids[BOID_N];

static void renderBoids() {
  static bool seeded = false;
  if (!seeded) {
    for (Boid& b : gBoids)
      b = { (float)random(240), (float)random(240),
            random(-100, 101) / 100.0f, random(-100, 101) / 100.0f };
    seeded = true;
  }
  fadeFrameHalf();                               // 50%/frame decay, truncates to true black
  for (int i = 0; i < BOID_N; i++) {
    Boid& b = gBoids[i];
    float ax = 0, ay = 0, cx = 0, cy = 0, sx = 0, sy = 0; int cnt = 0;
    for (int j = 0; j < BOID_N; j++) {
      if (j == i) continue;
      const Boid& o = gBoids[j];
      float dx = o.x - b.x, dy = o.y - b.y, d2 = dx * dx + dy * dy;
      if (d2 < 28.0f * 28.0f) {
        ax += o.vx; ay += o.vy; cx += o.x; cy += o.y; cnt++;
        if (d2 < 120.0f) { sx -= dx; sy -= dy; }
      }
    }
    if (cnt) {
      ax /= cnt; ay /= cnt; cx = cx / cnt - b.x; cy = cy / cnt - b.y;
      b.vx += ax * 0.05f + cx * 0.002f + sx * 0.02f;
      b.vy += ay * 0.05f + cy * 0.002f + sy * 0.02f;
    }
    float m = sqrtf(b.vx * b.vx + b.vy * b.vy);
    if (m > 1.6f) { b.vx *= 1.6f / m; b.vy *= 1.6f / m; m = 1.6f; }
    if (m < 0.001f) m = 1;                       // stationary boid: degenerate triangle, don't div0
    b.x += b.vx * 3.6f; b.y += b.vy * 3.6f;      // lab speed 3 (*0.6) at 60fps; we cap at maxFps 30 -> double the step
    if (b.x < 0) b.x += 240; else if (b.x > 240) b.x -= 240;
    if (b.y < 0) b.y += 240; else if (b.y > 240) b.y -= 240;
    float ux = b.vx / m, uy = b.vy / m;          // heading; perp = (-uy, ux)
    canvas->fillTriangle((int)(b.x + 5 * ux),           (int)(b.y + 5 * uy),
                         (int)(b.x - 4 * ux - 3 * uy),  (int)(b.y - 4 * uy + 3 * ux),
                         (int)(b.x - 4 * ux + 3 * uy),  (int)(b.y - 4 * uy - 3 * ux),
                         pColor(5, i * 8));      // lab cyan -> per-unit palette, flock spans nearby hues
  }
}

// ========================== GARDEN EELS (id 37) ==========================
// Partner's chibi reef, ported full-scene from references/catsoup_pkg/effects.js
// ("gardeneels"): water gradient, god rays, kelp, fish-shadow schools, jellyfish,
// five undulating chibi eels (bands/spots, sparkle eyes, 5 rotating expressions),
// sand drawn over the eel bases, a hermit crab that walks in carrying one of 26
// homes, rising bubbles. Canvas2D alpha becomes 1/16-step 565 blends; rotated
// ellipses (pincers) and the violin/guitar tilt are axis-aligned approximations.
// Per-object float/sinf only (a few hundred per frame) -- no per-pixel trig.
constexpr uint16_t geC565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static inline uint16_t geBlend(uint16_t d, uint16_t s, uint8_t a16) {  // a16/16 of s over d
  uint32_t dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
  uint32_t sr = (s >> 11) & 31, sg = (s >> 5) & 63, sb = s & 31;
  return (uint16_t)((((dr * (16 - a16) + sr * a16) >> 4) << 11) |
                    (((dg * (16 - a16) + sg * a16) >> 4) << 5) |
                    ((db * (16 - a16) + sb * a16) >> 4));
}
static inline void geBlendPx(uint16_t* fb, int x, int y, uint16_t c, uint8_t a16) {
  if ((unsigned)x < 240u && (unsigned)y < 240u) { uint16_t& p = fb[y * 240 + x]; p = geBlend(p, c, a16); }
}
static void geBlendEllipse(uint16_t* fb, float cx, float cy, float rw, float rh, uint16_t c, uint8_t a16, bool upperHalf) {
  int ih = (int)rh; if (ih < 1) ih = 1;
  for (int dy = -ih; dy <= (upperHalf ? 0 : ih); dy++) {
    float fy = dy / rh; float half = rw * sqrtf(1.0f - fy * fy); int y = (int)(cy + dy);
    for (int x = (int)(cx - half); x <= (int)(cx + half); x++) geBlendPx(fb, x, y, c, a16);
  }
}
static void geLine3(int x0, int y0, int x1, int y1, uint16_t c) {  // 3px near-vertical stroke
  canvas->drawLine(x0 - 1, y0, x1 - 1, y1, c); canvas->drawLine(x0, y0, x1, y1, c); canvas->drawLine(x0 + 1, y0, x1 + 1, y1, c);
}
static void geQuad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
  canvas->fillTriangle(x0, y0, x1, y1, x2, y2, c); canvas->fillTriangle(x0, y0, x2, y2, x3, y3, c);
}
static inline float geRnd() { return random(10000) * 0.0001f; }

struct GeEel  { float x, h, ph, extPh, extSp, mph, bend, waveK, waveSp, waveAmp, wph; int8_t gaze; uint8_t col; };
struct GeKelp { float x, h, ph; };
struct GeBub  { float x, y, r, sp, ph; };
struct GeJelly{ float x, y, ph, sp, gph; uint8_t hue; bool glow; };
struct GeShad { float x, y, sp, ph, sz; int8_t dir; bool live; };
struct GeCrab { float x, y, tx, ty, sp, ph; uint32_t nextTurn, leaveAt; int8_t face; uint8_t home; bool back, live, leaving; };
constexpr int GE_NE = 5, GE_NK = 9, GE_NB = 20, GE_NJ = 3, GE_NS = 16;
constexpr float GE_SAND = 240 * 0.82f;
static GeEel  geEels[GE_NE];
static GeKelp geKelpA[GE_NK];
static GeBub  geBubA[GE_NB];
static GeJelly geJellyA[GE_NJ];
static GeShad geShadA[GE_NS];
static GeCrab geCrab;
static uint32_t geShadNext, geCrabNext;
static bool geSeeded = false;

struct GeVar { uint16_t body, band, iris, spotC; bool spot; };
static const GeVar GE_VAR[4] = {   // orange banded / yellow+white / white+black spots / pale pink
  { geC565(242,124, 38), geC565(252,236,206), geC565(250,178, 34), 0, false },
  { geC565(245,205, 70), geC565(255,255,255), geC565(245,175, 40), 0, false },
  { geC565(246,246,250), 0,                   geC565(245,175, 40), geC565(45,45,52), true },
  { geC565(243,201,212), geC565(255,244,248), geC565(246,170, 60), 0, false },
};

static void geSeedAll(uint32_t now) {
  for (int i = 0; i < GE_NE; i++) { GeEel& e = geEels[i];
    e.x = 240.0f * (i + 0.5f) / GE_NE + (geRnd() - 0.5f) * 14.4f; e.h = 240 * (0.46f + geRnd() * 0.22f);
    e.ph = geRnd() * 6.28f; e.extPh = geRnd() * 6.28f; e.extSp = 0.3f + geRnd() * 0.5f; e.mph = geRnd() * 6.28f;
    e.bend = (geRnd() - 0.5f) * 1.6f; e.waveK = 2 + geRnd() * 3; e.waveSp = 1.1f + geRnd() * 1.9f;
    e.waveAmp = 0.025f + geRnd() * 0.045f; e.wph = geRnd() * 6.28f; e.col = (uint8_t)i;
    e.gaze = (i == 0 || i % 4 == 3) ? 0 : (geRnd() < 0.5f ? -1 : 1); }
  for (GeKelp& k : geKelpA) { k.x = geRnd() * 240; k.h = 240 * (0.4f + geRnd() * 0.4f); k.ph = geRnd() * 6.28f; }
  for (GeBub& b : geBubA)  { b.x = geRnd() * 240; b.y = geRnd() * 240; b.r = geRnd() < 0.2f ? 4 + geRnd() * 4 : 1 + geRnd() * 2.5f; b.sp = 0.3f + geRnd() * 0.7f; b.ph = geRnd() * 6.28f; }
  for (GeJelly& j : geJellyA) { j.x = geRnd() * 240; j.y = 240 * (0.1f + geRnd() * 0.4f); j.ph = geRnd() * 6.28f;
    j.sp = (geRnd() < 0.5f ? 1 : -1) * (0.12f + geRnd() * 0.18f); j.hue = (uint8_t)random(256); j.glow = geRnd() < 0.6f; j.gph = geRnd() * 6.28f; }
  for (GeShad& s : geShadA) s.live = false;
  geCrab.live = false;
  geShadNext = now + 1500 + (uint32_t)(geRnd() * 3000);
  geCrabNext = now + 2500 + (uint32_t)(geRnd() * 4000);
  geSeeded = true;
}

// hermit crab home: 26 hand-drawn variants, axis-aligned GFX approximations of the
// lab's Canvas2D paths (beziers become ellipses/arcs; nothing tilts). fc mirrors x.
static void geHome(int cx, int cy, float S, int fc, uint8_t kind) {
  auto X = [&](float f) { return cx + (int)(f * S * fc); };
  auto Y = [&](float f) { return cy + (int)(f * S); };
  auto R = [&](float f) { int v = (int)(f * S); return v < 1 ? 1 : v; };
  switch (kind) {
    case 0:  for (int i = 0; i < 6; i++) { float tt = i / 5.0f; int rr = R(0.5f - tt * 0.4f);   // conch shell
               canvas->fillCircle(X(-0.08f + tt * 0.5f), Y(0.2f - tt * 0.62f), rr, geC565(246,223,162));
               canvas->drawCircle(X(-0.08f + tt * 0.5f), Y(0.2f - tt * 0.62f), rr, geC565(230,169,74)); } break;
    case 1:  geQuad(X(-0.42f),Y(-0.3f),X(0.42f),Y(-0.3f),X(0.3f),Y(0.35f),X(-0.3f),Y(0.35f),geC565(244,244,248));   // teacup
             canvas->drawLine(X(-0.42f),Y(-0.12f),X(0.42f),Y(-0.12f),geC565(208,96,96));
             canvas->drawLine(X(-0.42f),Y(-0.1f),X(0.42f),Y(-0.1f),geC565(208,96,96));
             canvas->fillArc(X(0.46f),Y(0),R(0.2f)+1,R(0.2f)-1,290,70,geC565(244,244,248)); break;
    case 2:  canvas->fillRect(X(-0.32f),Y(-0.45f),R(0.64f),R(0.9f),geC565(216,48,40));   // soda can
             canvas->fillRect(X(-0.32f),Y(-0.08f),R(0.64f),R(0.16f),geC565(236,238,242)); break;
    case 3:  canvas->fillRect(X(-0.4f),Y(-0.05f),R(0.8f),R(0.5f),geC565(236,210,162));   // little house
             canvas->fillTriangle(X(-0.48f),Y(-0.05f),X(0),Y(-0.5f),X(0.48f),Y(-0.05f),geC565(192,72,56));
             canvas->fillRect(X(-0.1f),Y(0.12f),R(0.2f),R(0.33f),geC565(106,68,36)); break;
    case 4:  canvas->fillEllipse(X(0),Y(-0.08f),R(0.42f),R(0.48f),geC565(255,242,160));   // lightbulb
             canvas->fillRect(X(-0.16f),Y(0.3f),R(0.32f),R(0.22f),geC565(154,154,160)); break;
    case 5:  geQuad(X(-0.4f),Y(-0.2f),X(0.4f),Y(-0.2f),X(0.28f),Y(0.42f),X(-0.28f),Y(0.42f),geC565(207,116,64));   // flower pot
             canvas->fillEllipse(X(0),Y(-0.3f),R(0.26f),R(0.2f),geC565(84,172,84)); break;
    case 6:  canvas->fillEllipse(X(0),Y(0.22f),R(0.26f),R(0.22f),geC565(154,82,40));   // violin (upright -- no tilt)
             canvas->fillEllipse(X(0),Y(-0.1f),R(0.2f),R(0.18f),geC565(154,82,40));
             canvas->fillRect(X(-0.045f),Y(-0.6f),R(0.09f),R(0.38f),geC565(58,30,14));
             canvas->drawLine(X(-0.04f),Y(-0.05f),X(-0.04f),Y(0.32f),geC565(238,238,238));
             canvas->drawLine(X(0.04f),Y(-0.05f),X(0.04f),Y(0.32f),geC565(238,238,238)); break;
    case 7:  canvas->fillEllipse(X(0),Y(0.24f),R(0.28f),R(0.24f),geC565(208,144,72));   // acoustic guitar
             canvas->fillEllipse(X(0),Y(-0.04f),R(0.2f),R(0.18f),geC565(208,144,72));
             canvas->fillCircle(X(0),Y(0.14f),R(0.08f),geC565(58,36,16));
             canvas->fillRect(X(-0.045f),Y(-0.62f),R(0.09f),R(0.4f),geC565(90,58,24)); break;
    case 8:  canvas->fillEllipse(X(0),Y(-0.16f),R(0.5f),R(0.32f),geC565(74,154,68));   // leaf parasol
             canvas->drawLine(X(0),Y(0.15f),X(0),Y(-0.42f),geC565(46,110,42));
             canvas->drawLine(X(0),Y(-0.12f),X(0.3f),Y(-0.2f),geC565(46,110,42));
             canvas->drawLine(X(0),Y(-0.12f),X(-0.3f),Y(-0.2f),geC565(46,110,42));
             canvas->drawLine(X(0),Y(0.15f),X(0),Y(0.46f),geC565(90,138,58)); break;
    case 9:  geQuad(X(0.12f),Y(-0.12f),X(0.5f),Y(-0.35f),X(0.5f),Y(0.35f),X(0.12f),Y(0.12f),geC565(232,184,58));   // trumpet
             canvas->fillRect(X(-0.48f),Y(-0.08f),R(0.6f),R(0.16f),geC565(232,184,58));
             for (int i = 0; i < 3; i++) canvas->fillRect(X(-0.28f + i * 0.12f),Y(-0.22f),R(0.05f),R(0.14f),geC565(176,138,32)); break;
    case 10: canvas->fillRect(X(-0.24f),Y(-0.45f),R(0.48f),R(0.62f),geC565(34,34,34));   // top hat
             canvas->fillEllipse(X(0),Y(0.2f),R(0.46f),R(0.1f),geC565(34,34,34));
             canvas->fillRect(X(-0.24f),Y(0.04f),R(0.48f),R(0.1f),geC565(176,48,64)); break;
    case 11: canvas->fillCircle(X(0),Y(0.05f),R(0.38f),geC565(232,84,58));   // alarm clock
             canvas->fillCircle(X(0),Y(0.05f),R(0.27f),geC565(255,255,255));
             canvas->drawLine(X(0),Y(0.05f),X(0),Y(-0.1f),geC565(51,51,51));
             canvas->drawLine(X(0),Y(0.05f),X(0.13f),Y(0.05f),geC565(51,51,51));
             canvas->fillEllipse(X(-0.28f),Y(-0.32f),R(0.12f),R(0.1f),geC565(232,84,58));
             canvas->fillEllipse(X(0.28f),Y(-0.32f),R(0.12f),R(0.1f),geC565(232,84,58)); break;
    case 12: geQuad(X(-0.3f),Y(0),X(0.3f),Y(0),X(0.22f),Y(0.4f),X(-0.22f),Y(0.4f),geC565(217,168,106));   // cupcake
             canvas->fillEllipse(X(0),Y(-0.1f),R(0.32f),R(0.24f),geC565(244,168,192));
             canvas->fillCircle(X(0),Y(-0.32f),R(0.08f),geC565(192,48,64)); break;
    case 13: canvas->fillRect(X(-0.13f),Y(-0.05f),R(0.26f),R(0.45f),geC565(244,240,224));   // toadstool
             canvas->fillArc(X(0),Y(-0.05f),R(0.4f),1,180,360,geC565(216,48,40));
             canvas->fillEllipse(X(-0.15f),Y(-0.15f),R(0.06f),R(0.05f),geC565(255,255,255));
             canvas->fillEllipse(X(0.12f),Y(-0.12f),R(0.05f),R(0.04f),geC565(255,255,255)); break;
    case 14: canvas->fillEllipse(X(0),Y(0.12f),R(0.34f),R(0.3f),geC565(90,160,192));   // teapot
             canvas->fillTriangle(X(-0.3f),Y(0.02f),X(-0.5f),Y(-0.12f),X(-0.44f),Y(0.04f),geC565(90,160,192));
             canvas->fillArc(X(0.4f),Y(0.12f),R(0.14f)+1,R(0.14f)-1,290,70,geC565(90,160,192));
             canvas->fillEllipse(X(0),Y(-0.18f),R(0.1f),R(0.06f),geC565(58,112,138)); break;
    case 15: canvas->fillRect(X(-0.34f),Y(-0.2f),R(0.68f),R(0.5f),geC565(216,72,120));   // gift box
             canvas->fillRect(X(-0.06f),Y(-0.2f),R(0.12f),R(0.5f),geC565(244,208,48));
             canvas->fillEllipse(X(-0.14f),Y(-0.24f),R(0.1f),R(0.08f),geC565(244,208,48));
             canvas->fillEllipse(X(0.14f),Y(-0.24f),R(0.1f),R(0.08f),geC565(244,208,48)); break;
    case 16: geQuad(X(-0.4f),Y(0.25f),X(-0.4f),Y(-0.1f),X(0.4f),Y(-0.1f),X(0.4f),Y(0.25f),geC565(244,200,56));   // crown
             canvas->fillTriangle(X(-0.4f),Y(-0.1f),X(-0.2f),Y(0.05f),X(0),Y(-0.25f),geC565(244,200,56));
             canvas->fillTriangle(X(0),Y(-0.25f),X(0.2f),Y(0.05f),X(0.4f),Y(-0.1f),geC565(244,200,56));
             canvas->fillCircle(X(0),Y(-0.02f),R(0.06f),geC565(224,64,80)); break;
    case 17: canvas->fillEllipse(X(0),Y(-0.02f),R(0.4f),R(0.38f),geC565(244,168,200));   // donut
             canvas->fillCircle(X(0),Y(0),R(0.14f),geC565(122,74,32));
             canvas->drawLine(X(-0.2f),Y(-0.1f),X(-0.14f),Y(-0.04f),geC565(244,208,64));
             canvas->drawLine(X(0.15f),Y(-0.12f),X(0.21f),Y(-0.06f),geC565(64,160,240)); break;
    case 18: canvas->fillEllipse(X(-0.05f),Y(0.18f),R(0.34f),R(0.24f),geC565(244,208,48));   // rubber duck
             canvas->fillCircle(X(0.2f),Y(-0.1f),R(0.2f),geC565(244,208,48));
             canvas->fillTriangle(X(0.38f),Y(-0.1f),X(0.52f),Y(-0.04f),X(0.38f),Y(0.02f),geC565(240,136,40));
             canvas->fillCircle(X(0.24f),Y(-0.15f),1,geC565(0,0,0)); break;
    case 19: canvas->fillRect(X(-0.3f),Y(-0.3f),R(0.6f),R(0.6f),geC565(244,244,248));   // die
             canvas->drawRect(X(-0.3f),Y(-0.3f),R(0.6f),R(0.6f),geC565(176,176,184));
             for (int p = 0; p < 5; p++) { static const float px[5] = {-0.15f,0,0.15f,0.15f,-0.15f}, py[5] = {-0.15f,0,0.15f,-0.15f,0.15f};
               canvas->fillCircle(X(px[p]),Y(py[p]),R(0.05f),geC565(51,51,51)); } break;
    case 20: canvas->fillArc(X(0),Y(-0.08f),R(0.42f),R(0.24f),300,120,geC565(244,212,58));   // banana (crescent arc)
             break;
    case 21: canvas->fillTriangle(X(0),Y(-0.42f),X(0.28f),Y(0.3f),X(-0.28f),Y(0.3f),geC565(240,106,32));   // traffic cone
             geQuad(X(-0.14f),Y(-0.02f),X(0.14f),Y(-0.02f),X(0.19f),Y(0.12f),X(-0.19f),Y(0.12f),geC565(255,255,255));
             canvas->fillRect(X(-0.36f),Y(0.3f),R(0.72f),R(0.1f),geC565(240,106,32)); break;
    case 22: canvas->fillTriangle(X(0),Y(0.42f),X(-0.35f),Y(-0.35f),X(0.35f),Y(-0.35f),geC565(240,200,96));   // pizza slice
             canvas->fillRect(X(-0.36f),Y(-0.42f),R(0.72f),R(0.1f),geC565(208,128,64));
             canvas->fillCircle(X(-0.08f),Y(-0.15f),R(0.06f),geC565(208,48,40));
             canvas->fillCircle(X(0.1f),Y(-0.04f),R(0.06f),geC565(208,48,40));
             canvas->fillCircle(X(0),Y(0.16f),R(0.05f),geC565(208,48,40)); break;
    case 23: canvas->fillRect(X(-0.46f),Y(-0.18f),R(0.92f),R(0.48f),geC565(51,51,51));   // boombox
             canvas->fillArc(X(0),Y(-0.18f),R(0.2f)+1,R(0.2f)-1,180,360,geC565(119,119,119));
             canvas->fillCircle(X(-0.22f),Y(0.07f),R(0.13f),geC565(136,136,136));
             canvas->fillCircle(X(0.22f),Y(0.07f),R(0.13f),geC565(136,136,136));
             canvas->fillCircle(X(-0.22f),Y(0.07f),R(0.055f),geC565(34,34,34));
             canvas->fillCircle(X(0.22f),Y(0.07f),R(0.055f),geC565(34,34,34)); break;
    case 24: canvas->fillTriangle(X(0),Y(-0.46f),X(0.3f),Y(0.3f),X(-0.3f),Y(0.3f),geC565(224,64,128));   // party hat
             canvas->drawLine(X(-0.2f),Y(0.1f),X(0.2f),Y(0.1f),geC565(244,208,64));
             canvas->drawLine(X(-0.1f),Y(-0.16f),X(0.1f),Y(-0.16f),geC565(244,208,64));
             canvas->fillCircle(X(0),Y(-0.46f),R(0.1f),geC565(244,208,64)); break;
    default: canvas->fillRect(X(-0.1f),Y(-0.4f),R(0.2f),R(0.55f),geC565(58,154,74));   // cactus in a pot
             canvas->fillRect(X(-0.28f),Y(-0.1f),R(0.12f),R(0.24f),geC565(58,154,74));
             canvas->fillRect(X(0.16f),Y(-0.2f),R(0.12f),R(0.28f),geC565(58,154,74));
             canvas->fillCircle(X(0),Y(-0.42f),R(0.08f),geC565(224,64,80));
             geQuad(X(-0.24f),Y(0.15f),X(0.24f),Y(0.15f),X(0.18f),Y(0.42f),X(-0.18f),Y(0.42f),geC565(207,116,64)); break;
  }
}

static void geMouth(int cx, int cy, float s, int kind) {
  uint16_t mc = geC565(90, 30, 30);
  switch (kind) {
    case 0: canvas->fillEllipse(cx, cy, (int)(s*0.26f)+1, (int)(s*0.34f)+1, mc);                      // open "O" + tongue
            canvas->fillEllipse(cx, cy+(int)(s*0.12f), (int)(s*0.14f)+1, (int)(s*0.14f)+1, geC565(230,120,120)); break;
    case 1: canvas->fillEllipse(cx, cy, (int)(s*0.13f)+1, (int)(s*0.16f)+1, mc); break;               // small o
    case 2: canvas->fillArc(cx, cy-(int)(s*0.12f), (int)(s*0.26f)+1, (int)(s*0.26f)+1, 20, 160, mc); break;  // smile (r1==r2 = thin stroke, not a filled wedge)
    case 3: canvas->drawLine(cx-(int)(s*0.18f), cy, cx, cy-(int)(s*0.12f), mc);                       // :3 wavy
            canvas->drawLine(cx, cy-(int)(s*0.12f), cx+(int)(s*0.18f), cy, mc); break;
    default: canvas->drawLine(cx-(int)(s*0.16f), cy, cx+(int)(s*0.16f), cy, mc); break;               // neutral
  }
}

static void renderGardenEels(uint32_t now) {
  uint32_t st = now * 9 / 20;                    // the lab runs EVERY effect on scaled time (TS=0.45 in led-matrix-lab.html)
  if (!geSeeded) geSeedAll(st);
  uint16_t* fb = canvas->getFramebuffer();
  float T = st * 0.003f;                         // lab t*0.001 * current default 3

  // water gradient, row LUT computed once: #0a6a86 -> #064a63 @0.7 -> #05364a
  static uint16_t geRow[240]; static bool rowInit = false;
  if (!rowInit) { for (int y = 0; y < 240; y++) { float f = y / 239.0f;
      int r, g, b;
      if (f < 0.7f) { float u = f / 0.7f; r = 10 + (int)((6-10)*u); g = 106 + (int)((74-106)*u); b = 134 + (int)((99-134)*u); }
      else          { float u = (f-0.7f)/0.3f; r = 6 + (int)((5-6)*u); g = 74 + (int)((54-74)*u); b = 99 + (int)((74-99)*u); }
      geRow[y] = geC565(r, g, b); } rowInit = true; }
  for (int y = 0; y < 240; y++) { uint16_t c = geRow[y]; uint16_t* p = fb + y * 240; for (int x = 0; x < 240; x++) p[x] = c; }

  // god rays: 3 translucent trapezoids down to the sand
  const uint16_t rayC = geC565(191, 239, 255);
  for (int i = 0; i < 3; i++) { float sx = 240 * (0.25f + i * 0.25f) + sinf(T * 0.3f + i) * 10;
    for (int y = 0; y < (int)GE_SAND; y += 2) {          // every other row: alpha .05 reads the same, half the cost
      float f = y / GE_SAND; int xl = (int)(sx - 6 + f * 22), xr = (int)(sx + 6 + f * 20);
      for (int x = xl; x <= xr; x++) geBlendPx(fb, x, y, rayC, 2); } }

  // kelp behind everything
  const uint16_t kelpC = geC565(20, 102, 81);    // rgba(30,120,70,.6) pre-blended vs mid-water
  for (GeKelp& k : geKelpA) { int px = (int)k.x, py = (int)GE_SAND;
    for (int s = 1; s <= 10; s++) { float f = s / 10.0f;
      int yy = (int)(GE_SAND - f * k.h), xx = (int)(k.x + sinf(T * 1.2f + f * 3 + k.ph) * f * 12);
      geLine3(px, py, xx, yy, kelpC); px = xx; py = yy; } }

  // fish shadows drifting by in the far background
  if (st >= geShadNext) { int n = 2 + random(4); int dir = random(2) ? 1 : -1; float y0 = 240 * (0.12f + geRnd() * 0.4f);
    int placed = 0;
    for (GeShad& s : geShadA) { if (s.live || placed >= n) continue;
      s.x = dir > 0 ? -30.0f - placed * 22 : 270.0f + placed * 22; s.y = y0 + (geRnd() - 0.5f) * 19.2f;
      s.sp = 0.6f + geRnd() * 0.5f; s.ph = geRnd() * 6.28f; s.sz = 7.2f + geRnd() * 4.8f; s.dir = (int8_t)dir; s.live = true; placed++; }
    geShadNext = st + 5000 + (uint32_t)(geRnd() * 7000); }
  const uint16_t shadC = geC565(14, 74, 94);     // rgba(18,48,62,.55) pre-blended vs water
  for (GeShad& s : geShadA) { if (!s.live) continue;
    s.x += s.dir * s.sp; s.y += sinf(T * 3 + s.ph) * 0.3f;
    if ((s.dir > 0 && s.x > 285) || (s.dir < 0 && s.x < -45)) { s.live = false; continue; }
    int x = (int)s.x, y = (int)s.y, sz = (int)s.sz, d = s.dir;
    canvas->fillEllipse(x, y, sz, (int)(s.sz * 0.45f), shadC);
    canvas->fillTriangle(x - d * (int)(s.sz * 0.8f), y, x - d * (int)(s.sz * 1.5f), y - (int)(s.sz * 0.45f),
                         x - d * (int)(s.sz * 1.5f), y + (int)(s.sz * 0.45f), shadC); }

  // jellyfish: blended glow + dome, pre-blended tentacles
  for (GeJelly& j : geJellyA) { j.x += j.sp; j.y += sinf(T * 0.6f + j.ph) * 0.3f;
    if (j.x < -30) j.x = 270; else if (j.x > 270) j.x = -30;
    const float rw = 12.0f, rh = 10.8f; float pulse = 0.5f + 0.5f * sinf(T * 2 + j.ph);
    uint16_t rc = activePaletteLUT[(uint8_t)(j.hue + 40)];
    if (j.glow) { float gl = 0.4f + 0.6f * (0.5f + 0.5f * sinf(T * 2.5f + j.gph));
      geBlendEllipse(fb, j.x, j.y, rw * 2.4f, rh * 2.4f, rc, (uint8_t)(1 + gl * 1.5f), false);
      geBlendEllipse(fb, j.x, j.y - rh * 0.3f, rw * 1.1f, rh * 1.1f, geC565(210,255,240), (uint8_t)(1 + gl * 2), false); }
    uint16_t cc = geBlend(rc, 0xFFFF, 8);        // the lab's (c+255)/2 lighten
    geBlendEllipse(fb, j.x, j.y, rw * (0.9f + pulse * 0.2f), rh, cc, 3, true);
    uint16_t tc = geBlend(geRow[j.y < 0 ? 0 : (j.y > 239 ? 239 : (int)j.y)], cc, 4);
    for (int a = 0; a < 5; a++) { float ox = j.x - rw + a * rw * 0.5f, px = ox, py = j.y;
      for (int s2 = 1; s2 <= 6; s2++) { float nx = ox + sinf(T * 1.5f + s2 + a) * 3, ny = j.y + s2 * rh * 0.7f;
        canvas->drawLine((int)px, (int)py, (int)nx, (int)ny, tc); px = nx; py = ny; } } }

  // the eels
  const uint16_t oc = geC565(26, 26, 26);
  for (GeEel& e : geEels) {
    const GeVar& v = GE_VAR[e.col & 3];
    float ext = 0.55f + 0.5f * sinf(T * e.extSp + e.extPh); if (ext < 0.28f) ext = 0.28f; if (ext > 0.82f) ext = 0.82f;
    const int N = 14; float H = e.h * ext;
    float waviness = 0.3f + 0.7f * (0.5f + 0.5f * sinf(T * 0.35f + e.wph));
    float ptx[N + 1], pty[N + 1];
    for (int s = 0; s <= N; s++) { float f = (float)s / N;
      pty[s] = GE_SAND - f * H + (1 - f) * 12.0f;   // base buried below the sand line
      ptx[s] = e.x + e.bend * f * f * 19.2f + sinf(T * 0.9f + e.ph) * f * 7.2f
             + sinf(f * e.waveK - T * e.waveSp + e.ph) * f * 240.0f * e.waveAmp * waviness; }
    const float w0 = 8.88f;                        // res*0.037
    auto W = [&](int s) { return w0 * (1.0f - ((float)s / N) * 0.08f); };
    float topW = W(N); float headR = topW;
    int hx = (int)ptx[N], hy = (int)pty[N];
    bool banded = !v.spot;
    // outline BEHIND the fill: draw the whole silhouette 2px wider in outline color, then the colored
    // fill on top. Fill half-width W is strictly < outline half-width W+ob at every cross-section (same
    // centerline + endpoints), so color can never spill past the outline -- unlike the old per-segment
    // geLine3 flanks, which only thickened horizontally and leaked on the wavy/tilted parts.
    const float ob = 2.0f;
    canvas->fillCircle(hx, hy, (int)headR + 2, oc);                        // cap outline
    for (int s = 0; s < N; s++) { float w1 = W(s) + ob, w2 = W(s + 1) + ob;   // silhouette outline
      geQuad((int)(ptx[s] - w1), (int)pty[s], (int)(ptx[s+1] - w2), (int)pty[s+1],
             (int)(ptx[s+1] + w2), (int)pty[s+1], (int)(ptx[s] + w1), (int)pty[s], oc); }
    canvas->fillCircle(hx, hy, (int)headR, banded ? v.band : v.body);      // cap fill
    for (int s = 0; s < N; s++) { float w1 = W(s), w2 = W(s + 1);          // body fill
      bool isBand = banded && (((int)(((float)s / N) * 5) % 2) == 0);
      geQuad((int)(ptx[s] - w1), (int)pty[s], (int)(ptx[s+1] - w2), (int)pty[s+1],
             (int)(ptx[s+1] + w2), (int)pty[s+1], (int)(ptx[s] + w1), (int)pty[s], isBand ? v.band : v.body); }
    if (v.spot) for (int s = 1; s < N; s++) { if (((s * 3 + 2) % 4) < 2) {
        float off = (((s * 13) % 7) - 3) / 3.0f * W(s) * 0.6f; int rr = (int)(240 * (0.006f + ((s * 5) % 3) * 0.005f));
        canvas->fillEllipse((int)(ptx[s] + off), (int)pty[s], rr, rr, v.spotC); } }
    int kind = ((int)floorf(st * 0.0006f + e.mph)) % 5; if (kind < 0) kind += 5;
    if (e.gaze == 0) {                             // front-facing: blush + two sparkle eyes + expression
      float ey = hy - topW * 0.28f, ex = topW * 0.4f, er = topW * 0.32f;
      uint16_t blush = geC565(250, 140, 165);
      canvas->fillEllipse((int)(hx - ex), (int)(ey + topW * 0.52f), (int)(topW * 0.24f), (int)(topW * 0.14f) + 1, blush);
      canvas->fillEllipse((int)(hx + ex), (int)(ey + topW * 0.52f), (int)(topW * 0.24f), (int)(topW * 0.14f) + 1, blush);
      for (int sgn = -1; sgn <= 1; sgn += 2) { int cx0 = (int)(hx + sgn * ex);
        canvas->fillCircle(cx0, (int)ey, (int)er, 0xFFFF);
        canvas->fillCircle(cx0, (int)ey, (int)(er * 0.68f), v.iris);
        canvas->fillCircle(cx0, (int)ey, (int)(er * 0.4f), oc);
        canvas->drawPixel(cx0 - 1, (int)ey - 1, 0xFFFF);
        canvas->drawCircle(cx0, (int)ey, (int)er, oc); }
      geMouth(hx, (int)(hy + topW * 0.18f), topW * 0.8f, kind);
    } else {                                       // profile: one small eye, no mouth
      float er = topW * 0.3f; int exx = (int)(hx + e.gaze * topW * 0.34f), eyy = (int)(hy - topW * 0.42f);
      canvas->fillCircle(exx, eyy, (int)er, 0xFFFF);
      canvas->fillCircle(exx, eyy, (int)(er * 0.68f), v.iris);
      canvas->fillCircle(exx, eyy, (int)(er * 0.4f), oc);
      canvas->drawPixel(exx - 1, eyy - 1, 0xFFFF);
      canvas->drawCircle(exx, eyy, (int)er, oc);
    }
  }

  // sand over the eel bases, then speckles
  const uint16_t sandC = geC565(200, 180, 138), spkC = geC565(180, 160, 119);
  for (int x = 0; x < 240; x++) { int top = (int)(GE_SAND + sinf(x * 0.05f) * 2.4f);
    canvas->drawFastVLine(x, top, 240 - top, sandC); }
  for (int i = 0; i < 30; i++) { int sx2 = (i * 53) % 240, sy2 = (int)GE_SAND + ((i * 29) % (240 - (int)GE_SAND));
    canvas->fillRect(sx2, sy2, 2, 2, spkC); }

  // hermit crab: walks in off-screen, roams 50-70s, wanders off, and a fresh one (new home)
  // shows up after a break -- the lab version stayed forever, but a unit runs for hours and
  // the rotating homes are the payoff
  const float S = 20.4f;                           // res*0.085 * the drawHome 1.2 scale where noted
  if (!geCrab.live && st >= geCrabNext) { bool fromLeft = random(2); float yT = GE_SAND - S * 0.42f, yB = 240 - S * 0.5f;
    GeCrab& c = geCrab; c.x = fromLeft ? -40.0f : 280.0f; c.y = yT + geRnd() * (yB - yT);
    c.tx = fromLeft ? 240 * (0.15f + geRnd() * 0.3f) : 240 * (0.55f + geRnd() * 0.3f); c.ty = yT + geRnd() * (yB - yT);
    c.sp = 0.3f + geRnd() * 0.25f; c.ph = geRnd() * 6.28f; c.nextTurn = st + 2800 + (uint32_t)(geRnd() * 2000);
    c.leaveAt = st + 22500 + (uint32_t)(geRnd() * 9000);         // 50-70 s wall clock (st ticks at 0.45x)
    c.face = fromLeft ? 1 : -1; c.home = (uint8_t)random(26); c.back = false; c.live = true; c.leaving = false; }
  if (geCrab.live) { GeCrab& c = geCrab; float yT = GE_SAND - S * 0.42f, yB = 240 - S * 0.5f;
    if (!c.leaving && st >= c.leaveAt) { c.leaving = true; c.back = false;
      c.tx = c.x < 120 ? -60.0f : 300.0f; c.ty = c.y; }          // walk out the nearest side
    if (c.leaving && (c.x < -50 || c.x > 290)) { c.live = false;
      geCrabNext = st + 3600 + (uint32_t)(geRnd() * 5400); }     // 8-20 s wall, then a new visitor
    if (!c.leaving && st >= c.nextTurn) { float r = geRnd();
      if (r < 0.4f) { float nx2 = c.x + (random(2) ? 1 : -1) * 240 * (0.15f + geRnd() * 0.2f);
        c.tx = nx2 < 12 ? 12 : (nx2 > 228 ? 228 : nx2); c.nextTurn = st + 600 + (uint32_t)(geRnd() * 600); c.back = false; }
      else if (r < 0.85f) { c.tx = 240 * (0.08f + geRnd() * 0.84f); c.nextTurn = st + 1600 + (uint32_t)(geRnd() * 1600); c.back = false; }
      else { c.tx = 240 * (0.1f + geRnd() * 0.8f); c.nextTurn = st + 1000 + (uint32_t)(geRnd() * 1000); c.back = geRnd() < 0.6f; }
      c.ty = yT + geRnd() * (yB - yT); }
    float dx = c.tx - c.x, dy = c.ty - c.y, dist = sqrtf(dx * dx + dy * dy); if (dist < 0.001f) dist = 1;
    bool moving = dist > 3.6f;
    if (moving) { c.x += dx / dist * c.sp; c.y += dy / dist * c.sp; c.face = c.back ? -(dx >= 0 ? 1 : -1) : (dx >= 0 ? 1 : -1); }
    else if (c.nextTurn > st + 120) c.nextTurn = st + 120;
    int cx = (int)c.x, cy = (int)(c.y + (moving ? sinf(st * 0.03f + c.ph) * 0.96f : 0));
    geHome(cx, (int)(cy - S * 0.38f), S * 1.2f, 1, c.home);   // fixed orientation: the home rides the back, does not flip when the crab reverses (was c.face -> asymmetric homes snapped side to side)
    const uint16_t legC = geC565(207, 90, 36), bodyC = geC565(242, 116, 58);
    for (int l = -2; l <= 2; l++) { float lp = moving ? sinf(st * 0.05f + l + c.ph) * S * 0.08f : 0;
      canvas->drawLine(cx + (int)(l * S * 0.13f), cy + (int)(S * 0.3f), cx + (int)(l * S * 0.13f + lp), cy + (int)(S * 0.5f), legC);
      canvas->drawLine(cx + (int)(l * S * 0.13f) + 1, cy + (int)(S * 0.3f), cx + (int)(l * S * 0.13f + lp) + 1, cy + (int)(S * 0.5f), legC); }
    float open = 0.12f + 0.4f * (0.5f + 0.5f * sinf(st * 0.012f + c.ph));   // pincers open/close
    for (int side = -1; side <= 1; side += 2) { int bx = cx + (int)(side * S * 0.46f), by = cy + (int)(S * 0.14f);
      for (int f2 = -1; f2 <= 1; f2 += 2) { int fx2 = bx + side * (int)(S * 0.2f), fy2 = by + f2 * (int)(open * S * 0.35f);
        canvas->fillEllipse(fx2, fy2, (int)(S * 0.18f), (int)(S * 0.09f) + 1, bodyC);
        canvas->drawEllipse(fx2, fy2, (int)(S * 0.18f), (int)(S * 0.09f) + 1, legC); }
      canvas->fillEllipse(bx, by, (int)(S * 0.14f), (int)(S * 0.13f), bodyC);
      canvas->drawEllipse(bx, by, (int)(S * 0.14f), (int)(S * 0.13f), legC); }
    canvas->fillEllipse(cx, cy + (int)(S * 0.12f), (int)(S * 0.4f), (int)(S * 0.36f), bodyC);
    canvas->drawEllipse(cx, cy + (int)(S * 0.12f), (int)(S * 0.4f), (int)(S * 0.36f), legC);
    // eyes at source scale (1px radii) and a THIN smile arc (r1==r2): the fattened +1 eyes plus a
    // filled 2px annulus merged into a solid black heart on the face (crab_sim.py repro, 2026-07-16)
    canvas->fillEllipse(cx - (int)(S * 0.14f), cy + (int)(S * 0.04f), (int)(S * 0.055f), (int)(S * 0.07f), 0x0000);
    canvas->fillEllipse(cx + (int)(S * 0.14f), cy + (int)(S * 0.04f), (int)(S * 0.055f), (int)(S * 0.07f), 0x0000);
    canvas->fillArc(cx, cy + (int)(S * 0.14f), (int)(S * 0.12f), (int)(S * 0.12f), 25, 155, 0x0000); }

  // rising bubbles on top of everything
  const uint16_t bubC = geC565(100, 135, 150);
  for (GeBub& b : geBubA) { b.y -= b.sp * 1.5f; b.x += sinf(T * 2 + b.ph) * 0.3f;
    if (b.y < -2) { b.y = 242; b.x = geRnd() * 240; }
    canvas->drawCircle((int)b.x, (int)b.y, (int)b.r, bubC); }
}

// SWIRL (id 45): partner's "procedural fluid" -- iterated domain warp (3 octaves) on the shared
// 256-entry sin LUT, ported 1:1 from references/catsoup_pkg/effects.js ("swirl"; warp 12 /
// speed 3 baked, ocean palette, lab TS=0.45 time scale). Renders the lab's 64x64 logical grid
// (integer-only, ~4k px) into an 8 KB heap buffer kept after first entry (Echo's polar-LUT
// precedent), then separable-bilinear upscales into the whole framebuffer using the
// 0x07E0F81F spread format -- every fb pixel is written, so loop() skips the clear.
constexpr int SWIRL_RES = 64;
static uint16_t* swirlBuf = nullptr;

static inline uint32_t swirlSpread(uint16_t c) { return ((uint32_t)c | ((uint32_t)c << 16)) & 0x07E0F81Fu; }
static inline uint32_t swirlLerp(uint32_t a, uint32_t b, uint32_t w) {   // spread-format lerp, w in 0..16
  return ((a * (16 - w) + b * w) >> 4) & 0x07E0F81Fu;
}

static void renderSwirl(uint32_t now) {
  static uint16_t pal[256]; static bool palInit = false;
  if (!palInit) {   // lab PAL.ocean stops: deep blue -> mid blue -> teal -> white-cyan
    const uint8_t stops[4][4] = {{0,0,4,20},{110,0,60,120},{190,0,170,190},{255,200,255,255}};
    for (int i = 0; i < 256; i++) {
      int s = 0; while (s < 2 && i > stops[s+1][0]) s++;
      int span = stops[s+1][0] - stops[s][0]; if (span < 1) span = 1;
      int f = i - stops[s][0];
      pal[i] = geC565(stops[s][1] + (stops[s+1][1]-stops[s][1]) * f / span,
                      stops[s][2] + (stops[s+1][2]-stops[s][2]) * f / span,
                      stops[s][3] + (stops[s+1][3]-stops[s][3]) * f / span);
    }
    palInit = true;
  }
  if (!swirlBuf) { swirlBuf = (uint16_t*)malloc(SWIRL_RES * SWIRL_RES * 2); if (!swirlBuf) return; }

  uint32_t T = ((now * 9 / 20) * 3) >> 5;        // lab (t*speed)>>5: speed 3, t on the 0.45x lab clock
  const int W = 12;                              // "turbulence" default
  for (int y = 0; y < SWIRL_RES; y++)
    for (int x = 0; x < SWIRL_RES; x++) {
      int q1 = fastSin(x*3 + T) + fastCos(y*4 - (T>>1)), q2 = fastSin(y*3 - T) + fastCos(x*4 + (T>>1));
      int wx = x + ((q1 * W) >> 6), wy = y + ((q2 * W) >> 6);
      int r1 = fastSin(wx*3 + (T>>1)) + fastCos(wy*5 - T), r2 = fastSin(wy*3 - (T>>1)) + fastCos(wx*5 + T);
      int fx = x + ((r1 * W) >> 6), fy = y + ((r2 * W) >> 6);
      int v = fastSin(fx*2 + T) + fastCos(fy*2 - (T>>1)) + fastSin((fx+fy)*2 + (T>>2));
      swirlBuf[y * SWIRL_RES + x] = pal[(((v + 381) * 85) >> 8) & 255];
    }

  // separable bilinear 64 -> 240: one vertical lerp per source column per row, then horizontal
  static uint8_t mi[240], mi2[240], mw[240]; static bool mapInit = false;
  if (!mapInit) {
    for (int o = 0; o < 240; o++) {
      uint32_t s16 = (uint32_t)o * (SWIRL_RES - 1) * 16 / 239;   // 4.4 fixed-point source coord
      mi[o] = s16 >> 4; mw[o] = s16 & 15;
      mi2[o] = mi[o] + 1 < SWIRL_RES ? mi[o] + 1 : mi[o];        // clamped (weight is 0 at the edge)
    }
    mapInit = true;
  }
  uint16_t* fb = canvas->getFramebuffer();
  uint32_t vrow[SWIRL_RES];
  for (int oy = 0; oy < 240; oy++) {
    const uint16_t* r0 = swirlBuf + mi[oy] * SWIRL_RES;
    const uint16_t* r1 = swirlBuf + mi2[oy] * SWIRL_RES;
    uint32_t wy = mw[oy];
    for (int i = 0; i < SWIRL_RES; i++) vrow[i] = swirlLerp(swirlSpread(r0[i]), swirlSpread(r1[i]), wy);
    uint16_t* out = fb + oy * 240;
    for (int ox = 0; ox < 240; ox++) {
      uint32_t c = swirlLerp(vrow[mi[ox]], vrow[mi2[ox]], mw[ox]);
      out[ox] = (uint16_t)((c & 0xFFFF) | (c >> 16));
    }
  }
}

// ===================== ATLAS: creative-coding lab effects (ids 49..55) =====================
// Ported from creative_coding/c++/effects.js. The px-grid effects (Julia, Interference, Munching
// Squares) write a res x res RGB565 grid into the shared fxBuf, then blitUp() bilinear-upscales it
// into the whole 240 framebuffer -- the same pipeline as renderSwirl (reusing swirlSpread/swirlLerp).
// Wireframe Globe is a draw() effect: it plots dots straight to the canvas. Lab palettes are baked
// here from the SAME stop lists as lab-core.js PAL, kept as 24-bit 0xRRGGBB; to565() converts at the
// last step. The lab clock is now*9/20 (0.45x, see swirl). ponytail: params are the lab defaults
// baked as constants (no on-device sliders); clear stays on. Globe morph is QMI8658 tilt-driven on
// device (in-plane gravity -> radial swell), falling back to the browser's time-based burst if no IMU.
static inline int   acli(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float aclf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint32_t to24(int r, int g, int b) { return ((uint32_t)(r&255)<<16)|((g&255)<<8)|(b&255); }
static inline uint16_t to565(uint32_t c) { return geC565((c>>16)&255, (c>>8)&255, c&255); }
static uint16_t hsv565(float h, float s, float v) {   // p5 HSB source: h 0..360, s/v 0..1
  h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
  float c = v*s, x = c*(1 - fabsf(fmodf(h/60.0f, 2) - 1)), m = v - c, r, g, b;
  int seg = (int)(h/60.0f);
  switch (seg) { case 0: r=c;g=x;b=0; break; case 1: r=x;g=c;b=0; break; case 2: r=0;g=c;b=x; break;
                 case 3: r=0;g=x;b=c; break; case 4: r=x;g=0;b=c; break; default: r=c;g=0;b=x; break; }
  return geC565((int)((r+m)*255), (int)((g+m)*255), (int)((b+m)*255));
}

static uint32_t PAL_plasma[256], PAL_escape[256], PAL_rainbow[256], PAL_stained[256];   // Globe/Fermat dropped their baked palettes for the config palette engine (pColor)
static void labBake(uint32_t* pal, const uint8_t* s, int n) {   // s = n rows of {pos,r,g,b}; linear between bracketing stops (== lab makePalette)
  for (int i = 0; i < 256; i++) {
    int k = 0; for (; k < n-1; k++) if (i >= s[k*4] && i <= s[(k+1)*4]) break;
    if (k > n-2) k = n-2;
    const uint8_t* a = s + k*4; const uint8_t* b = s + (k+1)*4;
    int span = b[0]-a[0]; if (span < 1) span = 1; int f = i - a[0];
    pal[i] = to24(a[1]+(b[1]-a[1])*f/span, a[2]+(b[2]-a[2])*f/span, a[3]+(b[3]-a[3])*f/span);
  }
}
static void atlasInit() {
  static bool done = false; if (done) return; done = true;
  static const uint8_t ST_plasma[]  ={0,20,10,60, 70,150,20,140, 140,235,60,80, 200,250,190,50, 255,255,245,190};
  static const uint8_t ST_escape[]  ={0,0,0,8, 40,20,10,90, 110,10,120,160, 170,230,120,60, 220,255,220,120, 255,0,0,0};
  static const uint8_t ST_stained[] ={0,10,4,30, 60,180,20,40, 120,20,60,200, 190,20,180,140, 255,240,220,90};
  labBake(PAL_plasma, ST_plasma, 5); labBake(PAL_escape, ST_escape, 6); labBake(PAL_stained, ST_stained, 5);
  for (int i = 0; i < 256; i++) {   // rainbow: HSV sweep, matches lab-core.js PAL.rainbow
    float h = i/256.0f*6, x = 1 - fabsf(fmodf(h,2)-1); int sx = (int)h; float r,g,b;
    if (sx==0){r=1;g=x;b=0;} else if (sx==1){r=x;g=1;b=0;} else if (sx==2){r=0;g=1;b=x;}
    else if (sx==3){r=0;g=x;b=1;} else if (sx==4){r=x;g=0;b=1;} else {r=1;g=0;b=x;}
    PAL_rainbow[i] = to24((int)(r*255),(int)(g*255),(int)(b*255));
  }
}

static uint16_t* fxBuf = nullptr;   // shared res x res grid, sized to the largest (julia 140^2)
static void blitUp(int res, bool smooth) {   // generalizes renderSwirl's 64->240 upscale to any res<=240
  uint16_t* fb = canvas->getFramebuffer();
  static uint8_t mi[240], mi2[240], mw[240];
  for (int o = 0; o < 240; o++) {
    uint32_t s16 = (uint32_t)o * (res-1) * 16 / 239;
    if (smooth) { mi[o] = s16>>4; mw[o] = s16 & 15; mi2[o] = mi[o]+1 < res ? mi[o]+1 : mi[o]; }
    else { int r = (s16+8)>>4; if (r >= res) r = res-1; mi[o] = r; mw[o] = 0; mi2[o] = r; }   // nearest for smooth:false effects
  }
  uint32_t vrow[240];
  for (int oy = 0; oy < 240; oy++) {
    const uint16_t* r0 = fxBuf + mi[oy]*res; const uint16_t* r1 = fxBuf + mi2[oy]*res; uint32_t wy = mw[oy];
    for (int i = 0; i < res; i++) vrow[i] = swirlLerp(swirlSpread(r0[i]), swirlSpread(r1[i]), wy);
    uint16_t* out = fb + oy*240;
    for (int ox = 0; ox < 240; ox++) {
      uint32_t c = swirlLerp(vrow[mi[ox]], vrow[mi2[ox]], mw[ox]);
      out[ox] = (uint16_t)((c & 0xFFFF) | (c >> 16));
    }
  }
}

static void renderJulia(uint32_t now) {   // res 96 -- escape-time Julia, c orbits a small circle (blitUp upscales to 240)
  const int res = 96; uint32_t t = now*9/20; int T = (int)((t*3)>>7);
  float cRe = fastSin(T)*0.006f, cIm = fastCos(T)*0.006f;
  const float hw = 1.5f, ctr = res/2.0f;   // centered on the grid, sized to fill the round panel
  for (int y = 0; y < res; y++) for (int x = 0; x < res; x++) {
    float zr = (x-ctr)/ctr*hw, zi = (y-ctr)/ctr*hw, m2 = 0; int i = 0;
    while (i < 40) { float nr = zr*zr-zi*zi+cRe, ni = 2*zr*zi+cIm; zr = nr; zi = ni; m2 = zr*zr+zi*zi; if (m2 > 64) break; i++; }
    uint16_t col;
    if (i >= 40) col = to565(PAL_escape[255]);
    else { float mu = i + 1.0f - 64.0f/m2; col = to565(PAL_escape[acli((int)(mu/40*255),0,255)]); }   // cheap continuous shade in [i,i+1) -- was per-pixel logf(logf(sqrt)), ~2 transcendentals/pixel
    fxBuf[y*res+x] = col;
  }
  blitUp(res, true);
}
static void renderInterference(uint32_t now) {
  const int res = 64; uint32_t t = now*9/20; int T = (int)((t*3)>>4); const int k = 6, n = 2;
  static const int SRC[4][2] = {{16,16},{48,48},{48,16},{16,48}};
  for (int y = 0; y < res; y++) for (int x = 0; x < res; x++) {
    int v = 0; for (int i = 0; i < n; i++) { int dx = x-SRC[i][0], dy = y-SRC[i][1]; int dist = (int)(sqrtf((float)(dx*dx+dy*dy))*2); v += fastSin(dist*k-T); }
    fxBuf[y*res+x] = to565(PAL_plasma[acli((v+n*127)*255/(n*254),0,255)]);
  }
  blitUp(res, true);
}
static void renderXormunch(uint32_t now) {
  const int res = 64; uint32_t t = now*9/20; const int mask = 4; int tt = (int)(t>>4);
  for (int y = 0; y < res; y++) for (int x = 0; x < res; x++) {
    int v = ((x^y)+tt) & 255; fxBuf[y*res+x] = to565(PAL_rainbow[(v+(mask<<5))&255]);
  }
  blitUp(res, false);
}
static void renderGlobe(uint32_t now) {   // dotted lat/lon sphere spinning about Y; color = config palette by latitude (pole -> pole)
  float t = now*0.45f;
  canvas->fillScreen(geC565(2,4,8));
  const float cx = 120, cy = 120, R = 240*0.41f;
  const int nLat = 16, nLon = 32;
  float rotY = t*0.0005f*3, cosR = cosf(rotY), sinR = sinf(rotY);
  // morph amplitude: tilt-driven on device (QMI8658 in-plane gravity), time-based burst as fallback
  float morphAmt;
  static float gTiltSmooth = 0;
  int16_t ax, ay, az;
  if (imuPresent && imuReadAccel(&ax, &ay, &az)) {
    float rawMag = aclf(sqrtf((float)ax*ax + (float)ay*ay)/8192.0f, 0.0f, 1.0f);   // 8192 counts/g (ACCEL_COUNTS_PER_G): 0 flat .. 1 on edge
    gTiltSmooth += (rawMag - gTiltSmooth)*0.06f;   // low-pass into a smooth waveform
    morphAmt = gTiltSmooth*0.4f;                   // map 0..1 tilt to a 0..0.4 radial swell
  } else {
    float mb = sinf(t*0.00013f); if (mb < 0) mb = 0; morphAmt = mb*mb*mb;   // 0 most of the time, swells in bursts
  }
  float wobT = t*0.001f;
  for (int la = 0; la <= nLat; la++) {
    float phi = (float)la/nLat*3.14159265f, sinP = sinf(phi), cosP = cosf(phi);
    uint16_t base = pColor(16, (int)((float)la/nLat*255));   // config palette by latitude, slow drift; depth-dimmed per-dot below
    int cr = ((base>>11)&0x1F)<<3, cg = ((base>>5)&0x3F)<<2, cb = (base&0x1F)<<3;   // RGB565 -> 8-bit for the alpha scale
    for (int lo = 0; lo < nLon; lo++) {
      float theta = (float)lo/nLon*6.2832f;
      float x0 = cosf(theta)*sinP, y0 = cosP, z0 = sinf(theta)*sinP;
      float xr = x0*cosR + z0*sinR, zr = -x0*sinR + z0*cosR, yr = y0;
      float Rp = R*(1 + morphAmt*0.38f*sinf(3*phi + 2*theta + wobT));
      int sx = (int)(cx + xr*Rp), sy = (int)(cy - yr*Rp);
      float alpha = zr < 0 ? 0.30f + 0.30f*(1+zr) : 0.55f + 0.45f*zr;   // far side dims (no real alpha: scale color toward the dark bg)
      canvas->fillRect(sx, sy, 3, 3, geC565((int)(cr*alpha),(int)(cg*alpha),(int)(cb*alpha)));
    }
  }
}
static void renderRosewindow(uint32_t now) {   // radial mirror-fold stained-glass mandala
  const int res = 64; uint32_t t = now*9/20; const int seg = 8; int T = (int)((t*3)>>5); float wedge = 6.2832f/seg;
  for (int y = 0; y < res; y++) for (int x = 0; x < res; x++) {
    float dx = x-32, dy = y-32, r = sqrtf(dx*dx+dy*dy);
    float ang = atan2f(dy,dx); ang = fmodf(fmodf(ang,wedge)+wedge,wedge); if (ang > wedge/2) ang = wedge-ang;
    int ai = (int)(ang*(256.0f/wedge)), ri = (int)(r*3);
    int breath = 128 + (fastSin(T+(int)(r*2))>>1);
    int v = ((fastSin(ri*4+T)+fastSin(ai*3-T)+breath+384)>>2) & 255;
    fxBuf[y*res+x] = to565(PAL_stained[v]);
  }
  blitUp(res, true);
}
static void renderPolarrose(uint32_t now) {   // layered rose curve r = cos(k*theta), k morphs, hue cycles per layer (p5-atlas polar-rose)
  atlasInit();
  float fc = now*0.06f;                          // ~60fps-equivalent frame counter (source uses frameCount)
  canvas->fillScreen(hsv565(320, 0.40f, 0.08f)); // dark magenta bg
  const float cx = 120, cy = 120, R = 240*0.42f;
  float k = 2 + sinf(fc*0.005f)*5;               // petal count morphs
  for (int L = 0; L < 6; L++) {
    uint16_t col = hsv565(fmodf(fc + L*30, 360), 0.75f, 0.90f);
    float scale = R*(1 - L*0.13f), px = 0, py = 0; bool first = true;
    for (float a = 0; a < 6.2832f*2; a += 0.02f) {
      float r = cosf(k*a)*scale, x = cx + cosf(a)*r, y = cy + sinf(a)*r;
      if (!first) canvas->drawLine((int)px, (int)py, (int)x, (int)y, col);
      px = x; py = y; first = false;
    }
  }
}
static void renderFermat(uint32_t now) {   // Fermat spiral of golden-angle dots; color = config palette, core -> rim (p5-atlas emergent-spiral)
  float fc = now*0.06f, t = fc*0.01f;
  uint16_t bg = pColor(20, 0);   // dim the palette's leading color for a cohesive horizon
  canvas->fillScreen(geC565((((bg>>11)&0x1F)<<3)>>2, (((bg>>5)&0x3F)<<2)>>2, ((bg&0x1F)<<3)>>2));
  const float cx = 120, cy = 120, c = 240*0.012f;
  for (int i = 0; i < 1200; i++) {
    float a = i*2.39996f + t*0.1f, r = c*sqrtf((float)i), wob = sinf(t + i*0.05f)*4;
    float x = cx + cosf(a)*(r+wob), y = cy + sinf(a)*(r+wob);
    canvas->fillRect((int)x, (int)y, 2, 2, pColor(20, i*255/1199));   // dot index (radial) -> palette offset, slow drift
  }
}
static void renderAtlas(int idx, uint32_t now) {
  atlasInit();
  if (!fxBuf) { fxBuf = (uint16_t*)malloc(96*96*2); if (!fxBuf) return; }   // ~18 KB (largest grid = julia 96^2), kept after first entry (swirlBuf precedent)
  switch (idx) {
    case 0: renderJulia(now);        break;   case 1: renderInterference(now); break;
    case 2: renderXormunch(now);     break;   case 3: renderGlobe(now);        break;
    case 4: renderRosewindow(now);   break;   case 5: renderPolarrose(now);    break;
    case 6: renderFermat(now);       break;
  }
}

// ---- id 47: greetz scroller ----------------------------------------------------------------
// A demoscene sine-wave marquee. Ported from references/greetz.html; content lives in greetz.h.
// The panel is physically round, so pixels outside r=120 do not exist on the glass -- the rim
// vignette the web version needed CSS for is free here, and nothing needs clipping.

static char        gGreetzText[GREETZ_BUF];
static size_t      gGreetzLen  = 0;
static GreetzState gGreetzState;
static int32_t     gGreetzOff  = 0;      // scroll offset in pixels
static uint8_t     gGreetzPal  = 0;

static constexpr int GREETZ_CELL = VGA_FONT_W * 2 + 2;   // 8px glyph at 2x + the VGA 9th column (the letter-spacing) doubled
static constexpr int GREETZ_SPEED  = 4;                 // px/frame (owner: "slightly faster"); loop ~83 s
static constexpr int GREETZ_AMP    = 24;                // px of swing (owner: "a little wavier")
// The reference's 0.012 rad/px is a 523px wavelength -- fine on its 1728px canvas (3.3 waves
// visible), but on a 240px panel that is under HALF a wave, which reads as a lazy drift rather
// than a scroller. 410 gives ~1.6 waves across the glass. Raise for tighter ripples.
static constexpr int GREETZ_X_STEP = 410;               // 8.8 fixed point: phase steps per screen px
static constexpr int GREETZ_T_STEP = 208;               // 8.8 fixed point: 0.020 rad/px -> phase

// Five CRT phosphor palettes from the reference, advancing one per completed loop.
static const uint8_t GREETZ_PAL[5][3] = {
  {0x33, 0xff, 0x66},   // green
  {0xff, 0xb0, 0x00},   // amber
  {0x33, 0xcc, 0xff},   // cyan
  {0xff, 0x33, 0x99},   // pink
  {0xc0, 0xc0, 0xc0},   // silver
};

static uint32_t greetzRng(uint32_t n) { return n ? (uint32_t)random(n) : 0; }

static void greetzRebuild() {
  gGreetzLen = greetzBuild(gGreetzState, gGreetzText, sizeof(gGreetzText), greetzRng);
  gGreetzOff = 0;
}

// Parallax pixel starfield, ported from the reference. Positions are Q4 fixed point (1/16 px) so
// the slow far-layer stars still drift smoothly without float in the loop.
struct GreetzStar { int32_t x, y; uint8_t spd; uint8_t sz; uint8_t bright; };
struct GreetzShot { int32_t x, y, vx, vy; uint8_t life, max; };

static constexpr int GREETZ_STARS = 35;
static constexpr int GREETZ_SHOTS = 2;
static GreetzStar gGreetzStar[GREETZ_STARS];
static GreetzShot gGreetzShot[GREETZ_SHOTS];
static uint8_t    gGreetzShotN = 0;

static void greetzSeedStars() {
  for (int i = 0; i < GREETZ_STARS; i++) {
    int depth = random(256);                       // 0 = far, 255 = near
    gGreetzStar[i].x      = random(240) << 4;
    gGreetzStar[i].y      = random(240) << 4;
    gGreetzStar[i].spd    = (uint8_t)(5 + depth * 35 / 255);    // Q4: 0.3 .. 2.5 px/frame
    gGreetzStar[i].sz     = depth < 128 ? 1 : 2;
    gGreetzStar[i].bright = (uint8_t)(77 + depth * 140 / 255);  // 0.3 .. 0.85 of full
  }
  gGreetzShotN = 0;
}

static void greetzDrawStarfield() {
  for (int i = 0; i < GREETZ_STARS; i++) {
    GreetzStar& s = gGreetzStar[i];
    s.x -= s.spd;                                  // drift left, matching the scroller
    if (s.x < 0) { s.x = 239 << 4; s.y = random(240) << 4; }
    uint16_t c = gfx->color565((210 * s.bright) >> 8, (255 * s.bright) >> 8, (225 * s.bright) >> 8);
    canvas->fillRect(s.x >> 4, s.y >> 4, s.sz, s.sz, c);
  }

  if (gGreetzShotN < GREETZ_SHOTS && random(1000) < 12) {       // ~1.2% per frame
    GreetzShot& p = gGreetzShot[gGreetzShotN++];
    p.x = random(120) << 4; p.y = random(96) << 4;
    p.vx = (5 + random(5)) << 4; p.vy = (1 + random(2)) << 4;
    p.life = 0; p.max = 26;
  }
  for (int i = (int)gGreetzShotN - 1; i >= 0; i--) {
    GreetzShot& p = gGreetzShot[i];
    p.x += p.vx; p.y += p.vy; p.life++;
    for (int t = 0; t < 7; t++) {                  // pixel head plus a fading trail
      int a = (7 - t) * (p.max - p.life) * 255 / (7 * p.max);
      if (a <= 0) continue;
      uint16_t c = gfx->color565(a, a, a);
      canvas->fillRect((p.x - p.vx * t) >> 4, (p.y - p.vy * t) >> 4, 2, 2, c);
    }
    if (p.life > p.max || (p.x >> 4) > 240 || (p.y >> 4) > 240)
      gGreetzShot[i] = gGreetzShot[--gGreetzShotN];             // swap-remove
  }
}

static void greetzOnEnter() {
  static bool firstEntry = true;
  if (firstEntry) {                       // loops/swapAt/palette persist across re-entry so the
    greetzInit(gGreetzState, greetzRng);  // RiverDaddy egg (3-5 loops out) and the 5-phosphor
    gGreetzPal = 0;                       // cycle stay reachable on a button-cycled unit.
    firstEntry = false;
  }
  greetzSeedStars();
  greetzRebuild();
}

static void renderGreetz(uint32_t now) {
  (void)now;
  const uint8_t* p = GREETZ_PAL[gGreetzPal];
  uint16_t col = gfx->color565(p[0], p[1], p[2]);
  greetzDrawStarfield();                  // behind the text; must run every frame, incl. the wrap frame below

  int32_t total = (int32_t)gGreetzLen * GREETZ_CELL;
  gGreetzOff += GREETZ_SPEED;
  if (gGreetzOff >= total) {              // loop complete: reshuffle and flip phosphor.
    gGreetzPal = (uint8_t)((gGreetzPal + 1) % 5);   // The screen holds only pad here, so both
    greetzRebuild();                                // changes land invisibly.
    return;
  }

  int first = gGreetzOff / GREETZ_CELL;                       // leftmost partly-visible cell
  int last  = (gGreetzOff + 240) / GREETZ_CELL + 1;
  if (last > (int)gGreetzLen) last = (int)gGreetzLen;

  for (int i = first; i < last; i++) {
    char ch = gGreetzText[i];
    if (ch == ' ') continue;
    if (ch < VGA_FONT_FIRST || ch > VGA_FONT_LAST) continue;  // belt and braces; a host test pins this
    int x = i * GREETZ_CELL - gGreetzOff;
    // The shifted sum goes negative for a partly-offscreen glyph now that X_STEP > T_STEP; the
    // mask is what keeps idx a valid LUT index either way -- don't drop it chasing a cycle.
    int idx = (((x * GREETZ_X_STEP) + (gGreetzOff * GREETZ_T_STEP)) >> 8) & 0xFF;
    int y = 120 + (fastSin(idx) * GREETZ_AMP) / 127 - (VGA_FONT_H * 2) / 2;

    const uint8_t* rows = VGA_FONT[ch - VGA_FONT_FIRST];
    for (int r = 0; r < VGA_FONT_H; r++) {
      uint8_t bits = rows[r];
      if (!bits) continue;
      for (int b = 0; b < VGA_FONT_W; b++) {
        if (!(bits & (0x80 >> b))) continue;
        // ponytail: fillRect clips against the canvas bounds for us, which is what keeps a
        // partly-offscreen glyph from writing outside the framebuffer. Direct fb writes would be
        // faster but would need that clip written by hand -- revisit only if this measures hot.
        canvas->fillRect(x + b * 2, y + r * 2, 2, 2, col);
      }
    }
  }
}

void renderEffect(int effect, uint32_t now) {
  switch (effect) {
      case 0: {  // MATRIX rain (text size MATRIX_TS); NAME_COLS columns weave gConfig.name in
        canvas->setTextSize(MATRIX_TS);
        bool named = gConfig.nameMatrixRain && !gConfig.name.empty();
        int nameLen = (int)gConfig.name.size();
        if (named) updateNameGlitch(now, nameLen);   // advance the "resisting" letter glitches
        for (int i = 0; i < MATRIX_COLS; i++) {
          if (now - matrixCols[i].lastUpdate > matrixCols[i].speed) { matrixCols[i].y += MATRIX_RH; matrixCols[i].lastUpdate = now; }
          int x = i * MATRIX_CW, y = matrixCols[i].y;
          bool nameCol = named && matrixNameCol[i];
          // glyph for tail position t: name letter (a few glitching back toward random), else rain
          auto glyph = [&](int t) -> char {
            if (!nameCol) return (char)random(33, 126);
            char c = matrixNameGlyph(gConfig.name, t);
            if (!c) return (char)random(33, 126);              // past the name -> rain
            char g; return nameGlitchOn(nameLen - 1 - t, g) ? g : c;   // resisting letter shows scramble
          };
          if (y > -MATRIX_RH && y < 240) { canvas->setCursor(x, y); canvas->setTextColor(0xBE76); canvas->print(glyph(0)); }
          int tail = nameCol ? max(15, (int)gConfig.name.size()) : 15;   // long name streams its full length
          for (int j = 1; j < tail; j++) {
            int tY = y - j * MATRIX_RH;
            if (tY > -MATRIX_RH && tY < 240) { int gV = 255 - (j * 18); if (gV < 40) gV = 40; canvas->setCursor(x, tY); canvas->setTextColor(gfx->color565(0, gV, 0)); canvas->print(glyph(j)); }
          }
          if (matrixCols[i].y > 240 + 15 * MATRIX_RH) {   // whole streak cleared the bottom -> respawn
            initMatrixColumn(i);
            if (matrixNameCol[i]) { matrixNameCol[i] = false; pickMatrixNameCol(i); }   // hand the name slot to a fresh column -> roams
          }
        }
        break;
      }

      case 1: { // 3D ROTATING CUBE
        float cube[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
        int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        float rX = now/1000.0f, rY = now/1300.0f, rZ = now/1700.0f;
        float cX=cos(rX), sX=sin(rX), cY=cos(rY), sY=sin(rY), cZ=cos(rZ), sZ=sin(rZ);
        int px[8], py[8];
        for(int i=0; i<8; i++) {
          float x=cube[i][0], y=cube[i][1], z=cube[i][2];
          float ty = y*cX - z*sX, tz = y*sX + z*cX; y=ty; z=tz;
          float tx = x*cY + z*sY; tz = -x*sY + z*cY; x=tx; z=tz;
          tx = x*cZ - y*sZ; ty = x*sZ + y*cZ; x=tx; y=ty;
          float p = 3.0f / (3.0f - z); px[i] = 120 + (int)(x*p*50); py[i] = 120 + (int)(y*p*50);
        }
        for(int i=0; i<12; i++) canvas->drawLine(px[edges[i][0]], py[edges[i][0]], px[edges[i][1]], py[edges[i][1]], pColor(5, i*15));
        break;
      }

      case 2: { // FLOWING PLASMA
        uint32_t t = now + 50000;
        for (int x = 0; x < 240; x += 12) {
          for (int y = 0; y < 240; y += 12) {
            float v = fastSin(x / 16.0 + t / 800.0) + fastSin((y + t / 10.0) / 20.0) + fastSin((x + y + t / 15.0) / 30.0);
            canvas->fillRect(x, y, 12, 12, pColor(v * 4 + 10, x / 2 + y / 2));
          }
        }
        break;
      }

      case 3: { // 4D TESSERACT
        float nodes[16][4] = {{-1,-1,-1,-1},{1,-1,-1,-1},{1,1,-1,-1},{-1,1,-1,-1},{-1,-1,1,-1},{1,-1,1,-1},{1,1,1,-1},{-1,1,1,-1},{-1,-1,-1,1},{1,-1,-1,1},{1,1,-1,1},{-1,1,-1,1},{-1,-1,1,1},{1,-1,1,1},{1,1,1,1},{-1,1,1,1}};
        int edges[32][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7},{8,9},{9,10},{10,11},{11,8},{12,13},{13,14},{14,15},{15,12},{8,12},{9,13},{10,14},{11,15},{0,8},{1,9},{2,10},{3,11},{4,12},{5,13},{6,14},{7,15}};
        float r = now/1000.0f;
        float cr=cos(r), sr=sin(r), cr8=cos(r*0.8f), sr8=sin(r*0.8f);
        int px[16], py[16];
        for(int i=0; i<16; i++) {
          float x=nodes[i][0], y=nodes[i][1], z=nodes[i][2], w=nodes[i][3];
          float tw = w*cr - x*sr; float tx = w*sr + x*cr; w=tw; x=tx;
          float ty = y*cr8 - z*sr8; float tz = y*sr8 + z*cr8; y=ty; z=tz;
          float p = 4.0f / (4.0f - w); float p2 = 3.0f / (3.0f - z);
          px[i] = 120 + (int)(x*p*p2*45); py[i] = 120 + (int)(y*p*p2*45);
        }
        for(int i=0; i<32; i++) canvas->drawLine(px[edges[i][0]], py[edges[i][0]], px[edges[i][1]], py[edges[i][1]], pColor(8, i*4));
        break;
      }

      case 4: { // DOOM CORRIDOR --- rare trap door: a missing floor tile drops you into a parallel tunnel below (+palette swap)
        float speed = now / 250.0f;
        float turn = 0.8f * fastSin(now / 60) / 127.0f;   // swing the bend left<->right (~15s), straightening through 0
        float flicker = (random(100) < 3) ? (float)random(6, 10) / 10.0f : 1.0f; // 0.6-0.9: strobes without dimming walls to near-black
        const int NUM_SEG = 15;
        const uint32_t DROP_MS = 450;

        static long trapTile = 0;
        static bool dropping = false; static uint32_t dropStart = 0;
        long baseTile = (long)speed;
        if (!dropping && trapTile <= baseTile) trapTile = baseTile + NUM_SEG + random(40, 130);  // schedule next trap ~10-30s out
        int trapI = (int)(baseTile + NUM_SEG - trapTile);                                        // 0=far .. NUM_SEG-1=at your feet

        if (dropping) {
          float p = (float)(now - dropStart) / DROP_MS;                          // 0..1 through the fall
          if (p >= 1.0f) { dropping = false; trapTile = baseTile + NUM_SEG + random(40, 130); drawCorridor(speed, turn, flicker, 0, -1); break; }
          int panY = -(int)(p * 240);                                            // world slides up as you fall
          drawCorridor(speed, turn, flicker, panY, -1);                          // tunnel you're leaving --- rises out the top
          drawCorridor(speed, turn, flicker, panY + 240, -1);                    // parallel tunnel below --- rises into center
        } else {
          int holeSeg = (trapI >= NUM_SEG - 4 && trapI <= NUM_SEG - 2) ? trapI : -1;  // gap opens in the floor for the last few tiles
          if (trapI == NUM_SEG - 2) {                                            // gap reaches your feet -> drop, and swap palette mid-fall
            dropping = true; dropStart = now;
            if (gRotList.size() > 1) { gRotIndex = (gRotIndex + 1) % gRotList.size(); buildActivePaletteLUT(); gPalFadeStart = 0; gLastPaletteRotate = now; }  // DOOM: instant jolt, cancel any fade
          }
          drawCorridor(speed, turn, flicker, 0, holeSeg);
        }
        break;
      }

      case 5: for (int x = 0; x < 240; x += 15) { for (int y = 0; y < 240; y += 15) { int v = fastSin(x + now/16) + fastSin(y + now/12); canvas->fillRect(x, y, 15, 15, pColor(v / 8 + 5, x + y)); } } break;
      case 6: for (int i = 0; i < 10; i++) { int br = (fastSin(now / 8) + 150) * 40 / 127; canvas->drawCircle(120, 120, br + (i * 10), pColor(10, i * 15)); } canvas->fillCircle(120, 120, 30, BLACK); break;
      case 7: for (int i = 0; i < 12; i++) { int size = (now / 10 + i * 20) % 240; canvas->drawRect(120 - size/2, 120 - size/2, size, size, pColor(5, i * 20)); } break;
      case 8: for (int i = 0; i < 10; i++) { canvas->fillRect(0, random(240), 240, random(5, 15), pColor(1, i * 50)); } break;
      case 9: { for (int x = 0; x < 240; x += 20) { for (int y = 0; y < 240; y += 20) { int dx=x-120, dy=y-120; int dist = sqrt(dx*dx + dy*dy); int v = fastSin(dist - now / 8) + fastSin(x / 10) + fastSin(y / 10); canvas->fillRect(x, y, 20, 20, pColor(8, (now / 10) - dist / 2 + v * 5)); } } break; }
      case 10: { for (int i = 0; i < 60; i++) { int angle = i * 4.25f; int spd = ((now + (i * 100)) % 2000) / 8; canvas->drawLine(120, 120, 120 + (fastCos(angle) * spd / 127), 120 + (fastSin(angle) * spd / 127), pColor(5, i * 10)); } break; }

      case 11: { // NAME SPIRAL: the configured name streams outward along a rotating Archimedean coil
        const char* nm = gConfig.name.empty() ? "hello" : gConfig.name.c_str();
        int len = gConfig.name.empty() ? 5 : (int)gConfig.name.size();
        static const char* SEPS[] = { "-~-", "~*~", ".:.", "-+-", "=~=", "<~>" };
        const int SEP_COUNT = (int)(sizeof(SEPS) / sizeof(SEPS[0]));
        const int SEP_LEN = 3;
        int ribbonLen = len + SEP_LEN;    // name + ASCII-art gap, e.g. john-~-john
        int L = ribbonLen * 3;            // keep about three readable name repeats on the coil
        if (L < 18) L = 18;
        if (L > 44) L = 44;
        int maxTextSize = len <= 5 ? 4 : (len <= 12 ? 3 : 2);
        float flow = now / 180.0f;        // outward advance; slot phase = frac((i+flow)/L). slow: each glyph moves << its width/frame so it reads crisp without a trail
        float bias = (fastSin(now / 40) + 127) / 254.0f;         // 0..1, slow (~10s): which end gets the big letters
        float gamma = 0.75f + 0.5f * bias;                       // narrow [0.75,1.25]: wider range collapsed the inner letters into a center pile
        int cx = 120;                                            // fixed center (orbit removed)
        int cy = 120;
        int maxR = 116 - maxTextSize * 3;                         // push the coil to the round rim; glyphs ride the edge with slight clip
        const int rMin = 14;                                     // inner radius floor: keep glyphs off dead-center so they never stack there
        canvas->setTextWrap(false);       // rim letters must not wrap to the left edge
        for (int i = 0; i < L; i++) {
          float phase = fmodf((i + flow) / L, 1.0f);              // 0..1, outward, wraps to respawn at center
          int r   = rMin + (int)(powf(phase, gamma) * (maxR - rMin)); // ponytail: powf ok on S3 FPU
          int ang = (int)(phase * 3 * 256 + i * (256 / L) + now / 96); // 3 turns + slot spread + slow coil spin
          int x = cx + fastCos(ang) * r / 127;
          int y = cy + fastSin(ang) * r / 127;
          float sizef = bias * phase + (1 - bias) * (1 - phase);  // crossfade: grow outward <-> grow inward
          int ts = 1 + (int)(sizef * maxTextSize);
          if (ts > maxTextSize) ts = maxTextSize;
          int stream = i + (int)flow;
          int pos = stream % ribbonLen;
          bool nameGlyph = pos < len;
          char ch;
          if (nameGlyph) ch = nm[pos];
          else {
            int motif = ((stream / ribbonLen) + (int)(now / 2400)) % SEP_COUNT;
            ch = SEPS[motif][pos - len];
          }
          canvas->setTextSize(ts);
          uint16_t col = pColor(nameGlyph ? 6 : 10, ang + i * 8 + (nameGlyph ? 0 : 64));
          canvas->setTextColor(nameGlyph ? col : dim565(col, 2, 3)); // separators are texture; the name stays dominant
          canvas->setCursor(x - 3 * ts, y - 4 * ts);             // center the 6x8*ts glyph on its point
          canvas->print(ch);                                     // name cycles as a word-spaced ASCII-art ribbon
        }
        break;
      }

      case 12: { // STARFIELD: palette-colored warp stars streaking out of center
        static struct Star { uint8_t ang; uint16_t r; uint8_t spd; } st[48];
        static bool sinit = false;
        if (!sinit) { for (auto& s : st) { s.ang = random(256); s.r = random(120); s.spd = random(2, 6); } sinit = true; }
        for (auto& s : st) {
          uint16_t pr = s.r;
          s.r += s.spd + s.r / 24;                                 // accelerate outward -> perspective
          if (s.r > 120) { s.ang = random(256); s.r = random(6, 18); s.spd = random(2, 6); pr = s.r; }
          canvas->drawLine(120 + fastCos(s.ang) * pr / 127, 120 + fastSin(s.ang) * pr / 127,
                           120 + fastCos(s.ang) * s.r / 127, 120 + fastSin(s.ang) * s.r / 127,
                           pColor(8, s.ang + s.r));                // streak from prev to current radius
        }
        break;
      }

      case 13: { // MYSTIFY: bouncing polygon with a fading trail (classic screensaver)
        const int NP = 4, TRAIL = 8;
        static int16_t px[NP], py[NP], vx[NP], vy[NP];
        static int16_t hist[TRAIL][NP * 2];                        // static -> zero-init; first few frames converge from origin
        static int hp = 0; static bool minit = false;
        if (!minit) { for (int i = 0; i < NP; i++) { px[i] = random(240); py[i] = random(240); vx[i] = random(2, 5) * (random(2) ? 1 : -1); vy[i] = random(2, 5) * (random(2) ? 1 : -1); } minit = true; }
        const int R = 118;
        for (int i = 0; i < NP; i++) {
          px[i] += vx[i]; py[i] += vy[i];
          int dx = px[i] - 120, dy = py[i] - 120;
          if (dx*dx + dy*dy > R*R) {                                // outside the round rim -> reflect off the radial normal
            float d = sqrtf(dx*dx + dy*dy), nx = dx/d, ny = dy/d, vn = vx[i]*nx + vy[i]*ny;
            vx[i] -= (int16_t)lroundf(2*vn*nx); vy[i] -= (int16_t)lroundf(2*vn*ny);
            px[i] = 120 + (int16_t)(nx*R); py[i] = 120 + (int16_t)(ny*R);
          }
          hist[hp][i * 2] = px[i]; hist[hp][i * 2 + 1] = py[i];
        }
        for (int t = 0; t < TRAIL; t++) {                          // oldest -> newest, dimming toward the tail
          int idx = (hp - t + TRAIL) % TRAIL;
          uint16_t c = dim565(pColor(10, t * 20), (uint8_t)(TRAIL - t), (uint8_t)TRAIL);
          for (int i = 0; i < NP; i++) { int j = (i + 1) % NP; canvas->drawLine(hist[idx][i*2], hist[idx][i*2+1], hist[idx][j*2], hist[idx][j*2+1], c); }
        }
        hp = (hp + 1) % TRAIL;
        break;
      }

      case 14: { // DVD LOGO: bitmap bounces off the round rim, hugging it by the logo's actual
                 // lit-pixel box (not its loose bounding circle), recoloring on each hit
        const int w = DVD_W, h = DVD_H, rb = (w + 7) / 8;
        static int x = 72, y = 64, vx = 3, vy = 2, ci = 0;        // start off-center: dead-center would just oscillate on one diameter
        static int bx0, bx1, by0, by1; static bool binit = false;
        if (!binit) {                                             // tight box of lit pixels, scanned once -> trims transparent margin
          bx0 = w; bx1 = 0; by0 = h; by1 = 0;
          for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
              if (DVD_BITS[j*rb + i/8] & (0x80 >> (i & 7))) {
                if (i < bx0) bx0 = i; if (i > bx1) bx1 = i;
                if (j < by0) by0 = j; if (j > by1) by1 = j;
              }
          binit = true;
        }
        x += vx; y += vy;
        const int RIM = 120;                                      // farthest lit-box corner crossing this radius = rim hit
        int corx[2] = { x + bx0, x + bx1 }, cory[2] = { y + by0, y + by1 };
        int nx = 0, ny = 0, bestd = 0;
        for (int a = 0; a < 2; a++)
          for (int b = 0; b < 2; b++) {
            int dx = corx[a] - 120, dy = cory[b] - 120, dd = dx*dx + dy*dy;
            if (dd > bestd) { bestd = dd; nx = dx; ny = dy; }      // (nx,ny) = vector to the offending corner
          }
        if (bestd > RIM*RIM) {                                    // reflect off the radial normal at that corner
          float d = sqrtf(bestd), ux = nx/d, uy = ny/d, vn = vx*ux + vy*uy;
          float rvx = vx - 2*vn*ux, rvy = vy - 2*vn*uy;           // reflected velocity...
          float ang = atan2f(rvy, rvx) + (random(-35, 36)) * 0.01745f;  // ...plus a small random turn, else it traces a fixed rosette
          vx = (int)lroundf(cosf(ang) * 3.6f); vy = (int)lroundf(sinf(ang) * 3.6f);
          int over = (int)lroundf(d - RIM);                       // push the corner back exactly onto the rim
          x -= (int)lroundf(ux * over); y -= (int)lroundf(uy * over);
          ci += 43;                                               // new palette hue each rim hit
        }
        uint16_t c = activePaletteLUT[(uint8_t)ci];
        for (int j = 0; j < h; j++)                               // draw the mask tinted; clear bits stay transparent
          for (int i = 0; i < w; i++)
            if (DVD_BITS[j*rb + i/8] & (0x80 >> (i & 7))) canvas->drawPixel(x + i, y + j, c);
        break;
      }

      case 15: { // PIPES: MS-screensaver pipes crawl and accumulate, then wipe and restart when the screen fills
        constexpr int CELL = 12, TUBE = 8, RIM = 114;              // RIM: round rim inset by the tube half-width
        static int hx, hy, dir, seg, col; static bool started = false;
        if (g_pipesReset || !started) { canvas->fillScreen(BLACK); hx = 120; hy = 120; dir = random(4); seg = 0; col = random(256); started = true; g_pipesReset = false; }
        const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        auto outside = [](int x, int y){ int a = x-120, b = y-120; return a*a + b*b > RIM*RIM; };
        if (random(100) < 22) { dir = (dir + (random(2) ? 1 : 3)) & 3; col += 6; }   // random 90-degree elbow
        int nx = hx + dx[dir] * CELL, ny = hy + dy[dir] * CELL;
        if (outside(nx, ny)) {                                     // hit the round rim: try one turn, then the other
          int base = dir, t = random(2) ? 1 : 3;
          dir = (base + t) & 3; col += 6;
          nx = hx + dx[dir] * CELL; ny = hy + dy[dir] * CELL;
          if (outside(nx, ny)) { dir = (base + (4 - t)) & 3; nx = hx + dx[dir] * CELL; ny = hy + dy[dir] * CELL; }
        }
        if (outside(nx, ny) || ++seg > 1200) { g_pipesReset = true; break; } // truly cornered (rare on a circle), or full -> restart
        uint16_t c = pColor(6, col);                                                  // hue drifts along the run -> gradient tube
        canvas->fillRect(min(hx, nx) - TUBE/2, min(hy, ny) - TUBE/2, abs(nx - hx) + TUBE, abs(ny - hy) + TUBE, c);
        hx = nx; hy = ny;
        break;
      }
      case 16: { // FRACTAL CLOCK: morphing recursive hand-tree, free-running off millis()
        const float scale = 0.7937f;                                  // cube root of 1/2
        int aHour = now / 600, aMinute = now / 80, aSecond = now / 14;  // three "hands", incommensurate rates
        int dSec = aSecond - aHour, dMin = aMinute - aHour;            // branch angles = hand deltas (rotate whole tree by hour)
        fr0x = fastCos(dSec) * scale / 127.0f; fr0y = fastSin(dSec) * scale / 127.0f;
        fr1x = fastCos(dMin) * scale / 127.0f; fr1y = fastSin(dMin) * scale / 127.0f;
        for (int d = 0; d <= FRACTAL_DEPTH; d++) fracDepthCol[d] = pColor(30, d * 18);  // palette gradient down the branches
        const float R = 48.0f;                                        // trunk length (Mayoff's min(w,h)/5)
        fractalBranch(120, 120, fastCos(aHour) * R / 127.0f, fastSin(aHour) * R / 127.0f, 0);
        break;
      }
      case 17: renderFluid(now); break;   // id 30: tilt-driven liquid

      case 18: { // YIN-YANG (id 31): 1-bit nekojiru sprite, inverse-rotated per pixel.
        // Flywheel, in Q8 fixed point (256 sub-steps per LUT index). Gyro-Z is rotation about the
        // panel normal -- literally "someone twisted the disc". Friction decays omega toward a
        // nonzero IDLE, not toward zero: with no IMU (every board but the Waveshare) gz stays 0,
        // omega sits at IDLE, and this is exactly the reference sketch's constant spin -- so one
        // code path covers the toy and the three boards that can't be tilted.
        //
        // IDLE takes the SIGN of the current spin, so the coast carries on whichever way it was
        // already going. Decaying toward a fixed +IDLE instead would drag a counter-clockwise spin
        // back through zero and reverse it -- the disc visibly stopping and turning around, which
        // is not what a flywheel does. omega is never pushed toward zero, only away from it, so the
        // sign can't chatter at the crossing.
        static constexpr int YY_TORQUE_K   = 3;    // gyro counts -> Q8 omega, as (gz * K) >> 6
        static constexpr int YY_IDLE       = 128;  // Q8 omega at rest: ~8 s/rev at 60 fps
        static constexpr int YY_DAMP_SHIFT = 6;    // friction time constant ~64 frames (~1 s)
        static constexpr int YY_OMEGA_MAX  = 8000; // ~31 LUT idx/frame; past this the spin aliases into strobing
        static int32_t omega = YY_IDLE, angleQ = 0;

        int16_t gxr, gyr, gzr;
        if (imuPresent && imuReadGyro(&gxr, &gyr, &gzr)) omega += ((int32_t)gzr * YY_TORQUE_K) >> 6;
        const int32_t idle = (omega < 0) ? -YY_IDLE : YY_IDLE;   // coast keeps its direction
        omega += (idle - omega) >> YY_DAMP_SHIFT;
        omega = constrain(omega, -YY_OMEGA_MAX, YY_OMEGA_MAX);
        // ponytail: frame-ticked, not time-ticked -- non-audio modes are fps-capped, so frames are
        // even enough. If maxFps ever changes the spin speed noticeably, scale by the frame delta.
        angleQ += omega;

        const int ang = (angleQ >> 8) & 0xFF;
        const int cv = fastCos(ang), sv = fastSin(ang);                 // +-127
        const uint16_t lit = pColor(120, 0), dark = pColor(120, 128);   // two tones, opposite, ~30 s round the wheel
        constexpr int CX = 120, CY = 120, RAD = 120;

        uint16_t* buf = canvas->getFramebuffer();   // fills every pixel incl. the black surround -- no fillScreen needed
        for (int y = 0; y < 240; y++) {
          const int dy = y - CY;
          for (int x = 0; x < 240; x++) {
            const int dx = x - CX;
            uint16_t c = BLACK;
            if (dx * dx + dy * dy <= RAD * RAD) {                       // inside the round panel
              const int sx = CX + (( dx * cv + dy * sv + 64) >> 7);     // inverse-rotate the sample point
              const int sy = CY + ((-dx * sv + dy * cv + 64) >> 7);
              c = yyBit(sx, sy) ? lit : dark;
            }
            buf[y * 240 + x] = c;
          }
        }
        break;
      }

      case 20: { // WORMHOLE (id 33): wireframe deep zoom down a "triangular spiral" funnel
        // (Grant/Ghannam/Kennedy's natural-spiral model -- math-wise a discrete log spiral, but
        // its right-triangle lock is a keeper: a mod-M spiral turns 360/M per step and MUST grow
        // G = 1/cos(2*pi/M), so turn and growth are one knob, not two). Rings are the regular
        // (M/2)-gons that model delineates, each one triangle-step up the funnel; the vertex-j
        // rail chains ARE the spiral hypotenuses, so two opposite chains get accent hues (the
        // paper's red/blue arms over hurricanes/galaxies). Zoom advances t through ONE ring
        // spacing then wraps -- self-similar, so the dive is seamless and infinite (rotPhase
        // absorbs one twist step). Random re-twists every ~7-18s: mod (6 = sparse triangle
        // funnel .. 16 = the paper's nautilus web), handedness, zoom speed/direction (pull-back
        // is the rare one), spin. twist/G slew toward the new mod; the V-gon pops.
        static const uint8_t MODS[]  = {6, 8, 10, 12, 14, 16};
        static const float  MOD_G[]  = {2.0f, 1.41421f, 1.23607f, 1.15470f, 1.10992f, 1.08239f}; // 1/cos(2pi/M)
        static float twist = 21.3f, twistTgt = 21.3f;    // 256/M LUT units per ring (+/- = handedness)
        static float ringG = 1.1547f, ringGTgt = 1.1547f;
        static float zoomVel = 0.5f, zoomTgt = 0.5f;     // ring spacings per second (+ = falling in)
        static float spinVel = 0.01f, spinTgt = 0.01f;   // whole-funnel spin, LUT units/ms
        static float t = 0, rotPhase = 0;
        static int verts = 6;
        static uint32_t nextTwist = 0, lastMs = 0;
        if (now >= nextTwist) {
          int mi   = random(0, 6);
          twistTgt = (random(2) ? 1 : -1) * 256.0f / MODS[mi];
          ringGTgt = MOD_G[mi];
          verts    = MODS[mi] / 2;
          zoomTgt  = (random(4) ? 1 : -1) * random(30, 90) / 100.0f;
          spinTgt  = (random(3) ? 1 : -1) * random(25, 80) / 1000.0f;
          nextTwist = now + random(7000, 18000);
        }
        twist   += (twistTgt - twist) * 0.02f;
        ringG   += (ringGTgt - ringG) * 0.02f;
        zoomVel += (zoomTgt - zoomVel) * 0.03f;
        spinVel += (spinTgt - spinVel) * 0.03f;
        uint32_t dt = now - lastMs; lastMs = now;
        if (dt > 100) dt = 100;                        // clamp: a mode-switch gap must not lurch the dive
        rotPhase += spinVel * dt;
        t += zoomVel * dt / 1000.0f;
        if      (t >= 1.0f) { t -= 1.0f; rotPhase -= twist; }   // ring i takes over ring i+1's slot...
        else if (t < 0.0f)  { t += 1.0f; rotPhase += twist; }   // ...so the phase absorbs one twist step
        if (rotPhase > 256.0f) rotPhase -= 256.0f; else if (rotPhase < 0.0f) rotPhase += 256.0f;
        int pup = 10 + (fastSin(now / 6) + 127) / 36;  // breathing pupil = the hole at the end
        int px[8], py[8]; bool have = false;
        int acc = verts >> 1;                          // second accent chain, opposite-ish the first
        int i = 0;
        for (float r = 6.0f * powf(ringG, t); r < 132.0f; r *= ringG, i++) {   // innermost spawns under the pupil
          float ang = rotPhase + i * twist;
          float vstep = 256.0f / verts;
          int h = (int)(r * 3.0f) / 2;                              // hue rides the radius
          uint8_t br = (uint8_t)(r > 125.0f ? 195 : 70 + (int)r);   // depth fade: dim at the far end
          uint16_t col = dim565(pColor(30, h), br, 195);
          int nx[8], ny[8];
          for (int j = 0; j < verts; j++) {
            int va = (int)(ang + j * vstep);
            nx[j] = 120 + fastCos(va) * (int)r / 127;
            ny[j] = 120 + fastSin(va) * (int)r / 127;
          }
          for (int j = 0; j < verts; j++) {
            canvas->drawLine(nx[j], ny[j], nx[(j + 1) % verts], ny[(j + 1) % verts], col);  // ring edge
            if (have) {   // rail = spiral hypotenuse chain; two chains full-bright in offset hues
              uint16_t rc = (j == 0)   ? pColor(30, h + 64)
                          : (j == acc) ? pColor(30, h + 192) : col;
              canvas->drawLine(px[j], py[j], nx[j], ny[j], rc);
            }
          }
          for (int j = 0; j < verts; j++) { px[j] = nx[j]; py[j] = ny[j]; }
          have = true;
        }
        canvas->fillCircle(120, 120, pup, BLACK);
        canvas->drawCircle(120, 120, pup, pColor(4, 128));
        break;
      }

      case 21: renderQR(); break;   // QR (id 34): static per-unit code, bitmap from config.html
      case 22: renderToasters(now); break;   // FLYING TOASTERS (id 35): see renderToasters above
      case 23: renderBoids(); break;         // BOIDS (id 36): see renderBoids above
      case 24: renderGardenEels(now); break; // GARDEN EELS (id 37): see renderGardenEels above
  }
}

// Floating eyelids: skin caps that ride the wandering eyeball itself (centered on the
// sclera at irX,irY), always drawn in front of ball/iris/pupil — googly-eye style, not
// a screen-edge vignette. The lid disc (radius LR, a bit proud of the biggest sclera)
// is cut by two arcs pinned at the eye corners (cx+-LR, cy); each lid fills from the
// disc edge to its arc. Apexes ease between random "expression" apertures; blinks
// sweep both to the equator, skinning the whole ball shut. Per-column vlines with a
// bit-by-bit integer sqrt — no float in the hot path (C3-safe).
static uint32_t isqrt32(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}

void drawEyelids(uint32_t now, int cx, int cy, int LR, bool square = false) {  // LR = lid radius; square = box eye
  static uint32_t nextBlink = 0, blinkStart = 0, nextExpr = 0;
  static int topRest = 40, botRest = 12, topTar = 40, botTar = 12;  // lid intrusion, % of LR (100 = to equator)
  const int CLOSE_MS = 80, OPEN_MS = 120;

  // Pick a new expression aperture every few seconds; special states only bias the same lids.
  if (nextExpr == 0) nextExpr = now + random(2000, 5000);       // hold normal briefly after boot
  if (isJittering) { topTar = 26; botTar = 12; nextExpr = now + random(1200, 2500); }
  else if (eyeEvent == EV_DROWSY) { topTar = 72; botTar = 34; nextExpr = now + random(1800, 3200); }
  else if (eyeEvent == EV_SCAN) { topTar = 30; botTar = 10; nextExpr = now + random(1000, 2400); }
  else if (eyeEvent == EV_SQUINT) { topTar = 52; botTar = 36; nextExpr = now + random(1200, 2400); }  // ~12% aperture slit
  else if (eyeEvent == EV_DOUBLE_TAKE) {                                                  // lids fly wide only on the snap-back
    if (now - eventStart >= 330) { topTar = 20; botTar = 8; }
    nextExpr = now + random(700, 1400);                                                   // hold the wide-eyed look ~1s after
  }
  else if (sideEye) { topTar = 50; botTar = 28; nextExpr = now + random(2000, 3500); }  // narrowed, skeptical stare
  else if (now >= nextExpr) {
    chooseMoodExpression(topTar, botTar);
    nextExpr = now + (eyeMood == MOOD_CURIOUS ? random(2200, 6200)
                    : eyeMood == MOOD_DROWSY  ? random(4500, 11000)
                    : random(3000, 9000));
  }
  // ease toward target; integer /4 stalls short, so fall back to +-1/frame for the last px
  topRest = easeInt(topRest, topTar, 4);
  botRest = easeInt(botRest, botTar, 4);

  // gaze coupling: cx,cy = the ball center = 120 + 0.4*gaze, so (cx-120,cy-120) recover it.
  int off = cy - 120, offX = cx - 120;                          // vertical / horizontal gaze (px, ~+-34)
  int hnarrow = (offX < 0 ? -offX : offX) / 6;                  // horizontal squint stays symmetric (0..~6%)
  // Vertical gaze retracts the trailing lid instead of squinting both: the upper lid opens as the
  // eye rolls up and covers as it looks down (lower lid inverse). Kills the half-lidded stare-up
  // that a direction-blind squint let happen when a sleepy expression met an extreme up-gaze.
  int t = topRest + hnarrow + off * 5 / 10;                     // up (off<0) opens top; down covers it   // ponytail: 5/10 tuned by eye, nudge if stare-up persists
  int b = botRest + hnarrow - off * 5 / 10;                     // up covers bottom; down opens it
  if (t < 0) t = 0; if (b < 0) b = 0;
  if (t > 100) t = 100; if (b > 100) b = 100;
  bool blinkAllowed = eyeEvent != EV_DROWSY && eyeEvent != EV_MICROSLEEP;  // both drive their own closure below
  if (nextBlink == 0) nextBlink = now + random(1500, 4000);     // first blink shortly after boot
  if (blinkStart == 0 && blinkAllowed && (eyeBlinkRequest || now >= nextBlink)) {
    blinkStart = now;
    eyeBlinkRequest = false;
  }
  if (!blinkAllowed) eyeBlinkRequest = false;
  if (blinkStart != 0) {
    uint32_t p = now - blinkStart;
    if (p < (uint32_t)CLOSE_MS) {                               // sweep both lids to the equator (100%)
      t += (100 - t) * (int)p / CLOSE_MS;
      b += (100 - b) * (int)p / CLOSE_MS;
    } else if (p < (uint32_t)(CLOSE_MS + OPEN_MS)) {            // and back out to rest
      int q = CLOSE_MS + OPEN_MS - (int)p;
      t += (100 - t) * q / OPEN_MS;
      b += (100 - b) * q / OPEN_MS;
    } else {                                                    // blink done -> schedule next (20% quick double-blink)
      blinkStart = 0;
      nextBlink = now + (random(100) < 20 ? random(120, 220) : random(2500, 6000));
    }
  }
  if (eyeEvent == EV_DROWSY) {                                  // one slow, heavy blink inside the droop
    uint32_t e = now - eventStart;
    if (e > 850 && e < 1850) {
      int q = (int)(e - 850), close;
      if      (q < 280) close = q * 100 / 280;
      else if (q < 620) close = 100;
      else              close = 100 - (q - 620) * 100 / 380;
      if (close < 0) close = 0; if (close > 100) close = 100;
      t += (100 - t) * close / 100;
      b += (100 - b) * close / 100;
    }
  }
  if (eyeEvent == EV_MICROSLEEP) {                              // ease shut -> sealed ~3s -> snap open
    uint32_t e = now - eventStart;
    int close;
    if      (e < 200)               close = 0;                 // still open as the droop begins
    else if (e < 800)               close = (int)(e - 200) * 100 / 600;   // ease shut (veins showing)
    else if (e < MICRO_MS - 400)    close = 100;               // sealed for the sleep
    else if (e < MICRO_MS)          close = 100 - (int)(e - (MICRO_MS - 400)) * 100 / 400;  // snap open
    else                            close = 0;
    t += (100 - t) * close / 100;
    b += (100 - b) * close / 100;
  }

  int ayT = cy - LR + LR * t / 100;                             // lid apex y positions
  int ayB = cy + LR - LR * b / 100;
  // pupil rides gaze at 0.7 but the lids at 0.4, so it drifts 0.75*off past the aperture;
  // slide both apexes ~0.6*off to follow it (~80% lag). Fade to 0 as the top lid shuts so
  // a blink still seals flat at the equator.
  int gshift = off * 6 / 10 * (100 - t) / 100;
  ayT += gshift; ayB += gshift;
  // never let a lid thin to nothing — tracking (or a wide expression) can drive one apex
  // toward the rim; hold each lid to a minimum band. Blink still seals flat at the equator,
  // far below this clamp, so full closure is unaffected.
  int minLid = LR * 16 / 100;                                  // min lid thickness, % of LR
  if (ayT < cy - LR + minLid) ayT = cy - LR + minLid;          // top lid: keep its band
  if (ayB > cy + LR - minLid) ayB = cy + LR - minLid;          // bottom lid: keep its band
  // lid edge = circle through the corners (cx+-LR, cy) with apex (cx, ay): center (cx,k),
  // k=(LR^2+cy^2-ay^2)/2(cy-ay); radius >= LR (half-chord), so the sqrt arg stays positive
  uint16_t skin = gConfig.skinColor;
  if (square) {                                                 // box eye: flat rectangular slabs, no arcs
    int top = cy - LR, bot = cy + LR, x0 = cx - LR, w = 2 * LR + 1;
    if (ayT > top) canvas->fillRect(x0, top, w, ayT - top, skin);   // top lid slab down to its edge
    if (ayB < bot) canvas->fillRect(x0, ayB, w, bot - ayB, skin);   // bottom lid slab up to its edge
    return;
  }
  bool flatT = ayT >= cy - 1, flatB = ayB <= cy + 1;            // apex at the equator -> lid drawn flat shut
  // pin the lid corners a few px inside the ball rim (CR) but still skin out to the rim (LR):
  // the outermost ring closes to a solid skin cap, so the almond's corners never bleed bare white.
  const int CR = LR - 4;
  int kT = 0, rT = 0, kB = 0, rB = 0;
  if (!flatT) { kT = (CR * CR + cy * cy - ayT * ayT) / (2 * (cy - ayT)); rT = kT - ayT; }
  if (!flatB) { kB = (CR * CR + cy * cy - ayB * ayB) / (2 * (cy - ayB)); rB = ayB - kB; }
  for (int x = cx - LR; x <= cx + LR; x++) {
    int dx = x - cx;
    int h = (int)isqrt32((uint32_t)(LR * LR - dx * dx));        // lid-disc half-height this column (to the ball rim)
    if (dx <= -CR || dx >= CR) { canvas->drawFastVLine(x, cy - h, 2 * h + 1, skin); continue; }  // corner: closed skin cap
    int yT = flatT ? cy : kT - (int)isqrt32((uint32_t)(rT * rT - dx * dx));
    int yB = flatB ? cy : kB + (int)isqrt32((uint32_t)(rB * rB - dx * dx));
    if (yT > cy - h) canvas->drawFastVLine(x, cy - h, yT - (cy - h), skin);  // top lid: disc edge down to arc
    if (yB < cy + h) canvas->drawFastVLine(x, yB, cy + h - yB, skin);        // bottom lid: arc down to disc edge
  }
}

// Bloodshot veins on the sclera for EV_MICROSLEEP: ~10 jagged red lines from the aperture rim
// inward toward the iris. Deterministic per event (seed) so they hold still frame-to-frame;
// alpha fades them in (0 = invisible white, 255 = full red). Drawn only while lids are open.
static void drawBloodshot(int cx, int cy, int R, uint8_t alpha, uint32_t seed) {
  uint16_t col = blend565(0xFFFF, 0xF800, alpha);              // white -> red as it fades in
  uint32_t s = seed;
  auto rnd = [&](int n){ s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (int)(s % (uint32_t)n); };
  int veins = 9 + rnd(4);                                      // 9-12
  for (int v = 0; v < veins; v++) {
    int ang = rnd(256), r = R - 2;
    int px = cx + fastCos(ang) * r / 127, py = cy + fastSin(ang) * r / 127;
    int segs = 3 + rnd(2);
    for (int k = 0; k < segs && r > R / 3; k++) {
      ang += rnd(21) - 10;                                     // wobble
      r   -= R / (segs + 1) - rnd(3);
      int nx = cx + fastCos(ang) * r / 127, ny = cy + fastSin(ang) * r / 127;
      canvas->drawLine(px, py, nx, ny, col);
      if (rnd(3) == 0) {                                       // occasional short branch
        int ba = ang + rnd(31) - 15, br = r + 6;
        canvas->drawLine(px, py, cx + fastCos(ba) * br / 127, cy + fastSin(ba) * br / 127, col);
      }
      px = nx; py = ny;
    }
  }
}

void renderEye(int theme, uint32_t now) {
    if (eyeEvent == EV_ROLL) {                                   // scripted: iris/pupil roll in place (ball + lids stay put)
      if (now - eventStart >= ROLL_MS) { curX = curY = 120; finishEyeEvent(now, 400); }
      curX = curY = 120;                                          // hold the eyeball centered; orbit is applied below
    }
    else if (eyeEvent == EV_SCAN) {
      if (now - eventStart >= SCAN_MS) finishEyeEvent(now, moodPauseMs());
    }
    else if (eyeEvent == EV_SQUINT) {                            // hold gaze; lids + pupil + jitter do the work
      if (now - eventStart >= SQUINT_MS) finishEyeEvent(now, moodPauseMs());
    }
    else if (eyeEvent == EV_DOUBLE_TAKE) {                       // dart away -> hold -> whip back wide-eyed
      uint32_t t = now - eventStart;
      if (t >= DOUBLE_TAKE_MS) { curX = dtOriginX; curY = dtOriginY; finishEyeEvent(now, moodPauseMs()); }
      else if (t < 150)  { float k = t / 150.0f;         curX = dtOriginX + (dtAwayX - dtOriginX) * k; curY = dtOriginY + (dtAwayY - dtOriginY) * k; }
      else if (t < 330)  { curX = dtAwayX; curY = dtAwayY; }     // linger on the distraction
      else if (t < 390)  { float k = (t - 330) / 60.0f;  curX = dtAwayX + (dtOriginX - dtAwayX) * k; curY = dtAwayY + (dtOriginY - dtAwayY) * k; }  // 60ms whip
      else               { curX = dtOriginX; curY = dtOriginY; } // settled; lids stay wide (drawEyelids)
    }
    else if (eyeEvent == EV_DROWSY) {
      uint32_t t = now - eventStart;
      if (t >= DROWSY_MS) {
        curX = curY = 120;
        finishEyeEvent(now, moodPauseMs());
      } else {
        uint32_t half = DROWSY_MS / 2;
        int drop = t < half ? (int)(t * 36 / half) : 36 - (int)((t - half) * 36 / half);
        curX += (120.0f - curX) * 0.08f;
        curY = 120 + drop;
      }
    }
    else if (eyeEvent == EV_WANDER_OFF) {                        // eye centered; the frame roll (end of fn) does the travel
      curX = curY = 120;
      if (now - eventStart >= WANDER_MS) finishEyeEvent(now, moodPauseMs());
    }
    else if (eyeEvent == EV_DRIFT) {                             // ball centered; iris/pupil overridden below
      curX = curY = 120;
      if (now - eventStart >= DRIFT_MS) finishEyeEvent(now, moodPauseMs());
    }
    else if (eyeEvent == EV_MICROSLEEP) {                        // droop -> sealed ~3s -> ease awake
      uint32_t t = now - eventStart;
      if (t >= MICRO_MS) { curX = curY = 120; finishEyeEvent(now, moodPauseMs()); }
      else if (t < 700)  { curX += (120.0f - curX) * 0.1f; curY = 120 + (int)(t * 30 / 700); }   // droop down
      else if (t < MICRO_MS - 500) { curX += (120.0f - curX) * 0.1f; curY = 150; }               // sleep: parked low
      else               { curY = 150 - (int)((t - (MICRO_MS - 500)) * 30 / 500); curX = 120; }  // wake: rise to center
    }
    else if (isJittering) { if (now > jitterEndTime) isJittering = false; else { curX = 120 + random(-40, 40); curY = 120 + random(-40, 40); } }
    else {
      if (eyeState == MOVING) {
        float dx = tarX - curX, dy = tarY - curY, dist = sqrt(dx*dx + dy*dy);
        if (dist < 1.0f || now > stateEndTime) {
          curX = tarX; curY = tarY;
          eyeState = PAUSING;
          pauseStartTime = now;
          microUsedThisPause = false;
          nextMicroSaccade = now + random(1800, 3600);
          stateEndTime = now + (sideEye ? moodSideEyePauseMs() : moodPauseMs());  // linger on a deliberate side-eye
          if (!sideEye && snapSpeed > 24.0f && random(100) < moodLandingBlinkChance()) eyeBlinkRequest = true;
        }
        else {
          if (dist > snapSpeed) { curX += (dx/dist)*snapSpeed; curY += (dy/dist)*snapSpeed; }
          else { curX = tarX; curY = tarY; }
        }
      } else {
        if (now > stateEndTime) {
          bool wasSpecial = sideEye; sideEye = false;            // a side-eye hold just ended -> resume normal
          if (!wasSpecial && maybeStartRareEvent(now)) {         // super-rare: wander-off / drift / microsleep
            // The event renderer takes over until finishEyeEvent().
          } else if (!wasSpecial && maybeStartEyeEvent(now)) {   // rare: roll, scan, or drowsy drift
            // The event renderer takes over until finishEyeEvent().
          } else if (!wasSpecial && random(100) < moodSideEyeChance()) {          // occasional: deliberate side-eye, snap + linger
            sideEye = true; tarX = (random(2) ? 45.0f : 195.0f); tarY = 120 + random(-12, 12);
            snapSpeed = 40.0f * moodSpeedPct() / 100.0f;
            if (snapSpeed < 24.0f) snapSpeed = 24.0f;
            eyeBlinkRequest = random(100) < 45;
            eyeState = MOVING; stateEndTime = now + 1500;
          } else {
            isWideMovement = random(100) < moodWideMoveChance(); float newTarX, newTarY;
            if (!isWideMovement) {
              newTarX = constrain(curX + random(-35, 35), 35, 205);
              newTarY = constrain(curY + random(-35, 35), 35, 205);
              snapSpeed = (float)random(100, 400) / 10.0f;
            } else {
              do {
                newTarX = random(40, 200); newTarY = random(40, 200);
                float tdx = newTarX - curX, tdy = newTarY - curY;
                if (sqrt(tdx*tdx + tdy*tdy) > 20.0f) break;
              } while(true);
              int rSpd = random(100);
              if (rSpd < 30) snapSpeed = (float)random(30, 80) / 10.0f;
              else if (rSpd < 70) snapSpeed = (float)random(120, 300) / 10.0f;
              else snapSpeed = (float)random(400, 700) / 10.0f;
            }
            snapSpeed = snapSpeed * moodSpeedPct() / 100.0f;
            if (snapSpeed < 1.5f) snapSpeed = 1.5f;
            tarX = newTarX; tarY = newTarY; eyeState = MOVING; stateEndTime = now + 1500;
          }
        }
      }
    }
    float gzX = (curX-120), gzY = (curY-120);
    int irX = 120+(int)(gzX*0.4), irY = 120+(int)(gzY*0.4);
    int puX = 120+(int)(gzX*0.7), puY = 120+(int)(gzY*0.6);   // vertical rides lower than horizontal so the iris doesn't outrun the top lid on an up-glance
    if (eyeEvent == EV_ROLL) {                                   // iris/pupil orbit the fixed ball, then spiral home to center
      uint32_t t = now - eventStart;
      int ang = (int)(256 * t / ROLL_MS) - 64;
      int R = t > ROLL_MS * 7 / 10 ? 34 * (ROLL_MS - t) / (ROLL_MS * 3 / 10) : 34;  // shrink radius to 0 over the last 30%
      puX = 120 + fastCos(ang) * R / 127; puY = 120 + fastSin(ang) * R / 127;
    } else if (eyeEvent == EV_DRIFT) {                           // iris + pupil each slide out their own way, behind the lids, and back
      uint32_t t = now - eventStart;
      int env = fastSin((int)(128 * t / DRIFT_MS));              // sin(pi*p): 0 -> peak -> 0, lands home
      int ir = 46 * env / 127, pu = 52 * env / 127;              // pupil reaches a touch further under the lid than the iris
      irX = 120 + fastCos(driftIrisAng)  * ir / 127; irY = 120 + fastSin(driftIrisAng)  * ir / 127;
      puX = 120 + fastCos(driftPupilAng) * pu / 127; puY = 120 + fastSin(driftPupilAng) * pu / 127;
    } else {
      applyScanOffset(now, puX, puY);
      applyMicroSaccade(now, puX, puY);
      applySquintInspect(now, puX, puY);
    }

    switch(theme) {
      case 0: for(int i=0; i<15; i++) canvas->drawCircle(120, 120, (int)(now/10 + i*20) % 118, pColor(10, i*15)); canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 1: canvas->fillCircle(120, 120, 118, pColor(2, 0)); if(random(0,10)>7) canvas->fillRect(0, random(240), 240, 8, 0xFFFF); canvas->fillCircle(irX+random(-8,8), irY, 70, 0xFFFF); break;
      case 2: for(int i=0; i<20; i++) { int angle = (now / 8) + (i * 12); canvas->drawCircle(120 + (fastCos(angle)*20/127), 120 + (fastSin(angle)*20/127), 118-i*5, pColor(6, i*10)); } canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 3: { int bt = (fastSin(now / 4) + 127) * 20 / 254; canvas->fillCircle(120, 120, 100+bt, pColor(3, 0)); canvas->fillCircle(120, 120, 80+bt, 0); } canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 4: for(int x=0; x<240; x+=20) { for(int y=0; y<240; y+=20) canvas->drawRect(x, y, 18, 18, pColor(20, x+y)); } canvas->fillCircle(irX, irY, 60, 0); canvas->fillCircle(irX, irY, 55, 0xFFFF); break;
      case 5: for(int i=0; i<400; i++) canvas->drawPixel(frand() % 240, frand() % 240, (uint16_t)frand()); canvas->fillCircle(irX, irY, 70, 0xFFFF); break;   // frand, not random(): see frand() -- esp_random() was ~25ms/frame here
      case 6: for(int i=0; i<12; i++) canvas->fillCircle(120, 120, 118-(i*10), (i+now/100)%2==0 ? pColor(5,0):0); canvas->fillCircle(irX, irY, 70, 0xFFFF); break;
      case 7: canvas->fillCircle(120, 120, 118, gConfig.voidColor); canvas->fillCircle(irX, irY, 80, 0xFFFF); break;
      case 8: canvas->fillRect(irX-60, irY-60, 120, 120, 0xFFFF); canvas->fillRect(puX-25, puY-25, 50, 50, gConfig.irisColor); canvas->fillRect(puX-15, puY-15, 30, 30, 0); break;
      case 9: canvas->fillCircle(120, 120, 118, 0xF81F); canvas->fillCircle(irX, irY, 80, 0xFFFF); break;
      case 10: for(int i=0; i<100; i+=10) canvas->fillRect(random(240), random(240), 10, 10, pColor(1, random(255))); canvas->fillCircle(irX, irY, 75, 0xFFFF); break;
      case 11: // --- AZTEC DIAMOND (Experimental) ---
        for(int i=0; i<8; i++) {
          int s = 120 - (i*15) - (now/20 % 15);
          if(s>0) {
            uint16_t c = (i%2==0) ? 0x8410 : 0x4208; // Stone-like grey steps
            canvas->drawRect(120-s, 120-s, s*2, s*2, c);
            canvas->drawLine(120-s, 120-s, 120+s, 120+s, c);
            canvas->drawLine(120+s, 120-s, 120-s, 120+s, c);
          }
        }
        for(int i=0; i<4; i++) {
          int a = (now/10) + (i*64);
          canvas->drawLine(120, 120, 120+(fastCos(a)*100/127), 120+(fastSin(a)*100/127), 0xFD20);
        }
        canvas->fillCircle(irX, irY, 70, 0xFFFF);
        break;
      case 12: for (int x = 0; x < 240; x += 15) { for (int y = 0; y < 240; y += 15) { int v = fastSin(x + now/16) + fastSin(y + now/12); canvas->fillRect(x, y, 15, 15, pColor(v / 8 + 5, x + y)); } } canvas->fillCircle(irX, irY, 70, 0xFFFF); break;
    }

    // Lid disc radius per theme. Back the aperture with white so no theme background leaks,
    // but 2px shy of the lid radius: fillCircle and the lids' integer-sqrt rasterizer round
    // differently at the top/bottom tangent, so the lids must slightly overdraw the white rim
    // (theme 8 draws its own square, excluded here).
    static const uint8_t kLidR[13] = {75, 80, 75, 75, 65, 80, 80, 90, 85, 90, 85, 80, 80};
    if (theme != 8) canvas->fillCircle(irX, irY, kLidR[theme] - 2, 0xFFFF);
    if (eyeEvent == EV_MICROSLEEP && theme != 8) {              // bloodshot fades in while the lids are still open
      uint32_t t = now - eventStart;
      if (t < 800) drawBloodshot(irX, irY, kLidR[theme] - 2, (uint8_t)(t < 450 ? t * 255 / 450 : 255), bloodshotSeed);
    }

    // MANDATORY IRIS AND PUPIL LAYER (Except theme 8)
    if (theme != 8) {
      // 1. Draw Iris (Custom colors for specific modes)
      uint16_t irisCol;
      if (theme == 9) irisCol = 0xF81F;      // Magenta
      else if (theme == 11) irisCol = 0xFD20; // Aztec Gold
      else if (theme == 7) irisCol = gConfig.voidColor; // Void: iris matches bg -> invisible, just the sclera ring
      else irisCol = gConfig.irisColor;      // configurable tint (standard themes)

      int irisRadius = (theme == 4) ? 25 : 38;
      // Pupil dilation: pick a new size every ~20-30s (2-3/min) and hold it, easing between.
      static uint32_t nextPupil = 0; static int pupilTar = 20, pupilCur = 20;
      if (now >= nextPupil) { pupilTar = random(15, 28); nextPupil = now + random(20000, 30000); }
      int aim = moodPupilAim(pupilTar);  // mood/events shape the slow random pupil breath
      int d = aim - pupilCur;
      pupilCur += (d / 8 != 0) ? d / 8 : ((d > 0) - (d < 0));                 // ease toward it (min +-1/frame)
      int pupilRadius = (theme == 4) ? 12 : pupilCur;
      int glintSize = (theme == 4) ? 4 : 6;
      int glintOff = (theme == 4) ? 6 : 10;

      if (theme == 11) { // Diamond Shape for Aztec
        for(int i=0; i<irisRadius; i++) canvas->drawLine(puX-i, puY-(irisRadius-i), puX+i, puY-(irisRadius-i), irisCol);
        for(int i=0; i<irisRadius; i++) canvas->drawLine(puX-i, puY+(irisRadius-i), puX+i, puY+(irisRadius-i), irisCol);
        for(int i=0; i<pupilRadius; i++) canvas->drawLine(puX-i, puY-(pupilRadius-i), puX+i, puY-(pupilRadius-i), 0);
        for(int i=0; i<pupilRadius; i++) canvas->drawLine(puX-i, puY+(pupilRadius-i), puX+i, puY+(pupilRadius-i), 0);
      } else {
        canvas->fillCircle(puX, puY, irisRadius, irisCol);
        canvas->fillCircle(puX, puY, pupilRadius, 0);
      }

      // 3. Draw Reflection (The white glint)
      int glintTarX = puX - glintOff, glintTarY = puY - glintOff;
      if (!glintInit) { glintX = glintTarX; glintY = glintTarY; glintInit = true; }
      glintX = easeInt(glintX, glintTarX, 3);
      glintY = easeInt(glintY, glintTarY, 3);
      canvas->fillCircle(glintX, glintY, glintSize, 0xFFFF);
    }
    // lids ride the floating eyeball, on top of every eye theme. The box eye (8) is made of
    // squares, so its lids are flat rectangular slabs sized to the 120px square (half-extent 60).
    if (gConfig.eyelids) {
      if (theme == 8) drawEyelids(now, irX, irY, 60, true);
      else            drawEyelids(now, irX, irY, kLidR[theme]);
    }
    if (eyeEvent == EV_WANDER_OFF) {                             // slide the finished frame off + wrap in from the opposite edge
      uint32_t t = now - eventStart;
      float p = (float)t / WANDER_MS; if (p > 1.0f) p = 1.0f;
      float e = p < 0.5f ? 2*p*p : 1 - 2*(1-p)*(1-p);           // ease in-out
      int off = (int)(SCREEN_RES * e + 0.5f);
      rollFramebuffer(canvas->getFramebuffer(), SCREEN_RES, SCREEN_RES, rareDirX * off, rareDirY * off);
    }
}

#if OCELLUS_AUDIO
// Particles are pinned to WALL-CLOCK, not to frames. They used to advance a fixed step per frame
// (r += 6, life -= 8), which silently made every tuning constant a function of the render path's
// speed: moving the framebuffer out of PSRAM took the mode 37 -> 60fps and, with it, made every ring
// 1.6x faster and shorter-lived than what it was tuned against. Position and brightness now derive
// from a particle's age in ms, so the next perf change (or a slow frame) can't retune the mode.
// Each particle stores only its birth time and birth radius; everything else is computed from age.
struct BloomRing  { uint32_t t0; int16_t r0; bool on; bool in; };   // `in` = collapse inward (snare) instead of expanding (kick)
struct BloomSpark { uint32_t t0; int16_t r0; uint8_t angle; bool on; };
// 14 slots, not 8: saturated detectors can hold RING_MS/BEAT_REFRACTORY_MS = 8 kick rings and
// SNARE_RING_MS/SNARE_REFRACTORY_MS = 6 snare rings alive at once. At 8 the overflow wasn't merely
// rare, it was BIASED -- the kick's slot is allocated first below, so under a full pool the snare
// is the one that silently never draws. If any of those four constants moves, so does this number.
static BloomRing    bloomRings[14]  = {};
static BloomSpark   bloomSparks[24] = {};
static uint32_t     bloomLastMs = 0;   // previous frame's timestamp, for the elapsed-ms core smoothing

constexpr uint32_t RING_MS  = 900;   // birth -> bezel. The frame-indexed version reached the edge in
                                     // ~540ms at the 37fps it was tuned at, ~330ms once we hit 60fps.
constexpr uint32_t SNARE_RING_MS = 600;   // bezel -> core. Shorter than the kick's 900: a snare should feel sharper.
constexpr uint32_t SPARK_MS = 420;   // sparks are accents; they should beat the rings out and vanish.
constexpr uint32_t ATTACK_MS  = 30;  // the core swells fast...
constexpr uint32_t RELEASE_MS = 140; // ...and settles slow. Asymmetric on purpose: a kick should punch,
                                     // not glide, and a symmetric EMA makes loud passages one flat blob.

// First-order lag toward `target` over time-constant tau. Frame-rate independent: a long frame moves
// proportionally further, and dt >= tau just snaps (the old `x += (target-x)/4` moved a fixed FRACTION
// per frame, so its real time-constant was whatever the frame rate happened to be that day).
static uint8_t lagTo(uint8_t cur, uint8_t target, uint32_t dt, uint32_t tau) {
  if (dt >= tau) return target;
  return (uint8_t)((int)cur + ((int)target - (int)cur) * (int)dt / (int)tau);
}

// Smoothed spectrum, shared by Radial Spectrum (id 35) and Reactive Iris (id 36). Both used to render
// RAW bins -- every bar/ray snapped straight to whatever the newest packet said, which reads as shimmer
// rather than motion (and got more visible at 60fps, since you now see more distinct values per second).
// Asymmetric, same as Bloom's core: fast attack so a bar still punches on a hit, slow release so it
// falls back instead of strobing. Wall-clock, so this can't be retuned by a future frame-rate change.
// Release is deliberately short: the CONSOLE already peak-holds every bin with a ~50ms decay envelope
// (SensoryBridge be012a2, see audio.h), so our release stacks on top of that one. 180ms here read as
// smeared/sluggish for exactly that reason.
constexpr uint32_t BIN_ATTACK_MS  = 8;
constexpr uint32_t BIN_RELEASE_MS = 60;
static uint8_t  gSmBins[64] = {};
static uint32_t gSmBinsMs = 0;
static void updateSmoothBins(const SbStreamMags& snap, uint32_t now) {
  uint32_t dt = clampDt(gSmBinsMs ? (uint32_t)(now - gSmBinsMs) : 0);
  gSmBinsMs = now;
  for (int i = 0; i < 64; i++) {
    uint8_t v = audioBin(snap.spectrogram[i]);
    gSmBins[i] = lagTo(gSmBins[i], v, dt, v > gSmBins[i] ? BIN_ATTACK_MS : BIN_RELEASE_MS);
  }
}

void resetBloom() {
  for (auto& r : bloomRings)  r.on = false;
  for (auto& s : bloomSparks) s.on = false;
  bloomLastMs = 0;                  // 0 => first frame back has dt 0, so nothing lurches on re-entry
  gBeatPend = gSnarePend = gSparkPend = false;   // the detectors themselves live on the RX side and self-heal in ~8 packets
}
#endif  // OCELLUS_AUDIO

// --- Fluid gravity mapping (shared by renderFluid and the sensor-debug screen) ---
// Hardware-validated mapping on the Waveshare board: accel Y is screen-horizontal and accel X is
// screen-vertical. Return units of g so Fluid can distinguish a shallow, face-up tilt from a strong
// orientation change. Both callers go through fluidGravity(), so debug and animation stay identical.
constexpr int8_t FLUID_AX_SIGN = -1, FLUID_AY_SIGN = 1;
constexpr float ACCEL_COUNTS_PER_G = 8192.0f;   // QMI8658 CTRL2=+/-4g
static inline void fluidGravity(int16_t ax, int16_t ay, float& gx, float& gy) {
  gx = FLUID_AY_SIGN * (float)ay / ACCEL_COUNTS_PER_G;   // screen-horizontal = accel Y
  gy = FLUID_AX_SIGN * (float)ax / ACCEL_COUNTS_PER_G;   // screen-vertical = accel X
}

#if defined(BOARD_WAVESHARE_128)
static fluid::Sim fluidSim;                 // continuous surface + wave state; board only
constexpr float STIR_SCALE = 0.000273f;     // gyro-Z counts -> rad/s (64 LSB per deg/s at +/-512dps)
static uint32_t fluidLastMs = 0;

void resetFluid() {
  setPanelRotation(0);                      // lock frame; no auto-flip fight
  fluid::init(fluidSim, 60, millis());   // fill %; was 40, more fluid requested (TODO)
  int16_t ax, ay, az;
  float gx, gy;
  if (imuPresent && imuReadAccel(&ax, &ay, &az)) {   // seed the pool at true down so a tilted
    fluidGravity(ax, ay, gx, gy);                    // mode entry doesn't slosh over from screen-down
    if (gx * gx + gy * gy >= fluid::DIRECT_TILT_G * fluid::DIRECT_TILT_G)
      fluidSim.angle = fluidSim.targetAngle = fluidSim.pendingAngle = atan2f(gy, gx);
  }
  fluidLastMs = 0;                          // first frame cannot inherit a stale mode-switch gap
}

void renderFluid(uint32_t now) {
  float gx = 0, gy = 0, stir = 0;           // zero on IMU read failure: step() keeps the last target
  int16_t ax, ay, az;
  if (imuPresent && imuReadAccel(&ax, &ay, &az)) {
    fluidGravity(ax, ay, gx, gy);           // shared accel->screen-gravity mapping
    int16_t gxr, gyr, gzr;
    if (imuReadGyro(&gxr, &gyr, &gzr)) stir = STIR_SCALE * (float)gzr;   // gyro-Z = spin about normal
  }

  float dt = fluidLastMs ? (float)(now - fluidLastMs) * 0.001f : 0.0f;
  fluidLastMs = now;
  fluid::step(fluidSim, gx, gy, stir, dt);    // internally sliced to <=1/120s; FPS-independent

  // Build one surface-height sample per tangent pixel from the sim's own basis (fluid::waveHeight),
  // so the drawn surface can never drift from the one the tests exercise.
  // 723 sinf/frame outside the pixel loop -- S3-only code; measured render 15.1 -> 9.6 ms together
  // with the air-row clip below, so the LUT micro-optimization it replaced was never the win.
  constexpr float R = 118.0f, CX = 119.5f, CY = 119.5f;
  float c = cosf(fluidSim.angle), sn = sinf(fluidSim.angle);
  float span = R * fluidSim.span;
  float surfaceByT[241];                    // tangent coordinate -120..120, in display pixels
  float minSurf = 1.0e9f;
  for (int i = 0; i <= 240; i++) {
    float t = (float)(i - 120);
    surfaceByT[i] = R * (fluidSim.level + fluid::waveHeight(fluidSim, t / span));
    if (surfaceByT[i] < minSurf) minSurf = surfaceByT[i];
  }

  // Palette-derived depth ramp: bright/clear at the meniscus, darker toward the bottom. Prebuilding
  // it avoids RGB565 channel work in the ~45k-pixel bowl loop and keeps custom palettes intact.
  uint16_t shallow = lift565(activePaletteLUT[230], 20);
  uint16_t middle  = lift565(activePaletteLUT[190], 14);
  uint16_t deep    = dim565(lift565(activePaletteLUT[145], 10), 2, 3);
  uint16_t meniscus = lift565(activePaletteLUT[255], 27);
  uint16_t depthColor[128];
  for (int d = 0; d < 128; d++)
    depthColor[d] = d < 40 ? blend565(shallow, middle, (uint8_t)(d * 255 / 40))
                           : blend565(middle, deep, (uint8_t)((d - 40) * 255 / 87));

  uint16_t* fb = canvas->getFramebuffer();
  float lineW = 1.15f + fluidSim.agitation * 0.85f;
  // Meniscus glint wanders slowly (~5 s period, riding the sim's ambient-swell phase) instead of
  // sitting pinned left-of-center -- one sinf per frame buys constant subtle motion.
  float glintC = -0.28f + 0.30f * sinf(3.0f * fluidSim.swellPhase);
  float airLimit = minSurf - 0.75f;         // down < this is air everywhere (waves only raise the surface)
  for (int y = 1; y < 239; y++) {
    float dy = (float)y - CY;
    float rr = R * R - dy * dy;
    if (rr <= 0.0f) continue;
    int hw = (int)sqrtf(rr);
    int x0 = max(1, (int)ceilf(CX - hw)), x1 = min(238, (int)floorf(CX + hw));
    // Clip the guaranteed-air span: down(x) = (x-CX)*c + dy*sn is monotone in x, so the pixels with
    // down < airLimit form a prefix (c>0) or suffix (c<0) of the row. Conservative, output-identical;
    // skips ~60% of the bowl when the pool is calm.
    if (c > 1.0e-6f) {
      float xs = CX + (airLimit - dy * sn) / c;
      if (xs > (float)x1) continue;
      if (xs > (float)x0) x0 = (int)ceilf(xs);
    } else if (c < -1.0e-6f) {
      float xs = CX + (airLimit - dy * sn) / c;
      if (xs < (float)x0) continue;
      if (xs < (float)x1) x1 = (int)floorf(xs);
    } else if (dy * sn < airLimit) continue;
    float dx = (float)x0 - CX;
    float down = dx * c + dy * sn;
    float across = -dx * sn + dy * c;
    for (int x = x0; x <= x1; x++, down += c, across -= sn) {
      float fi = across + 120.0f;
      int si = (int)fi;                      // fi is positive here, so truncation == floor
      if (si < 0) si = 0; else if (si > 239) si = 239;
      float surface = surfaceByT[si] + (surfaceByT[si + 1] - surfaceByT[si]) * (fi - si);
      float depth = down - surface;
      if (depth < -0.75f) continue;
      uint16_t col;
      if (depth < lineW) {
        float u = across / span;             // a soft glint that drifts along the surface
        float glint = max(0.0f, 1.0f - fabsf(u - glintC) * 1.7f);
        col = blend565(meniscus, 0xFFFF, (uint8_t)(30.0f + glint * 105.0f));
      } else {
        int di = (int)depth; if (di > 127) di = 127;
        col = depthColor[di];
      }
      fb[y * SCREEN_RES + x] = col;
    }
  }

  // Spray: crest-shed droplets arcing free of the pool and falling back in.
  uint16_t dropCol = lift565(activePaletteLUT[230], 26);
  for (int i = 0; i < fluid::DROP_COUNT; i++) {
    const fluid::Droplet& b = fluidSim.drops[i];
    if (b.life <= 0.0f) continue;
    int px = (int)lroundf(CX + b.x * R), py = (int)lroundf(CY + b.y * R);
    float rx = (float)px - CX, ry = (float)py - CY, lim = R - 2.0f;
    if (rx * rx + ry * ry > lim * lim) continue;
    canvas->fillCircle(px, py, 1, dropCol);
    canvas->drawPixel(px, py, meniscus);
  }
}
#else
void resetFluid() {}
void renderFluid(uint32_t /*now*/) {        // no IMU: static resting puddle in the lower bowl
  for (int y = 130; y < 240; y++) {
    int hw = (int)sqrtf((float)(120 * 120 - (y - 120) * (y - 120)));   // circle half-width at row y
    if (hw > 0) canvas->fillRect(120 - hw, y, 2 * hw, 1, activePaletteLUT[64]);
  }
}
#endif

// --- Sensor debug (registry id 42 = DEBUG_ID) ------------------------------------------------------------
// Dev screen: raw accel/gyro/battery/touch + an arrow pointing where the fluid computes "down".
// Reach it with `flash.py s3-touch --anim 42` (or {"cmd":"anim","id":42}); it's not in the button cycle.
// Board-agnostic: on boards without the IMU it just shows "IMU: none".
static const char* compass8(float gx, float gy) {
  static const char* N8[8] = { "E", "SE", "S", "SW", "W", "NW", "N", "NE" };
  return N8[((int)lroundf(atan2f(gy, gx) / 0.7853981634f)) & 7];
}
void renderSensorDebug(uint32_t now) {
  int16_t ax = 0, ay = 0, az = 0, gyx = 0, gyy = 0, gyz = 0;
  bool haveA = imuPresent && imuReadAccel(&ax, &ay, &az);
  if (imuPresent) imuReadGyro(&gyx, &gyy, &gyz);
  float gx = 0, gy = 1;
  if (haveA) fluidGravity(ax, ay, gx, gy);   // the exact vector renderFluid feeds the sim

  // Gravity arrow in the lower half: from a pivot dot toward "down". Tilt the board and it should
  // point at the physical low side; if it doesn't, the raw axes above tell you which axis/sign to fix.
  const int px = 120, py = 168, LEN = 44;
  uint16_t ac = gfx->color565(0, 255, 128);
  float mag = sqrtf(gx * gx + gy * gy);
  int len = (int)(LEN * (mag > 1.0f ? 1.0f : mag));  // pendulum length = tilt strength (gx/gy in g); flat board shrinks to the pivot
  if (len >= 4) {                                    // below ~0.09g the direction is ADC noise: pivot dot only
    int ex = (int)(px + len * gx / mag), ey = (int)(py + len * gy / mag);
    for (int o = -1; o <= 1; o++) canvas->drawLine(px, py + o, ex, ey + o, ac);   // 3px shaft
    canvas->fillCircle(ex, ey, 5, ac);                                            // arrowhead
  }
  canvas->fillCircle(px, py, 3, gfx->color565(255, 255, 255));                    // pivot

  canvas->setTextSize(1);
  int x = 30, y = 44; const int dy = 12; char buf[48];
  uint16_t hi = gfx->color565(255, 255, 255), lo = gfx->color565(120, 200, 255), warn = gfx->color565(255, 120, 120);
  #define DBG_LINE(col, ...) do { snprintf(buf, sizeof buf, __VA_ARGS__); \
    canvas->setTextColor(col); canvas->setCursor(x, y); canvas->print(buf); y += dy; } while (0)
  DBG_LINE(hi, "SENSOR DEBUG  id%d", DEBUG_ID);
  if (!imuPresent) { DBG_LINE(warn, "IMU: none"); }
  else {
    DBG_LINE(lo, "acc %6d %6d %6d", ax, ay, az);
    DBG_LINE(lo, "gyr %6d %6d %6d", gyx, gyy, gyz);
    DBG_LINE(hi, "grav %+5d,%+5d mg %s", (int)lroundf(gx * 1000), (int)lroundf(gy * 1000), compass8(gx, gy));
  }
  int bmv = gBatSimMv ? gBatSimMv : readBatteryMv();   // what the monitor is actually fed (batsim-aware)
  static const char* BS[] = { "NORM", "LOW", "CUTOFF" };
  DBG_LINE(lo, "bat %4d ema %4d mV%s", bmv, gBatt.emaMv(), gBatSimMv ? " SIM" : "");
  DBG_LINE(lo, "    %s  usb %s", BS[gBatt.state()], gBatt.usbPowered() ? "yes" : "no");
  const char* gname = "-";
  if (now - g_lastGestureMs < 1500) {
    TouchGesture t = (TouchGesture)g_lastGesture;
    gname = t == TOUCH_TAP ? "tap" : t == TOUCH_SWIPE_LEFT ? "swipeL" : t == TOUCH_SWIPE_RIGHT ? "swipeR"
          : t == TOUCH_SWIPE_UP ? "swipeU" : t == TOUCH_SWIPE_DOWN ? "swipeD" : "-";
  }
  DBG_LINE(lo, "tch %s  %s", touchPresent ? "ok" : "none", gname);
  #undef DBG_LINE
}

#if OCELLUS_AUDIO
// Audio-reactive bloom (registry id 34). Reads the freshest ESP-NOW spectrum; when no packet
// has arrived for ~500ms, self-animates a gentle idle breath so it's alive without a console.
void renderBloom(uint32_t now) {
  SbStreamMags snap; memcpy(&snap, &gAudioRx, sizeof snap);   // lock-free snapshot
  bool live = !audioStale(gAudioRxMillis, now);  // 500ms default; audioStale uses signed diff so a mid-frame packet (lastRx just ahead of now) doesn't underflow to "stale"
  if (live) lastInteractionTime = now;           // streaming audio counts as activity -> visualizer won't deep-sleep mid-song (sleeps sleepMin after the console stops)
  uint8_t bass, level; int hueOff;   // the bands that shape the core; transients are detected RX-side
  if (live) {
    BloomParams p = agcStretch(bloomParamsFromMags(snap), now);   // contrast-stretch pinned material (TODO #9)
    bass = p.bass; level = p.level;
    hueOff = gSbActive ? gSbHueSlew.update(sbHueTarget(snap), now)
                       : gHueSlew.update(snap.hue_shift * 40.0f, now);   // console color drift, slew-capped (see HueSlew)
  } else {
    float idle = (float)((now / 40) & 255);             // hue keeps its idle walk (v1)
    BloomParams p = bloomParamsFromMags(gDemoSnap);     // demo spectrum -> bands (no live AGC)
    bass = p.bass; level = p.level;
    hueOff = gSbActive ? gSbHueSlew.update(idle, now) : gHueSlew.update(idle, now);
  }
  // Snapshot-and-clear the real transients every frame (RX only sets them); use the real ones when
  // live, the synth's when idle. One RX event fires once; a demo frame leaves nothing pending.
  bool rbeat = gBeatPend, rsnare = gSnarePend, rspark = gSparkPend;
  gBeatPend = gSnarePend = gSparkPend = false;
  bool beat  = live ? rbeat  : gDemoBeat;
  bool snare = live ? rsnare : gDemoSnare;
  bool spark = live ? rspark : gDemoSpark;

  // Elapsed-ms core smoothing (see lagTo). Clamped: a mode switch or a long config write must not
  // hand us a 2-second dt and teleport the core to its target in one visible jump.
  uint32_t dt = clampDt(bloomLastMs ? (uint32_t)(now - bloomLastMs) : 0);
  bloomLastMs = now;

  static uint8_t smLevel = 0, smBass = 0;
  smLevel = lagTo(smLevel, level, dt, level > smLevel ? ATTACK_MS : RELEASE_MS);
  smBass  = lagTo(smBass,  bass,  dt, bass  > smBass  ? ATTACK_MS : RELEASE_MS);
  int coreR = 12 + (int)smLevel * 66 / 255 + (int)smBass * 26 / 255;   // ~12..104
  for (int rr = coreR; rr > 0; rr -= 4)                            // layered glow, palette-colored
    canvas->fillCircle(120, 120, rr, lift565(pColor(4, hueOff + hspread((coreR - rr) * 3)), 12));  // never too dark to see

  if (beat)                                                        // kick -> ring pushed OUT from the core
    for (auto& r : bloomRings) if (!r.on) { r.on = true; r.in = false; r.t0 = now; r.r0 = (int16_t)coreR; break; }
  if (snare)                                                       // snare -> ring pulled IN from the bezel
    for (auto& r : bloomRings) if (!r.on) { r.on = true; r.in = true;  r.t0 = now; r.r0 = (int16_t)coreR; break; }
  for (auto& r : bloomRings) if (r.on) {
    uint32_t life = r.in ? SNARE_RING_MS : RING_MS;
    uint32_t age  = (uint32_t)(now - r.t0);
    if (age >= life) { r.on = false; continue; }
    int travel = (int)(((int32_t)(120 - r.r0) * (int32_t)age) / (int32_t)life);
    int rad    = r.in ? 120 - travel      // bezel -> core over SNARE_RING_MS
                      : r.r0 + travel;    // core  -> bezel over RING_MS
    // Fade out over the ring's life. Frame-indexed rings just stopped being drawn at full brightness,
    // which pops; dimming to black means they arrive already gone.
    uint8_t fade = (uint8_t)(255 - age * 255 / life);
    // Snare rings sit half a wheel away from kick rings: pColor's first arg is a time DIVISOR, not a
    // palette stop, and under sbPalette (gSbActive) it's ignored entirely -- the LUT is read by offset
    // alone -- so a stop-index scheme would give both rings the same colour whenever SB mode is on
    // (and periodically even when it's off, since millis()/24 mod 256 recycles every ~6s). Offsetting
    // hueOff instead works in both regimes: a collapsing ring must never read as an expanding one at
    // a glance, and direction alone is too subtle when several are in flight.
    canvas->drawCircle(120, 120, rad, dim565(lift565(pColor(6, hueOff + (r.in ? 128 : 0)), 12), fade, 255));
  }

  if (spark)                                                       // treble EDGES -> sparks (a level threshold over-fires under the console's peak-hold; see audio.h)
    for (int k = 0; k < 2; k++)
      for (auto& s : bloomSparks) if (!s.on) { s.on = true; s.t0 = now; s.r0 = (int16_t)coreR; s.angle = (uint8_t)random(256); break; }
  for (auto& s : bloomSparks) if (s.on) {
    uint32_t age = (uint32_t)(now - s.t0);
    if (age >= SPARK_MS) { s.on = false; continue; }
    int rad = s.r0 + (int)(((int32_t)(120 - s.r0) * (int32_t)age) / (int32_t)SPARK_MS);
    int x = 120 + fastCos(s.angle) * rad / 127;
    int y = 120 + fastSin(s.angle) * rad / 127;
    // Lifted harder than the core/rings (12): a spark's hue comes from its random angle, so it can
    // land on a near-black palette stop and vanish -- and it's a 2px dot, so it has no area to be
    // read by. It's the brightest accent in the mode; give it the brightest floor.
    uint8_t fade = (uint8_t)(255 - age * 255 / SPARK_MS);
    canvas->fillCircle(x, y, 2, dim565(lift565(pColor(4, hueOff + hspread(s.angle)), 24), fade, 255));
  }
}

// id 35 --- Radial Spectrum: 64 spectrogram bins as a mirrored radial bar ring + bass-pulsed core.
// Stateless (recomputed each frame from the snapshot) so onAnimEnter needs no reset.
void renderRadialSpectrum(uint32_t now) {
  SbStreamMags snap; memcpy(&snap, &gAudioRx, sizeof snap);   // lock-free snapshot
  bool live = !audioStale(gAudioRxMillis, now);
  if (live) lastInteractionTime = now;                        // streaming audio counts as activity (see renderBloom)
  int hueOff = gSbActive
      ? gSbHueSlew.update(live ? sbHueTarget(snap) : (float)((now / 40) & 255), now)
      : gHueSlew.update(live ? snap.hue_shift * 40.0f : (float)((now / 40) & 255), now);
  if (live) updateSmoothBins(snap, now);                      // raw bins shimmer; smooth them (see updateSmoothBins)

  const int inner = 26, maxLen = 82;
  uint8_t bassAcc = 0;
  for (int i = 0; i < 64; i++) {
    int bin = i < 32 ? i * 2 : (63 - i) * 2;                  // mirror L/R about the vertical axis
    uint8_t v = live ? gSmBins[bin]
                     : audioBin(gDemoSnap.spectrogram[bin]);   // idle: synthetic demo spectrum
    if (bin < 8 && v > bassAcc) bassAcc = v;                  // low bins drive the core
    int len = inner + (int)v * maxLen / 255;
    int a = i * 4 - 64;                                       // byte-angle around the circle, bar 0 at top
    canvas->drawLine(120 + fastCos(a) * inner / 127, 120 + fastSin(a) * inner / 127,
                     120 + fastCos(a) * len   / 127, 120 + fastSin(a) * len   / 127,
                     pColor(6, hueOff + hspread(i * 3)));
  }
  int cr = 10 + (int)bassAcc * 18 / 255;                      // bass-pulsed core (layered like Bloom)
  for (int rr = cr; rr > 0; rr -= 3)
    canvas->fillCircle(120, 120, rr, pColor(4, hueOff + hspread((cr - rr) * 4)));
}
#endif  // OCELLUS_AUDIO (dim565 below is shared with the non-audio modes)

// Scale an RGB565 color's brightness by num/den --- the palette LUT has no dim tones, so this lets the
// iris body sit dark under bright fibers.
static inline uint16_t dim565(uint16_t c, uint8_t num, uint8_t den) {
  uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return ((r * num / den) << 11) | ((g * num / den) << 5) | (b * num / den);
}

#if OCELLUS_AUDIO
// id 36 --- Reactive Iris: spectrum as rays from the display edge inward (length = per-bin energy, wide
// dynamic range) converging on a soft center aura that breathes with the mix.
void renderReactiveIris(uint32_t now) {
  SbStreamMags snap; memcpy(&snap, &gAudioRx, sizeof snap);   // lock-free snapshot
  bool live = !audioStale(gAudioRxMillis, now);
  if (live) lastInteractionTime = now;                        // streaming audio counts as activity
  int hueOff = gSbActive
      ? gSbHueSlew.update(live ? sbHueTarget(snap) : (float)((now / 40) & 255), now)
      : gHueSlew.update(live ? snap.hue_shift * 40.0f : (float)((now / 40) & 255), now);
  uint8_t bass, level;
  if (live) { BloomParams p = agcStretch(bloomParamsFromMags(snap), now); bass = p.bass; level = p.level; }  // same stretch as Bloom
  else      { BloomParams p = bloomParamsFromMags(gDemoSnap); bass = p.bass; level = p.level; }  // demo spectrum -> bands
  // Same wall-clock smoothing as Bloom's core -- a per-frame fraction would make the aura's response
  // time drift with the render path's speed (this mode runs ~53fps, but that is not a constant).
  static uint8_t auraEMA = 0; static uint32_t auraLastMs = 0;
  uint32_t adt = clampDt(auraLastMs ? (uint32_t)(now - auraLastMs) : 0);
  auraLastMs = now;
  auraEMA = lagTo(auraEMA, level, adt, level > auraEMA ? ATTACK_MS : RELEASE_MS);   // smooth the center aura (bands are noisy)

  if (live) updateSmoothBins(snap, now);                     // raw bins shimmer; smooth them (see updateSmoothBins)

  const int irisR = 118;                                     // rays originate at the very display edge
  canvas->fillCircle(120, 120, irisR, dim565(pColor(20, hueOff), 1, 5));   // dark field
  for (int i = 0; i < 96; i++) {                             // rays = spectrum
    int a = i * 256 / 96;                                     // byte-angle, 96 rays around
    uint8_t v = live ? gSmBins[i * 64 / 96]
                     : audioBin(gDemoSnap.spectrogram[i * 64 / 96]);   // idle: synthetic demo spectrum
    int innerR = 6 + (255 - (int)v) * 100 / 255;             // loud -> long ray to center; quiet -> short stub (wide range)
    int outerR = irisR - 3;
    if (innerR < outerR)
      canvas->drawLine(120 + fastCos(a) * innerR / 127, 120 + fastSin(a) * innerR / 127,
                       120 + fastCos(a) * outerR / 127, 120 + fastSin(a) * outerR / 127,
                       pColor(5, hueOff + hspread(i * 3)));
  }
  int auraR = 9 + (int)auraEMA * 27 / 255;                   // center aura (2/3 size); brightness falls off outward -> diffuse glow
  for (int rr = auraR; rr > 0; rr--)
    canvas->fillCircle(120, 120, rr, dim565(pColor(4, hueOff), (uint8_t)(auraR - rr + 1), (uint8_t)auraR));
  canvas->drawCircle(120, 120, irisR - 1, pColor(3, hueOff));               // limbal rim at the edge
  // Accent rim gates on the SMOOTHED level: the raw stretched band swings ~5x harder around any
  // fixed threshold (AGC gain), so an unsmoothed test strobes the second ring at frame rate.
  if (auraEMA > 130) canvas->drawCircle(120, 120, irisR - 2, pColor(3, hueOff));
}

// id 43 --- Audio Debug: what the console is ACTUALLY delivering, in numbers. Sender-side changes
// (SensoryBridge TARGET_HZ send divider, peak-hold envelope) land on our tuned thresholds, so this
// screen reports the two things that decide whether they still hold:
//   * rate  -- pkt/s should equal the sender's TARGET_HZ (175 as of SB be5609b); short = packet loss.
//   * hold  -- the bass trace shows spikes vs plateaus. The envelope (SB be012a2) makes the sender
//              peak-hold each bin with a ~50ms decay, which lifts the floor BeatDetector measures its
//              +30 rising edge against, and keeps `high` above the 165 spark threshold longer.
// beat/s and spark% run the exact rules renderBloom uses (audio.cpp:35, main.cpp spark trigger), so a
// retune is a numbers question: sparks were tuned to fire on ~9% of frames.
// Reach it with `flash.py s3-touch --anim 43`; not in the button cycle.
void renderAudioDebug(uint32_t now) {
  SbStreamMags snap; memcpy(&snap, &gAudioRx, sizeof snap);   // lock-free snapshot (see renderBloom)
  bool live = !audioStale(gAudioRxMillis, now);
  // Stale transitions while this screen has been on-screen: gap max only remembers ~1.5s of ring;
  // "it dropped out 4 times in the last minute" needs a counter. Not "since boot" -- dropouts during
  // other modes are invisible to it (the counter only advances while id 43 is being rendered).
  // wasLive seeds from THIS frame's live state (function-local static init runs once, on the first
  // call), not a hardcoded true -- booting straight into a dead link must not count as a transition.
  static bool wasLive = live;
  static uint16_t staleCount = 0;
  if (wasLive && !live) staleCount++;
  wasLive = live;
  // The WiFi task stamps gAudioRxMillis, so a packet landing mid-frame is stamped AFTER the `now` the
  // render loop is holding -- the unsigned subtraction then wraps to ~4.29e9. The old
  // `(uint32_t)(int32_t)(now - gAudioRxMillis)` claimed to fix that but round-trips to the same bits and
  // did nothing; the negative has to be CLAMPED, not re-cast. It rendered as a 10-digit age on screen.
  int32_t dAge = (int32_t)(now - gAudioRxMillis);
  uint32_t age = dAge < 0 ? 0 : (uint32_t)dAge;   // a packet from the future is 0ms old
  BloomParams p = live ? bloomParamsFromMags(snap) : BloomParams{0, 0, 0, 0};
  // AGC-stretched view of bass/level (the values Bloom/Iris actually render) -- the `s` column.
  // Updated here too, so this screen is the live tuning instrument for AGC_SPAN_FLOOR/AGC_RELAX_PER_S.
  // No `s` on the high row: nothing renders a stretched high (see agcStretch).
  uint8_t aB = 0, aL = 0;
  if (live) { BloomParams s = agcStretch(p, now); aB = s.bass; aL = s.level; }

  // Accumulate over a 1s window; publish the completed window so the digits are readable.
  // Beats/sparks are counted by DIFFERENCING the RX-side counters, not by re-running detectors here:
  // this screen exists to report what Bloom actually fires, and Bloom now consumes the transients the
  // WiFi task finds per packet (onEspNowRecv). A second frame-sampled detector on this screen would
  // report the very undercount that fix removed. Rates are per SECOND now, not per frame ("spark %" of
  // frames was only meaningful while detection was frame-locked).
  static uint32_t winStart = 0, winPkt = 0, winBeat = 0, winSpark = 0, winSnare = 0, winVeto = 0, winRefr = 0;
  static uint16_t frames = 0;
  static uint8_t maxBass = 0, maxMid = 0, maxHigh = 0, maxLevel = 0;
  static uint32_t maxAge = 0;
  static uint16_t outPkt = 0, outFps = 0, outBeats = 0, outSpark = 0, outSnare = 0, outVeto = 0, outRefr = 0;   // last completed window
  static uint8_t outMaxBass = 0, outMaxMid = 0, outMaxHigh = 0, outMaxLevel = 0;
  static uint32_t outMaxAge = 0;
  static uint16_t outLost = 0;

  // Quantiles of the INTER-PACKET GAP (filled by the WiFi RX callback -- see gGapRing), not of the
  // frame-sampled `age`. A max alone says a blip happened; it can't say how OFTEN -- one stall a minute
  // and one a second give the same max. p99 is the number that separates them. p50 should sit on the
  // send interval (~5.7ms at 176/s); anything well above it in the tail is packets that never arrived.
  static uint16_t gapSort[GAPN];
  static uint16_t outP50 = 0, outP90 = 0, outP99 = 0, outGapMax = 0;

  frames++;
  if (p.bass  > maxBass)  maxBass  = p.bass;
  if (p.mid   > maxMid)   maxMid   = p.mid;
  if (p.high  > maxHigh)  maxHigh  = p.high;
  if (p.level > maxLevel) maxLevel = p.level;
  if (age     > maxAge)   maxAge   = age;

  if ((uint32_t)(now - winStart) >= 1000) {
    uint32_t pkt = gAudioRxCount, bt = gBeatCount, sp = gSparkCount,
             sn = gSnareCount, vt = gSnareVeto, rf = gSnareRefr;
    outPkt   = (uint16_t)(pkt - winPkt);     // window is ~1s, so count == rate closely enough to read
    outBeats = (uint16_t)(bt  - winBeat);
    outSpark = (uint16_t)(sp  - winSpark);
    outSnare = (uint16_t)(sn  - winSnare);
    outVeto  = (uint16_t)(vt  - winVeto);
    outRefr  = (uint16_t)(rf  - winRefr);
    outFps = frames;
    outMaxBass = maxBass; outMaxMid = maxMid; outMaxHigh = maxHigh; outMaxLevel = maxLevel;
    outMaxAge = maxAge;
    uint16_t nGap = gGapFill;                     // snapshot: the WiFi task keeps writing while we sort
    if (nGap) {
      for (uint16_t i = 0; i < nGap; i++) gapSort[i] = gGapRing[i];
      std::sort(gapSort, gapSort + nGap);
      outP50    = gapSort[(nGap * 50) / 100];
      outP90    = gapSort[(nGap * 90) / 100];
      outP99    = gapSort[(nGap * 99) / 100 < nGap ? (nGap * 99) / 100 : nGap - 1];
      outGapMax = gapSort[nGap - 1];
      outLost = lostFromGaps(gapSort, nGap);
    }
    winStart = now; winPkt = pkt; winBeat = bt; winSpark = sp; winSnare = sn; winVeto = vt; winRefr = rf;
    frames = 0; maxBass = maxMid = maxHigh = maxLevel = 0; maxAge = 0;
  }

  // Rolling bass trace, one column per frame (~2-4s of history at audio-mode fps). This is the whole
  // point of the screen: instantaneous bass spikes and falls; a peak-held bass plateaus.
  const int TW = 120, TX = 60, TBASE = 214, TH = 52;   // stays inside the round bezel at this y
  static uint8_t trace[TW] = {0}, highTrace[TW] = {0}, thrTrace[TW] = {0},
                 midTrace[TW] = {0}, midThrTrace[TW] = {0}, tickTrace[TW] = {0};
  static uint8_t head = 0;
  trace[head]     = p.bass;
  highTrace[head] = p.high;   // the band a snare's CRACK reaches -- before the mid band existed, its body reached nothing
  midTrace[head]  = p.mid;
  // Each detector's LIVE bar: floor, frame-to-frame rise, and the baseline's proportional margin,
  // whichever binds hardest. bar == v - score(v) by definition (audio.cpp), so this is exact by
  // construction -- no re-derivation to drift out of sync with the detector. A level trace without
  // its bar cannot show WHY a hit didn't fire; a static floor line could never show a rumble
  // lifting the bar out of a kick's reach. Computed on the RX side (gBassBar/gMidBar), not here:
  // by the time this screen snapshots gAudioRx, rxBeat/rxMid.prev already equals THIS packet's
  // value, so a score() call at render time would be scoring the sample against itself.
  thrTrace[head]    = gBassBar;
  midThrTrace[head] = gMidBar;
  { // Ground truth per column: did a beat/spark/snare actually fire since the last frame?
    static uint32_t seenBeat = 0, seenSpark = 0, seenSnare = 0;
    uint32_t b = gBeatCount, s = gSparkCount, n = gSnareCount;
    tickTrace[head] = (uint8_t)((b != seenBeat ? 1 : 0) | (s != seenSpark ? 2 : 0) | (n != seenSnare ? 4 : 0));
    seenBeat = b; seenSpark = s; seenSnare = n;
  }
  head = (uint8_t)((head + 1) % TW);

  canvas->setTextSize(1);
  int y = 34; const int dy = 12; char buf[48];
  uint16_t hi   = gfx->color565(255, 255, 255), lo = gfx->color565(120, 200, 255),
           warn = gfx->color565(255, 120, 120), ok = gfx->color565(120, 255, 160);
  // Header is centered (its width is fixed). Everything else is a label/value pair on a FIXED column:
  // label right-aligned to COLX, value left-aligned from COLX+6. Centering a whole "label value" string
  // re-centers the line every time a value gains or loses a digit -- age 9->10ms twitched the label left
  // by 3px, and the numbers you're trying to read are exactly the ones that change width. Anchoring the
  // split means digits grow rightward into empty space and nothing else moves.
  // Size-1 glyphs are 6px wide, so half-width = 3*len and a right-aligned label starts at COLX - 6*len.
  // Two columns now: the KV block is pulled left (COLX 116 -> 60) to free the right half for the age
  // quantiles. PCTX is where that second column starts. Both columns must stay inside the round bezel --
  // at row y the usable half-width is sqrt(120^2 - (120-y)^2), which is tightest at the top rows.
  const int COLX = 60, PCTX = 146;
  #define DBG_LINE(col, ...) do { snprintf(buf, sizeof buf, __VA_ARGS__); \
    canvas->setTextColor(col); canvas->setCursor(120 - 3 * (int)strlen(buf), y); \
    canvas->print(buf); y += dy; } while (0)
  #define DBG_KV(col, label, ...) do { \
    canvas->setTextColor(col); \
    canvas->setCursor(COLX - 6 * (int)strlen(label), y); canvas->print(label); \
    snprintf(buf, sizeof buf, __VA_ARGS__); \
    canvas->setCursor(COLX + 6, y); canvas->print(buf); y += dy; } while (0)
  #define DBG_PCT(col, row, label, val) do { \
    canvas->setTextColor(col); \
    snprintf(buf, sizeof buf, "%-4s%4u", label, (unsigned)(val)); \
    canvas->setCursor(PCTX, 34 + (row) * dy); canvas->print(buf); } while (0)
  DBG_LINE(hi, "AUDIO DEBUG  id%u", (unsigned)AUDIO_DEBUG_ID);   // from the constant, never a literal: the ids shift when an animation is added, and a debug screen that misreports its own id sends you to the wrong mode
  DBG_KV(live ? ok : warn, "state", "%s", live ? "LIVE" : "STALE");
  // Every numeric is width-padded, not just left-anchored: a bare %u still slides whatever follows it
  // ("9 ms" -> "12 ms" walks the unit one glyph right).
  // `age` is published on the 1s window, not per-frame. It was the only field on this screen not on the
  // window, so it redrew at frame rate (~35Hz) and no alignment could make it readable -- the digits
  // really were changing 35 times a second.
  DBG_KV(lo, "pkt/s", "%3u", outPkt);
  DBG_KV(lo, "age",   "%4lu ms", (unsigned long)outMaxAge);
  DBG_KV(lo, "bass",  "%3u ^%3u s%3u", p.bass,  outMaxBass,  aB);
  DBG_KV(lo, "mid",   "%3u ^%3u", p.mid,  outMaxMid);      // no `s`: mid gets no AGC (see agcStretch)
  DBG_KV(lo, "high",  "%3u ^%3u", p.high, outMaxHigh);
  DBG_KV(lo, "lvl",   "%3u ^%3u s%3u", p.level, outMaxLevel, aL);
  DBG_KV(hi, "trig",  "b%2u s%2u", outBeats, outSpark);
  // The snare row IS the tuning instrument. n=0 with v>0: the ratio test is eating them (the kick
  // dominates, or MID_BIN_LO is low enough to be catching its fundamental). n=0 with v=0 and r=0:
  // the mid never cleared its bar -- drop SNARE_MID_FLOOR / SNARE_MID_RISE. r>0: a real snare the
  // refractory window ate -- it cleared its bar AND would have out-scored the kick (same test as n),
  // so the window is the only thing that stopped it -- lower SNARE_REFRACTORY_MS. Target: n ~= 2/s
  // on a 120BPM backbeat, and no snare tick under a bare kick.
  // %2u is a MINIMUM width, not a max -- clamp to 99 or a 3-digit window blows the fixed column the
  // trace/KV split above exists to hold.
  DBG_KV(hi, "snare", "n%2u v%2u r%2u", (unsigned)(outSnare > 99 ? 99 : outSnare),
         (unsigned)(outVeto > 99 ? 99 : outVeto), (unsigned)(outRefr > 99 ? 99 : outRefr));
  DBG_KV(lo, "fps",   "%3u", outFps);
  // Gap quantiles over the last 256 PACKETS (~1.5s at 176/s). p50 == the send interval on a clean link;
  // the tail is packets that never arrived. This is the radio's health, not the render loop's.
  canvas->setTextColor(hi); canvas->setCursor(PCTX, 34 + dy); canvas->print("gap ms");
  DBG_PCT(lo, 2, "p50", outP50);
  DBG_PCT(lo, 3, "p90", outP90);
  DBG_PCT(lo, 4, "p99", outP99);
  DBG_PCT(lo, 5, "max", outGapMax);
  DBG_PCT(lo, 6, "lost", outLost);                                            // lost pkt/s inferred from the gap tail
  uint32_t rej = gRejCount;                                                   // snapshot: three reads of a volatile below
  DBG_PCT(rej ? warn : lo, 7, "rej", (unsigned)(rej > 9999 ? 9999 : rej));
  DBG_PCT(staleCount ? warn : lo, 8, "stl", (unsigned)(staleCount > 9999 ? 9999 : staleCount));  // live->stale transitions while this screen has been up
  #undef DBG_LINE
  #undef DBG_KV
  #undef DBG_PCT

  // Bars flip orange when they clear the LIVE threshold (not the static floor); the dotted line IS
  // that threshold, so "the kick didn't count" is visible as a bar stopping under the dots. gBassBar/
  // gMidBar ARE BeatDetector::score()'s bar (floor, frame-to-frame rise via prev, and the baseline
  // margin, whichever binds hardest -- see onEspNowRecv), so clearing the dots is the WHOLE rising
  // test, not just a level check. It still isn't full ground truth: the bar says nothing about the
  // refractory window, so a bar-clearing transient can lose to a still-open window from the last hit
  // (see the r counter). The ticks below the trace remain the actual fire/no-fire signal.
  uint16_t guide  = gfx->color565(90, 90, 110);
  uint16_t thrCol = gfx->color565(160, 160, 190);
  uint16_t hiCol  = gfx->color565(200, 100, 255);   // high history + spark ticks
  uint16_t midCol = gfx->color565(120, 255, 160);   // mid history + snare ticks
  uint16_t midThrCol = gfx->color565(70, 150, 100); // ...and mid's live bar, dimmer than its trace
  for (int i = 0; i < TW; i++) {                    // oldest column at the left
    int idx = (head + i) % TW;
    uint8_t v = trace[idx];
    int h = v * TH / 255;
    if (h) canvas->drawLine(TX + i, TBASE, TX + i, TBASE - h,
                            v >= thrTrace[idx] ? gfx->color565(255, 170, 60) : gfx->color565(60, 120, 200));
    canvas->drawPixel(TX + i, TBASE - thrTrace[idx]    * TH / 255, thrCol);      // live beat threshold
    canvas->drawPixel(TX + i, TBASE - highTrace[idx]   * TH / 255, hiCol);       // high-band history
    canvas->drawPixel(TX + i, TBASE - midTrace[idx]    * TH / 255, midCol);      // mid-band history
    canvas->drawPixel(TX + i, TBASE - midThrTrace[idx] * TH / 255, midThrCol);   // ...and the bar it must clear
    if (tickTrace[idx] & 1) canvas->drawFastVLine(TX + i, TBASE + 2,  3, hi);     // beat fired (white), below the baseline
    if (tickTrace[idx] & 2) canvas->drawFastVLine(TX + i, TBASE + 6,  3, hiCol);  // spark fired, below that
    // Snare ticks, the only row low enough for the bezel to bite: at y=226 the chord is x 64..176, so
    // it clips ~4 columns at the old end AND ~3 at the NEW end -- i.e. the newest tick can be missing
    // exactly while you watch a hit land. Accepted: the n/v/r counters are the ground truth, these
    // ticks are for reading rhythm against the trace. Beat/spark ticks sit higher and clear the chord.
    if (tickTrace[idx] & 4) canvas->drawFastVLine(TX + i, TBASE + 10, 3, midCol);
  }
  canvas->drawFastHLine(TX, TBASE + 1, TW, guide);
}

// id 44 --- Spectrogram waterfall: all 64 bins over time. bass aggregates bins 0-7, mid 18-31 (the
// snare-body band this screen was built to find -- moved up off the kick body after the tuning
// sweep, see MID_BIN_LO in audio.h), high 46-63. Bins 8-17 and 32-45 feed no detector by choice --
// kick spill and vocals/guitars, not a percussive transient band. Linear geometry (debug readability).
// Reach with flash.py --anim 44; not in the button cycle.
static inline uint16_t wfColor(uint8_t v) {   // heat ramp black->blue->orange->white; the palette seam for the pivot
  if (v < 85)  return gfx->color565(0, 0, (uint8_t)(v * 3));
  if (v < 170) { uint8_t t = (uint8_t)((v - 85) * 3); return gfx->color565(t, t / 2, (uint8_t)(255 - t)); }
  uint8_t t = (uint8_t)((v - 170) * 3);
  return gfx->color565(255, (uint8_t)(128 + t / 2), t);
}

void renderWaterfall(uint32_t now) {
  // History ring: 240 columns x 64 bins post-curve = 15KB, static -> internal RAM by construction.
  // Full redraw through the canvas API every frame (rotation-safe, simple). If [prof] shows this
  // render-bound, the escalation is memmove-scroll on the internal framebuffer -- measured, not assumed.
  static uint8_t wfHist[240][NUM_FREQS] = {};
  static uint8_t wfHead = 0;
  static uint8_t wfTick[240] = {};   // bit0 beat, bit1 spark, bit2 snare -- fired between this column's drain and the last

  if (gWfEnter) {   // fresh screen on entry -- the pend column may hold another mode's minutes-old peaks
    gWfEnter = false;
    memset(wfHist, 0, sizeof wfHist);
    memset(wfTick, 0, sizeof wfTick);
    for (int i = 0; i < NUM_FREQS; i++) gWfPend[i] = 0;   // plain loop over volatile -- loop-task-only, see gWfEnter comment
  }

  drainWfColumn(wfHist[wfHead]);   // copy-then-zero race notes live on the helper
  { static uint32_t seenBeat = 0, seenSpark = 0, seenSnare = 0;
    uint32_t bt = gBeatCount, sp = gSparkCount, sn = gSnareCount;
    wfTick[wfHead] = (uint8_t)((bt != seenBeat ? 1 : 0) | (sp != seenSpark ? 2 : 0) | (sn != seenSnare ? 4 : 0));
    seenBeat = bt; seenSpark = sp; seenSnare = sn; }
  wfHead = (uint8_t)((wfHead + 1) % 240);

  // 2px/bin = 128px tall, centered: at +/-64px from center the bezel chord is ~200px so every bin
  // stays visible across most of the history (3px/bin would clip the extremes to a ~144px window).
  // Bass at the bottom; time scrolls left, newest column at x=239, ~4s of history at 58fps.
  const int YTOP = 56;
  for (int c = 0; c < 240; c++) {
    const uint8_t* col = (const uint8_t*)wfHist[(uint8_t)((wfHead + c) % 240)];
    for (int b = 0; b < NUM_FREQS; b++) {
      uint8_t v = col[b];
      if (v) canvas->drawFastVLine(c, YTOP + (63 - b) * 2, 2, wfColor(v));   // skip black: loop() pre-cleared the canvas
    }
  }
  // Band edges: what bass (bins 0-7), mid (MID_BIN_LO..HI) and high (46-63) aggregate. The two
  // guides bracket the mid band; bass is not separately marked. The gap below the mid band (bins
  // 8..MID_BIN_LO-1) is kick spill and the gap above it (MID_BIN_HI..45) is vocals/guitars -- both
  // deliberately unaggregated.
  uint16_t guide = gfx->color565(90, 90, 110);
  const int Y_HIGH_BOT = YTOP + (63 - 46) * 2 + 2;                    // just below the high band
  const int Y_MID_TOP  = YTOP + (63 - (MID_BIN_HI - 1)) * 2 - 1;      // just above the mid band
  const int Y_MID_BOT  = YTOP + (63 - (MID_BIN_LO - 1)) * 2 - 1;      // just below the mid band (== bin MID_BIN_LO-1)
  // Derived from MID_BIN_LO/HI on purpose: this screen is where the hardware-tuning step tells the
  // operator to decide whether to move them, and a hardcoded guide would silently point at the old band.
  for (int x = 0; x < 240; x += 4) {
    canvas->drawPixel(x, Y_HIGH_BOT, guide);
    canvas->drawPixel(x, Y_MID_TOP,  guide);
    canvas->drawPixel(x, Y_MID_BOT,  guide);
  }
  // Detector ground truth, scrolling with the history: beats and snares below the plot, sparks above.
  // Same bezel-clipping hazard id 43's snare row documents (see d0a3b89): at y=190 (snare row) the
  // round bezel's chord is x 24..216, so the newest ~23 columns never draw a snare tick -- a snare
  // landing while you're watching it will not show. The beat row above it (y=186) already clips
  // ~20 columns for the same reason. Not moving these rows: accepted, same as id 43.
  uint16_t beatCol = gfx->color565(255, 255, 255), sparkCol = gfx->color565(200, 100, 255),
           snareCol = gfx->color565(120, 255, 160);
  for (int c = 0; c < 240; c++) {
    uint8_t tk = wfTick[(uint8_t)((wfHead + c) % 240)];
    if (tk & 1) canvas->drawFastVLine(c, YTOP + 128 + 2, 3, beatCol);
    if (tk & 4) canvas->drawFastVLine(c, YTOP + 128 + 6, 3, snareCol);
    if (tk & 2) canvas->drawFastVLine(c, YTOP - 5,       3, sparkCol);
  }
  // pkt/s + link state in the top arc, published on a 1s window (frame-rate digits are unreadable --
  // id 43 learned this the hard way). Bezel at y=20: chord is x 54..186; ~10 chars centered fits.
  static uint32_t winStart = 0, winPkt = 0;
  static uint16_t outPkt = 0;
  if ((uint32_t)(now - winStart) >= 1000) {
    uint32_t pkt = gAudioRxCount;
    outPkt = (uint16_t)(pkt - winPkt);
    winPkt = pkt; winStart = now;
  }
  bool live = !audioStale(gAudioRxMillis, now);
  char buf[24];
  snprintf(buf, sizeof buf, "%3u/s %s", outPkt, live ? "LIVE" : "STALE");
  canvas->setTextSize(1);
  canvas->setTextColor(live ? gfx->color565(120, 255, 160) : gfx->color565(255, 120, 120));
  canvas->setCursor(120 - 3 * (int)strlen(buf), 20);
  canvas->print(buf);
}

// id 36 --- Echo: the waterfall's data path reborn as a radial ripple. freq = angle (mirrored
// about the vertical axis, bass at the bottom), time = radius: each frame's peak-hold column is
// born at the pupil, drifts outward 1px/frame, and dies at the bezel (~2s of history at 58fps).
// Blits 2x2 blocks straight into the internal framebuffer through a polar LUT -- the canvas-API
// version of this density (~14k calls/frame) is exactly what made the linear waterfall cost
// 11.7ms/frame. Rotation needs no handling here: setPanelRotation is MADCTL on the panel (the
// whole image rotates at scan-out), so direct fb writes rotate exactly like canvas draws.
static uint8_t   echoHist[120][NUM_FREQS];   // 7.7KB static ring of post-curve columns; [echoHead] = newest
static uint8_t   echoHead = 0;
static uint16_t* echoLut  = nullptr;         // 120x120 blocks -> (ring<<6)|bin, 0xFFFF = outside the circle.
                                             // ~29KB heap on first entry, kept forever -- boards that
                                             // never enter an audio mode never pay for it.

void renderEcho(uint32_t now) {
  if (!echoLut) {                                    // first entry: build the polar map. One-time
    echoLut = (uint16_t*)malloc(120u * 120u * 2u);   // tens-of-ms soft-float cost on the C3, hidden
    if (!echoLut) { static bool warned = false; if (!warned) { warned = true; Serial.println("[echo] WARN: 29KB LUT alloc failed -- pupil-only"); } }
    if (echoLut) {                                   // in mode entry. malloc = internal RAM (never ps_malloc).
      uint8_t binFromAngle[256];
      buildBinFromAngle(binFromAngle);
      uint16_t* p = echoLut;
      for (int by = 0; by < 120; by++)
        for (int bx = 0; bx < 120; bx++) {
          float dx = (float)(bx * 2 - 119), dy = (float)(by * 2 - 119);   // block center vs screen center
          float r = sqrtf(dx * dx + dy * dy);
          if (r >= 119.0f) { *p++ = 0xFFFF; continue; }
          uint8_t a = (uint8_t)lroundf(atan2f(dy, dx) * (128.0f / (float)M_PI));  // byte-angle, fastSin convention
          *p++ = (uint16_t)(((uint16_t)r << 6) | binFromAngle[a]);
        }
    }
  }
  if (gEchoEnter) {                                  // fresh screen, not a replay of the last visit
    gEchoEnter = false;
    memset(echoHist, 0, sizeof echoHist);
    echoHead = 0;
    for (int i = 0; i < NUM_FREQS; i++) gWfPend[i] = 0;   // another mode's minutes-old peaks (see gWfEnter)
  }

  bool live = !audioStale(gAudioRxMillis, now);
  if (live) lastInteractionTime = now;               // streaming audio counts as activity (see renderBloom)
  // Reads hue_shift (and, SB mode, the chromagram) from the shared struct without a full snapshot:
  // a torn read is one weird target for one frame -- the slew cap / NoteHue EMA bound it.
  int hueOff = gSbActive
      ? gSbHueSlew.update(live ? sbHueTarget(gAudioRx) : (float)((now / 40) & 255), now)
      : gHueSlew.update(live ? gAudioRx.hue_shift * 40.0f : (float)((now / 40) & 255), now);

  echoHead = (uint8_t)((echoHead + 1) % 120);
  if (live) drainWfColumn(echoHist[echoHead]);
  else                                               // idle: feed the demo spectrum into the ring
    for (int i = 0; i < NUM_FREQS; i++)
      echoHist[echoHead][i] = audioBin(gDemoSnap.spectrogram[i]);

  uint16_t base[NUM_FREQS];                          // per-bin palette base: hue sweeps with angle,
  for (int i = 0; i < NUM_FREQS; i++)                // brightness carries the energy (house audio idiom)
    base[i] = pColor(6, hueOff + hspread(i * 3));

  if (echoLut) {                                     // alloc failure -> pupil-only, but alive
    uint16_t* fb = canvas->getFramebuffer();
    const uint16_t* lut = echoLut;
    for (int by = 0; by < 120; by++) {
      uint16_t* row0 = fb + (by * 2) * 240;
      for (int bx = 0; bx < 120; bx++) {
        uint16_t e = *lut++;
        if (e == 0xFFFF) continue;
        int idx = echoHead - (e >> 6); if (idx < 0) idx += 120;   // ring r = the column drained r frames ago
        uint8_t v = echoHist[idx][e & 63];
        if (!v) continue;                            // pre-clear already made it black
        uint16_t c = dim565(base[e & 63], v, 255);
        uint16_t* px = row0 + bx * 2;
        px[0] = c; px[1] = c; px[240] = c; px[241] = c;
      }
    }
  }

  uint8_t bassMax = 0;                               // pupil: bass-pulsed core over the polar singularity,
  for (int i = 0; i < 8; i++)                        // where 2x2 blocks can't resolve angle anyway
    if (echoHist[echoHead][i] > bassMax) bassMax = echoHist[echoHead][i];
  int cr = 6 + (int)bassMax * 6 / 255;               // r 6..12
  for (int rr = cr; rr > 0; rr -= 3)
    canvas->fillCircle(120, 120, rr, pColor(4, hueOff + hspread((cr - rr) * 4)));
}
#endif  // OCELLUS_AUDIO

// Per-mode frame profile: one Serial line/s saying where the frame time actually went. Audio modes
// run uncapped, so this is the ground truth for the only question that matters before optimizing
// anything --- is a frame render-bound (Bloom's ~26 nested fillCircles) or flush-bound (115KB canvas
// pushed over SPI)? It has to be a Serial line and not a field on the audio-debug screen, because
// that screen can only ever profile ITSELF; the mode under investigation is a different one.
// Reported in us, avg/max over the window, so there's no float formatting and no rounding to argue with.
static void profTick(uint8_t id, uint32_t renderUs, uint32_t flushUs) {
  static uint32_t winStart = 0, rSum = 0, fSum = 0, rMax = 0, fMax = 0, n = 0;
  rSum += renderUs; fSum += flushUs; n++;
  if (renderUs > rMax) rMax = renderUs;
  if (flushUs  > fMax) fMax = flushUs;
  uint32_t now = millis();
  if ((uint32_t)(now - winStart) < 1000) return;
  // pkt/s is here (not just on the audio-debug screen) because the screen can only report its own mode:
  // this is how you see the radio is actually feeding the mode you're looking at. 0 in non-audio modes --- the
  // radio is gated off there by design (battery).
  static uint32_t winPkt = 0;
  uint32_t pkt = gAudioRxCount;   // audio-off builds read the parked 0 (see the ensureRadio stub block)
  if (n) Serial.printf("[prof] id=%u fps=%lu render %lu/%lu us  flush %lu/%lu us  (avg/max)  pkt/s %lu  heap %lu\n",
                       id, (unsigned long)n, (unsigned long)(rSum / n), (unsigned long)rMax,
                       (unsigned long)(fSum / n), (unsigned long)fMax,
                       (unsigned long)(pkt - winPkt), (unsigned long)ESP.getFreeHeap());
  winStart = now; winPkt = pkt; rSum = fSum = rMax = fMax = n = 0;
}

// The strip is drawn over modes whose framebuffer PERSISTS between frames (the no-clear list in
// loop()), so it would leave a scar with nothing to repaint it. Of that no-clear list, only
// Pipes/Slideshow/GIF (true persistence) and Boids (decays rather than hard-clearing) are
// actually exposed -- Swirl's upscale and treatcat's drawSky() both repaint every pixel every
// frame regardless, so their no-clear entry there is a perf win, not persistence.
// Re-entering the animation on hide is the obvious fix and is wrong for what IS exposed: it is
// a no-op for Boids (its `seeded` static is never reset) and destructive for treatcat
// (treatcatOnEnter clears the fortune and re-rolls the scene facing) -- moot for Swirl, which
// repaints anyway.
// Save and restore the band instead -- uniform, no per-renderer knowledge, no reset side
// effects. Rows are contiguous in a 240-wide row-major framebuffer, so it is one memcpy.
// Lives here, not by the carousel state block near the top of the file, because it needs
// SCREEN_RES and canvas, both defined later in the file than that block.
static const int CAR_BAND_Y = 100, CAR_BAND_H = 40;
static const size_t CAR_BAND_BYTES = (size_t)CAR_BAND_H * SCREEN_RES * sizeof(uint16_t);
static uint16_t* gCarBand = nullptr;
static bool gCarBandValid = false;

static inline uint16_t* carBandPtr() {
  return canvas->getFramebuffer() + (size_t)CAR_BAND_Y * SCREEN_RES;
}
static void carouselBandRestore() {
  if (gCarBand && gCarBandValid) memcpy(carBandPtr(), gCarBand, CAR_BAND_BYTES);
  gCarBandValid = false;
}
static void carouselBandFree() {
  free(gCarBand); gCarBand = nullptr; gCarBandValid = false;
}

// Draws the name strip. Saves the band first so the next frame's restore can erase it, then
// dithers a scrim and prints the centre name plus its two neighbours.
static void carouselOverlay() {
  if (!gCarBand || gCarousel.n <= 0) return;
  memcpy(gCarBand, carBandPtr(), CAR_BAND_BYTES);
  gCarBandValid = true;

  // 50% checkerboard scrim: Arduino_GFX has no alpha, and a dither reads as translucent for
  // one extra line of code. The band (middle 40 rows of a 240 circle, dy<=20 from centre) is
  // very nearly full-width -- worst case ~1.7px off-glass at each edge (sqrt(120^2-20^2) ~=
  // 118.3) -- so no per-row clip is needed; those writes just land on invisible pixels.
  uint16_t* fb = carBandPtr();
  for (int y = 0; y < CAR_BAND_H; y++)
    for (int x = (y & 1); x < SCREEN_RES; x += 2)
      fb[y * SCREEN_RES + x] = BLACK;

  // Wrap must be off: the default (Arduino_GFX.cpp, wrap=true) wraps a name that runs past the
  // right edge onto the row below at x=0 -- unconditionally garbling the right neighbour every
  // rest frame, and for a long-enough centre name landing on rows the band doesn't cover
  // (glyph rows 128..143 vs the saved 100..139), a permanent scar on Pipes/Slideshow/GIF. Set
  // explicitly, not left to the default: Name Spiral (main.cpp ~2725) sets it false for its rim
  // text and never restores it, so the default here is otherwise boot-history dependent.
  canvas->setTextWrap(false);

  float p = gCarousel.pos();
  int centre = (int)lroundf(p);
  float frac = p - (float)centre;                 // -0.5..0.5, how far the strip has slid
  for (int d = -1; d <= 1; d++) {
    // n==1: both neighbours land on the centre itself -- skip them rather than draw the same name
    // three times. n==2 is correctly left alone: both neighbours map to the SAME other item there,
    // which is genuine circular-carousel behaviour, not a bug.
    if (d != 0 && gCarousel.n == 1) continue;
    int idx = (centre + d) % gCarousel.n;
    if (idx < 0) idx += gCarousel.n;
    const char* nm = animName(gCarousel.ids[idx]);
    int ts = (d == 0) ? 2 : 1;
    canvas->setTextSize(ts);
    canvas->setTextColor(d == 0 ? WHITE : gfx->color565(110, 110, 110));
    int w = ((int)strlen(nm) * 6 - 1) * ts;        // ink width: advance minus the trailing 1px gap
    int cx = SCREEN_RES / 2 + (int)lroundf((d - frac) * Carousel::ITEM_W) - w / 2;
    int cy = CAR_BAND_Y + CAR_BAND_H / 2 - 4 * ts;
    if (cx > -w && cx < SCREEN_RES) { canvas->setCursor(cx, cy); canvas->print(nm); }
  }
}

// Drives the model from the live touch snapshot. Drawing is separate (carouselOverlay).
static void carouselUpdate(uint32_t now) {
  if (gCarouselReq) {                      // swipe-up latch from the button task
    gCarouselReq = false;
    if (!gCarouselOpen) {
      // MALLOC_CAP_INTERNAL, not plain malloc: this board has PSRAM and the IDF prefers it for any
      // allocation over 4KB, which would put two 19.2KB memcpys per frame on the PSRAM bus. Same trap
      // that cost the canvas 23fps (see CLAUDE.md).
      if (!gCarBand) gCarBand = (uint16_t*)heap_caps_malloc(CAR_BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!gCarBand) return;               // 19.2KB unavailable: no strip, swipe left/right still works
      uint8_t list[Carousel::MAX_N];
      int n = carouselList(gConfig.favoritesMask, list, Carousel::MAX_N);
      gCarousel.open(list, n, currentAnimId);
      gCarouselOpen = true;
      gCarouselTickMs = now;
      gCarouselWasDown = false;
      gCarouselApplied = currentAnimId;   // what is really on screen, which may not be in the list
      gCarouselMoved   = false;           // opening is not a selection
    }
    gCarouselIdleMs = now;                 // a second swipe-up while open just resets the hide timer
  }
  if (!gCarouselOpen) return;

  uint32_t snap = gTouchSnap;              // ONE read: down/x/y must come from the same sample
  bool down = touchSnapDown(snap);
  if (down) {
    if (!gCarouselWasDown) gCarouselDownMs = now;   // rising edge: start of this continuous touch
    gCarousel.drag(touchSnapX(snap));
    lastInteractionTime = now;             // a long scrub must not trip the idle sleep
  } else if (gCarouselWasDown) {
    gCarousel.release();
  }
  gCarouselWasDown = down;

  float dt = (float)(now - gCarouselTickMs) / 1000.0f;
  gCarouselTickMs = now;
  if (dt > 0.25f) dt = 0.25f;              // a serial stall or an upload must not launch the strip across the list
  gCarousel.tick(dt);

  // Dwell starts when the strip stops MOVING, not at finger-up: a firm flick coasts ~3.1s
  // (carousel.h DECAY/V_SNAP), so stamping only while down closed the strip mid-coast and
  // discarded the selection entirely.
  if (down || gCarousel.moving()) gCarouselIdleMs = now;

  if (gCarousel.moving()) gCarouselMoved = true;
  uint8_t sel = gCarousel.settledId();
  // Compare against what the carousel last applied, NOT currentAnimId, which the button, the
  // multi-click jumps and the serial anim command also move -- a level test against it silently
  // reverts those for as long as the strip is up. The moved latch keeps merely OPENING the strip
  // from counting as a selection, including when the running anim is not in the list at all.
  if (sel != 0xFF && gCarouselMoved && sel != gCarouselApplied) { gCarouselApplied = sel; g_pendingAnim = sel; }

  // Both close paths below restore-then-free the band themselves rather than leaving it to the
  // loop()-side restore-before-dispatch: that call is gated on gCarouselOpen, which this function
  // is about to clear, so by the time loop() reaches the dispatch this frame it would see the
  // flag already false and skip the restore -- leaving the strip's pixels sitting on top of
  // whatever a persistent-framebuffer mode draws this frame.
  if (now - gCarouselIdleMs >= CAROUSEL_HIDE_MS) {
    carouselBandRestore();
    carouselBandFree();
    gCarouselOpen = false;
    // Auto-cycle is suspended while the strip is up (see the cycleSec gate in loop()), so elapsed
    // time kept accumulating underneath it. Without this, closing the strip after a deliberate
    // scrub could hand the cycler an already-expired window and yank the selection immediately.
    gLastCycle = now;
  }
  // Bound the DRAG, not the open. touchPoll() holds its state on an I2C read error without
  // clearing gTouchSnap, so a wedged bus mid-drag leaves the finger-down bit set forever: the
  // strip would never close, auto-cycle would stay off, and lastInteractionTime would be
  // restamped every frame, so a battery board would never idle-sleep. A legitimate browse is
  // many short touches, so it never reaches this; a stuck bit reaches it in 30s.
  if (down && now - gCarouselDownMs >= CAROUSEL_STUCK_MS) {
    carouselBandRestore();
    carouselBandFree();
    gCarouselOpen = false;
    gLastCycle = now;   // same reasoning as the hide-timeout close above
  }
}

void loop() {
  if (gPowerOffReq) powerOff();   // deferred button long-press (flag-only in the button task); never returns
  // Battery: sample every 5s (readBatteryMv is 8 ADC reads, ~free). On a state change apply the
  // brightness cap immediately and put an eye to sleep in place; splash/cutoff actions land here too.
  static uint32_t nextBattMs = 0;
  if (millis() >= nextBattMs) {
    nextBattMs = millis() + 5000;
    BattState prevB = gBatt.state();
    gBatt.feed(gBatSimMv ? gBatSimMv : readBatteryMv(), millis());
    if (gBatt.state() != prevB) {
      backlightSet(effectiveBrightness());
      if (gBatt.state() == BATT_LOW && currentAnimId < EYE_COUNT) eyeMood = MOOD_DROWSY;
    }
    // USB power = desk toy: refresh the idle timer so sleepMin never fires while plugged in, and
    // unplugging starts a FRESH sleepMin window (a stale timer would sleep the toy the instant
    // it's unplugged after hours on the desk). Config-gated, default ON.
    if (gConfig.stayAwakeUsb && gBatt.usbPowered()) lastInteractionTime = millis();
    if (gBatt.state() == BATT_CUTOFF) {
      // (the state-change branch above already lifted the LOW brightness cap -- CUTOFF isn't LOW --
      // so this final splash runs full-bright: deliberate, it's the last thing the user sees)
      batterySplash();
      powerOff();                        // never returns
    }
    // Slideshow renders no frame between transitions (the slide LIVES in the framebuffer), so a
    // splash would leave a 1-slide show on flushed black forever; dirty forces a re-list + redraw.
    if (gBatt.splashDue(millis())) {
      // The splash repaints everything, so the saved band is stale -- drop the strip rather
      // than paint 40 rows of stale pixels back over the splash.
      if (gCarouselOpen) { carouselBandFree(); gCarouselOpen = false; }
      batterySplash(); gSlidesDirty = true;
    }
  }

  uint32_t now = millis();
  pollConfigSerial();
#if defined(BOARD_WAVESHARE_128)
  carouselUpdate(now);
#endif
  if (gGifUploading) {                        // same deal for a GIF upload (see the slide case below)
    if (millis() - gGifRxMs > 3000) {         // host vanished mid-upload -> drop the partial tmp
      if (gGifUp.active) gGifStore.abortTmp();
      gGifUp.active = false; gGifUploading = false;
    }
    return;
  }
  if (gSlideUploading) {                      // dedicate the loop to draining serial during an upload
    if (millis() - gSlideRxMs > 3000) {       // host vanished mid-upload -> drop the partial tmp
      if (gSlideUp.active) gSlideStore.abortTmp(gSlideUp.index);
      gSlideUp.active = false; gSlideUploading = false;
    }
    return;                                    // skip rendering this frame -- deliberate trade-off: also
    // skips drainTap() in audio modes for the upload's duration, but uploads are normally done from
    // config, not mid-visualizer, and tap telemetry catches up once slide_end ends the upload.
  }
#if OCELLUS_AUDIO
  drainTap();   // every iteration, including frame-gap early returns -- the ring holds ~366ms, don't test it
#endif
  // Config-save render hold (mirror of the slide-upload drain above): a multi-part config.html save
  // sends long `set` lines (qrBits ~190B, custom palettes ~360B) that reliably lose bytes to the
  // flush window and the frame-gap naps (docs/config-save-rx-drop-investigation.md). For 250ms after
  // any `set` line, keep the loop quiet -- no render, no flush, no nap -- so the save's next parts
  // arrive into an idle RX path; each part re-arms the window, so a whole save runs drop-free after
  // its first line lands. The panel self-refreshes from GRAM, so the image simply holds. Placed
  // after drainTap: a save mid-audio-mode must not let the tap ring (~366ms) overrun.
  if (gCfgSetMs && millis() - gCfgSetMs < 250) { delay(1); return; }
  if (imuPresent && isPlayableId(currentAnimId) && currentAnimId != FLUID_ID && currentAnimId != YINYANG_ID) { static uint32_t t=0; if (now-t>=200) { t=now; setPanelRotation(imuRotation()); } }   // accel auto-flip (Fluid/Yin-Yang/debug own their orientation)
  if (g_pendingAnim >= 0) {
    currentAnimId = (uint8_t)g_pendingAnim;
    g_pendingAnim = -1;              // clear BEFORE onAnimEnter: it can take tens of ms (radio bring-up)
    onAnimEnter(currentAnimId);      // and anything the button task queues during it must survive
    lastInteractionTime = millis();
    gLastCycle = millis();           // a manual pick gets its full cycleSec window before the auto-cycler moves on
  }
  // Audio modes run uncapped (frame == flush) EXCEPT a toy audio mode drawn down after 3s of
  // silence, which caps so the frame-gap light sleep can engage. Debug screens (isAudioMode but
  // not audioId) always stay uncapped.
  bool audioIdNow = currentAnimId >= AUDIO_BASE && currentAnimId < AUDIO_BASE + AUDIO_COUNT;
  bool drawn = false;
#if OCELLUS_AUDIO
  drawn = audioIdNow && audioStale(gAudioRxMillis, now, DRAWDOWN_SILENCE_MS);
#endif
  bool uncapped = isAudioMode(currentAnimId) && !drawn;
  uint32_t frameDelay = uncapped ? 0 : 1000UL / (gConfig.maxFps ? gConfig.maxFps : 1);
  if (now < nextFrameTime) {
    // Actually SLEEP the gap instead of spinning it. The CPU burns ~40mA doing nothing at any clock
    // (measured: 80MHz pinned flat-out drew the same 50mA as 240MHz idle-spinning 42% of the frame),
    // so this dead time -- ~14ms of every 33ms frame now that rendering got cheap -- is the single
    // largest non-backlight load on the board. Frame content is unaffected: the panel self-refreshes
    // from its own GRAM, so the image holds while we're out.
    // Radio-gated: ESP-NOW would drop packets across a sleep, and audio modes run uncapped anyway
    // (frameDelay 0), so this can only ever fire in the non-audio modes -- which is the 362-days-a-year case.
    uint32_t gap = nextFrameTime - now;
    if (kLightSleepOk && !radioOn && gap > 3) {   // <=3ms isn't worth the wake latency
      esp_sleep_enable_timer_wakeup((uint64_t)(gap - 1) * 1000ULL);   // -1ms: wake early, never late
      esp_light_sleep_start();                      // millis()/esp_timer stay correct across this
#if defined(BOARD_WAVESHARE_128)
      // GPIO wake == the touch INT pulsed mid-nap (it's the only GPIO light-sleep source armed).
      // The falling-edge ISR can't fire during sleep, so hand the latch the pulse it missed.
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) touchNoteInt();
#endif
    } else {
      delay(1);
    }
    return;
  }
  nextFrameTime = now + frameDelay;   // 0 => uncapped (flush-bound) for audio modes
  uint32_t sleepMs = (uint32_t)gConfig.sleepMin * 60000UL;
  // millis() (fresh, monotonic >= lastInteractionTime), NOT the loop-top `now` snapshot: a switch applied
  // this iteration sets lastInteractionTime to a later millis(), so `now - lastInteractionTime` underflowed
  // to ~4.29e9 >= sleepMs and deep-slept mid-switch (looked like an intermittent crash). sleepMin==0 => never sleep.
  // Debug screens used to be exempt here (>= DEBUG_ID never sleeps) back when reaching them required a
  // serial cable. The 3-click gesture (see onAnimEnter) made them button-reachable, so that exemption
  // could park a battery board in a never-sleeps / radio-on / uncapped-fps state forever. Idle
  // timeout now applies uniformly; set sleepMin=0 to deliberately pin a debug screen up on a bench.
  if (gConfig.sleepMin && millis() - lastInteractionTime >= sleepMs) powerOff();
  // Auto-cycle favorites (spec 2026-07-16): advance on a timer WITHOUT counting as interaction --
  // on battery the idle sleep timer still wins mid-cycle (stayAwakeUsb covers the plugged-in case).
  // Direct apply, not g_pendingAnim: that path stamps lastInteractionTime, and this is already
  // loop() context -- the same context g_pendingAnim defers to. Debug screens (non-playable ids)
  // are exempt so the cycler can't yank a bench session.
  uint32_t cycMs = (uint32_t)gConfig.cycleSec * 1000UL;
  if (gConfig.cycleSec && isPlayableId(currentAnimId) && !gCarouselOpen) {
    // Fresh millis(), NOT the loop-top `now` snapshot -- same underflow trap as the sleep check
    // above: a manual pick applied this iteration sets gLastCycle to a LATER millis(), and
    // `now - gLastCycle` would wrap huge and fire the cycler in the same frame, clobbering the pick.
    uint32_t cnow = millis();
    if (cnow - gLastCycle >= cycMs) {
      gLastCycle = cnow;
      uint8_t nxt = nextFavorite(gConfig.cycleMask ? gConfig.cycleMask : gConfig.favoritesMask, currentAnimId);
      if (nxt != currentAnimId) { currentAnimId = nxt; onAnimEnter(nxt); }   // single-bit mask -> no-op, skip the re-enter reset
    }
  }
  uint32_t rotMs = (uint32_t)gConfig.paletteRotateSec * 1000UL;
  if (gConfig.paletteRotateSec && gRotList.size() > 1 && now - gLastPaletteRotate >= rotMs) {
    gRotIndex = (gRotIndex + 1) % gRotList.size();
    startPaletteFade();          // crossfade to the next palette instead of a hard swap
    gLastPaletteRotate = now;
  }
  updatePaletteFade(now);        // blend one frame of any in-flight crossfade
  uint8_t id = currentAnimId;  // snapshot: button task can mutate mid-loop
  bool audioId = id >= AUDIO_BASE && id < AUDIO_BASE + AUDIO_COUNT;  // audio 38..41 only
#if OCELLUS_AUDIO
  if (audioId) {
    drawdownStep(gDrawdown, audioStale(gAudioRxMillis, now, DRAWDOWN_SILENCE_MS), now);
    ensureRadio(gDrawdown.wantRadio);
    if (audioStale(gAudioRxMillis, now))       // 500ms: same signal the renderers' `live` uses
      synthAudio(gDemoSnap, gSynthState, gDemoBeat, gDemoSnare, gDemoSpark, now);
  }
#endif
  gSbActive = gConfig.sbPalette && audioId;
  gAudioPalSlow = audioId && !gSbActive;     // TODO #20: slow the legacy time-walk 8x in audio modes
#if OCELLUS_AUDIO
  if (gSbActive) ensureSbWheel();            // debug 37/38 keep their own ramps
#endif
  uint32_t tRender = micros();
#if defined(BOARD_WAVESHARE_128)
  if (gCarouselOpen) carouselBandRestore();   // undo last frame's strip BEFORE the renderer runs
#endif
  if (id == PIPES_ID || id == SLIDESHOW_ID || id == BOIDS_ID || id == SWIRL_ID || id == TREATCAT_ID ||
      id == GIF_ID || (id >= ATLAS_BASE && id < ANIM_COUNT)) {
    // no clear: pipes accumulate; slideshow keeps its blitted slide resident (loaded on change
    // only); boids fades its own trail (fadeFrame) instead of hard-clearing; swirl's upscale
    // writes every pixel; the GIF player needs frame persistence for disposal modes; the atlas
    // effects self-cover (blitUp writes every pixel; the draw effects fillScreen their own bg)
  } else {
    canvas->fillScreen(BLACK);
  }

  if (id < EYE_COUNT)                     renderEye(id, now);
  else if (id == SLIDESHOW_ID)            renderSlideshow(now);
  else if (id < EYE_COUNT + EFFECT_COUNT) renderEffect(id - EYE_COUNT, now);
  else if (id == SWIRL_ID)                renderSwirl(now);         // id 45: effect above the debug block
  else if (id == TREATCAT_ID)             renderTreatcat(now);      // id 46: interactive treat cat
  else if (id == GREETZ_ID)               renderGreetz(now);        // id 47: greetz scroller
  else if (id == GIF_ID)                  renderGif(now);           // id 48: animated GIFs off LittleFS
  else if (id >= ATLAS_BASE && id < ANIM_COUNT) renderAtlas(id - ATLAS_BASE, now);   // ids 49..55: ported lab effects (before the AUDIO_BASE catch-all)
  else if (id == DEBUG_ID)                renderSensorDebug(now);   // id 42: dev sensor screen
#if OCELLUS_AUDIO
  else if (id == AUDIO_DEBUG_ID)          renderAudioDebug(now);    // id 43: dev ESP-NOW/audio telemetry
  else if (id == WATERFALL_ID)            renderWaterfall(now);   // id 44: 64-bin spectrogram waterfall
  else if (id >= AUDIO_BASE) switch (id - AUDIO_BASE) {   // audio modes, 0-based; reserved holes 35..37 fall through to black
    case 0:  renderBloom(now);          break;      // id 38
    case 1:  renderRadialSpectrum(now); break;      // id 39
    case 3:  renderEcho(now);           break;      // id 41
    default: renderReactiveIris(now);   break;      // id 40
  }
#endif
#if defined(BOARD_WAVESHARE_128)
  if (gCarouselOpen) carouselOverlay();
#endif
  uint32_t tFlush = micros();
  canvas->flush();
  profTick(id, tFlush - tRender, micros() - tFlush);   // clear+render vs push-to-panel
}
