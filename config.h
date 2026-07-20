#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Palette {
  std::string name;
  std::vector<uint16_t> colors;   // RGB565
};

struct Config {
  std::string name = "ocellus";
  uint8_t  brightness = 50;
  uint8_t  sleepMin = 15;
  uint8_t  maxFps = 30;                 // frame cap for non-audio modes; audio modes run uncapped
  bool     flip = false;                // rotate display 180 deg (upside-down enclosure mount)
  uint16_t skinColor = 0xFDB2;          // eyelid/skin tone (RGB565); default peach
  uint16_t irisColor = 0x7FFF;          // iris tint (RGB565) for standard eye themes; default cyan
  uint16_t voidColor = 0x0010;          // Void theme background (RGB565); default deep blue
  bool     eyelids = true;              // draw eyelids over the eye themes
  bool     sbPalette = false;           // audio modes follow the console's live colors (SB virtual palette)
  uint16_t slideshowSec = 5;            // seconds per slide in Slideshow mode (id 32); clamped [1,60]
  std::string startupMode = "resume";  // resume | fixed | random
  uint8_t  startupId = 0;
  bool     nameMatrixRain = true;
  bool     nameBootSplash = true;
  std::string bootSplashStyle = "bounce";  // matrix | slide | bounce (only when nameBootSplash)
  uint64_t favoritesMask = 0;          // bit i => animation id i (0 = all play); 64-bit for up to 64 animations
  uint16_t cycleSec = 0;               // auto-cycle favorites every N s; 0 = off
  uint64_t cycleMask = 0;              // bit i => anim id i auto-cycles; 0 = cycle the favorites
  bool     stayAwakeUsb = true;        // USB power counts as activity -> no idle sleep while plugged in (sensed by charge-held battery voltage; battery.h)
  uint32_t palettesEnabled = 1;        // bit 0 = Rainbow preset
  uint16_t paletteRotateSec = 30;
  std::vector<Palette> customPalettes; // cap 4
  std::string qrText = "";             // QR source URL/text; persisted only so config.html can prefill -- device ignores it
  uint8_t     qrSize = 0;              // QR modules per side (21..177); 0 = unconfigured
  std::string qrBits = "";             // packed 1-bit QR modules, row-major MSB-first, hex (config.html encodes; device blits)
};

uint16_t hexToRgb565(const std::string& hex);   // "#RRGGBB"
std::string rgb565ToHex(uint16_t c);
bool qrModule(const std::string& hexBits, int size, int r, int c);  // module (r,c) dark? false out of range/short bits

std::string configToJson(const Config& c);
bool configFromJson(const std::string& json, Config& c);  // partial merge; false on parse error
