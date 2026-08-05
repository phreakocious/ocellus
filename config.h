#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Palette {
  std::string name;
  std::vector<uint16_t> colors;   // RGB565
};

// Decode caps, enforced in configFromJson. The whole config persists as ONE string in NVS, and
// nvs_set_str hard-fails at 4000 B (old value kept, echo-verify blind to it) -- so a maximal legal
// config must serialize well under that. test_config builds the maximal config and asserts
// configToJson stays under 3900 B; adding a field or raising a cap must keep that test green.
constexpr size_t CFG_NAME_MAX       = 24;   // display copy; matrix rain has 24 columns to weave it into
constexpr size_t CFG_MODE_STR_MAX   = 8;    // startup.mode / splashStyle: known values are <= 6 chars
constexpr size_t CFG_QR_TEXT_MAX    = 560;  // QR v18 ECC-M byte capacity; longer can't encode at <= 89 modules
constexpr int    CFG_QR_SIZE_MAX    = 89;   // modules = QR version 18; past that qrBits outgrows the 2048 B serial line
constexpr size_t CFG_QR_BITS_MAX    = 1982; // ceil(89*89/8)*2 hex chars
constexpr size_t CFG_PAL_NAME_MAX   = 24;   // config.html slot label
constexpr size_t CFG_PAL_COLORS_MAX = 5;    // buildPaletteLUT gradient contract: 1..5 stops (palette.h)

struct Config {
  std::string name = "ocellus";
  uint8_t  brightness = 40;
  uint8_t  sleepMin = 15;
  uint8_t  maxFps = 30;                 // frame cap for non-audio modes; audio modes run uncapped
  bool     flip = false;                // rotate display 180 deg (upside-down enclosure mount)
  uint16_t skinColor = 0xFDB2;          // eyelid/skin tone (RGB565); default peach
  uint16_t irisColor = 0x7FFF;          // iris tint (RGB565) for standard eye themes; default cyan
  uint16_t voidColor = 0x0010;          // Void theme background (RGB565); default deep blue
  bool     eyelids = true;              // draw eyelids over the eye themes
  bool     sbPalette = false;           // audio modes follow the console's live colors (SB virtual palette)
  uint16_t slideshowSec = 5;            // seconds per slide in Slideshow mode (id 32); clamped [1,60]
  uint16_t gifSec = 6;                  // seconds per clip in the GIF player (id 48); clamped [1,60].
                                        // Separate from slideshowSec: a still wants a long dwell, a
                                        // looping clip wants roughly its own length, and the two get
                                        // tuned against different content.
  uint8_t  catVariant = 0;              // treatcat cat sprite (id 46), 0..5; per-unit
  std::string startupMode = "resume";  // resume | fixed | random
  // Used by "fixed", AND as the fallback when "resume" has nothing to resume (a unit whose NVS has
  // no stored pick yet). 47 is Greetz -- a literal because ids never move, and pulling animations.h
  // into config.h would widen the include graph of every config consumer for one number.
  uint8_t  startupId = 47;
  bool     nameMatrixRain = true;
  bool     nameBootSplash = true;
  std::string bootSplashStyle = "random";  // matrix | slide | bounce | random (only when nameBootSplash)
  uint64_t favoritesMask = 0;          // bit i => animation id i (0 = all play); 64-bit for up to 64 animations
  uint16_t cycleSec = 0;               // auto-cycle favorites every N s; 0 = off
  uint64_t cycleMask = 0;              // bit i => anim id i auto-cycles; 0 = cycle the favorites
  bool     stayAwakeUsb = true;        // USB power counts as activity -> no idle sleep while plugged in (sensed by charge-held battery voltage; battery.h)
  uint32_t palettesEnabled = 1;        // bit 0 = Rainbow preset
  uint16_t paletteRotateSec = 30;
  std::vector<Palette> customPalettes; // cap 4
  std::string qrText = "https://nullphase.net/oc/";  // QR source URL/text; persisted so config.html prefills -- device ignores it
  uint8_t     qrSize = 25;             // QR modules per side (21..CFG_QR_SIZE_MAX); 0 = unconfigured. Default = the qrBits below.
  // Default QR = qrText above, encoded by config.html's qrEncode (auto version, ECC M), so a fresh
  // device's QR mode (id 34) works out of the box until re-encoded in config.html. 25x25 = 158 hex.
  std::string qrBits = "feb2bfc144506ebd4bb74195dbaeeaec11b107faaafe005e009fbb4bee01afa0ec69330b57f5f59b0c4a2c4b4d7f3f1aa56da3cefb004e45bfac2a305eb13babbf85d7f70ee8553f045637feb8e480";
};

uint16_t hexToRgb565(const std::string& hex);   // "#RRGGBB"
std::string rgb565ToHex(uint16_t c);
bool qrModule(const std::string& hexBits, int size, int r, int c);  // module (r,c) dark? false out of range/short bits

std::string configToJson(const Config& c);
bool configFromJson(const std::string& json, Config& c);  // partial merge; false on parse error
