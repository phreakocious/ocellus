#include "config.h"
#include "palette.h"
#include "animations.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>

uint16_t hexToRgb565(const std::string& hex) {
  long v = strtol(hex.c_str() + (hex.size() && hex[0] == '#' ? 1 : 0), nullptr, 16);
  uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

std::string rgb565ToHex(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  char buf[8];
  snprintf(buf, sizeof buf, "#%02x%02x%02x", r, g, b);
  return std::string(buf);
}

// Bit (r,c) of a row-major MSB-first hex-packed module matrix, decoded nibble-at-a-time (no buffer).
bool qrModule(const std::string& hexBits, int size, int r, int c) {
  if (r < 0 || c < 0 || r >= size || c >= size) return false;
  int i = r * size + c;
  size_t ci = (size_t)(i / 8) * 2 + ((i % 8) >= 4);   // hex char holding this bit's nibble
  if (ci >= hexBits.size()) return false;
  char h = hexBits[ci];
  int v = (h >= '0' && h <= '9') ? h - '0'
        : ((h | 32) >= 'a' && (h | 32) <= 'f') ? (h | 32) - 'a' + 10 : 0;
  return (v >> (3 - (i & 3))) & 1;
}

std::string configToJson(const Config& c) {
  JsonDocument d;
  d["type"] = "config";
  d["name"] = c.name;
  d["brightness"] = c.brightness;
  d["sleepMin"] = c.sleepMin;
  d["maxFps"] = c.maxFps;
  d["flip"] = c.flip;
  d["skinColor"] = rgb565ToHex(c.skinColor);
  d["irisColor"] = rgb565ToHex(c.irisColor);
  d["voidColor"] = rgb565ToHex(c.voidColor);
  d["eyelids"] = c.eyelids;
  d["sbPalette"] = c.sbPalette;
  d["slideshowSec"] = c.slideshowSec;
  JsonObject st = d["startup"].to<JsonObject>();
  st["mode"] = c.startupMode;
  st["id"] = c.startupId;
  JsonObject ns = d["nameStyle"].to<JsonObject>();
  ns["matrixRain"] = c.nameMatrixRain;
  ns["bootSplash"] = c.nameBootSplash;
  ns["splashStyle"] = c.bootSplashStyle;
  JsonArray fav = d["favorites"].to<JsonArray>();
  for (int i = 0; i < ANIM_COUNT; i++) if (c.favoritesMask & (1ull << i)) fav.add(i);
  d["cycleSec"] = c.cycleSec;
  JsonArray cyc = d["cycleAnims"].to<JsonArray>();
  for (int i = 0; i < ANIM_COUNT; i++) if (c.cycleMask & (1ull << i)) cyc.add(i);
  d["stayAwakeUsb"] = c.stayAwakeUsb;
  d["qrText"] = c.qrText;
  d["qrSize"] = c.qrSize;
  d["qrBits"] = c.qrBits;
  JsonObject pal = d["palettes"].to<JsonObject>();
  JsonArray en = pal["enabled"].to<JsonArray>();
  for (int i = 0; i < 32; i++) if (c.palettesEnabled & (1u << i)) en.add(i);
  pal["rotateSec"] = c.paletteRotateSec;
  JsonArray cust = pal["custom"].to<JsonArray>();
  for (size_t i = 0; i < c.customPalettes.size(); i++) {
    JsonObject po = cust.add<JsonObject>();
    po["id"] = CUSTOM_ID_BASE + (int)i;
    po["name"] = c.customPalettes[i].name;
    JsonArray col = po["colors"].to<JsonArray>();
    for (uint16_t x : c.customPalettes[i].colors) col.add(rgb565ToHex(x));
  }
  std::string out;
  serializeJson(d, out);
  return out;
}

bool configFromJson(const std::string& json, Config& c) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  if (d["name"].is<const char*>())      c.name = d["name"].as<std::string>();
  if (d["brightness"].is<int>())        { int v = d["brightness"].as<int>(); c.brightness = v < 0 ? 0 : (v > 255 ? 255 : v); }
  if (d["sleepMin"].is<int>())          { int v = d["sleepMin"].as<int>(); c.sleepMin = v < 0 ? 0 : (v > 255 ? 255 : v); }
  if (d["maxFps"].is<int>())            { int v = d["maxFps"].as<int>(); c.maxFps = v < 1 ? 1 : (v > 120 ? 120 : v); }
  if (d["flip"].is<bool>())             c.flip = d["flip"];
  if (d["skinColor"].is<const char*>()) c.skinColor = hexToRgb565(d["skinColor"].as<std::string>());
  if (d["irisColor"].is<const char*>()) c.irisColor = hexToRgb565(d["irisColor"].as<std::string>());
  if (d["voidColor"].is<const char*>()) c.voidColor = hexToRgb565(d["voidColor"].as<std::string>());
  if (d["eyelids"].is<bool>())          c.eyelids = d["eyelids"];
  if (d["sbPalette"].is<bool>())        c.sbPalette = d["sbPalette"];
  if (d["slideshowSec"].is<int>()) { int v = d["slideshowSec"].as<int>(); c.slideshowSec = v < 1 ? 1 : (v > 60 ? 60 : v); }
  if (d["startup"].is<JsonObject>()) {
    if (d["startup"]["mode"].is<const char*>()) c.startupMode = d["startup"]["mode"].as<std::string>();
    if (d["startup"]["id"].is<int>())           { int v = d["startup"]["id"].as<int>(); c.startupId = isPlayableId(v) ? (uint8_t)v : 0; }
  }
  if (d["nameStyle"].is<JsonObject>()) {
    if (d["nameStyle"]["matrixRain"].is<bool>()) c.nameMatrixRain = d["nameStyle"]["matrixRain"];
    if (d["nameStyle"]["bootSplash"].is<bool>()) c.nameBootSplash = d["nameStyle"]["bootSplash"];
    if (d["nameStyle"]["splashStyle"].is<const char*>()) c.bootSplashStyle = d["nameStyle"]["splashStyle"].as<std::string>();
  }
  if (d["favorites"].is<JsonArray>()) {
    c.favoritesMask = 0;
    for (JsonVariant v : d["favorites"].as<JsonArray>()) {
      int i = v.as<int>();
      if (isPlayableId(i)) c.favoritesMask |= (1ull << i);   // holes/debug never enter the mask
    }
  }
  if (d["cycleSec"].is<int>()) { int v = d["cycleSec"].as<int>(); c.cycleSec = v < 0 ? 0 : (v > 65535 ? 65535 : v); }
  if (d["cycleAnims"].is<JsonArray>()) {
    c.cycleMask = 0;
    for (JsonVariant v : d["cycleAnims"].as<JsonArray>()) {
      int i = v.as<int>();
      if (isPlayableId(i)) c.cycleMask |= (1ull << i);
    }
  }
  if (d["stayAwakeUsb"].is<bool>()) c.stayAwakeUsb = d["stayAwakeUsb"];
  if (d["qrText"].is<const char*>()) c.qrText = d["qrText"].as<std::string>();
  if (d["qrSize"].is<int>()) { int v = d["qrSize"].as<int>(); c.qrSize = (v >= 21 && v <= 177) ? (uint8_t)v : 0; }  // real QR versions only; else unconfigured
  if (d["qrBits"].is<const char*>()) {
    c.qrBits = d["qrBits"].as<std::string>();
    for (char h : c.qrBits)
      if (!((h >= '0' && h <= '9') || ((h | 32) >= 'a' && (h | 32) <= 'f'))) { c.qrBits.clear(); break; }  // non-hex = corrupt line, drop
  }
  if (d["palettes"].is<JsonObject>()) {
    JsonVariant p = d["palettes"];
    if (p["enabled"].is<JsonArray>()) {
      c.palettesEnabled = 0;
      for (JsonVariant v : p["enabled"].as<JsonArray>()) {
        int i = v.as<int>();
        if (i >= 0 && i < 32) c.palettesEnabled |= (1u << i);
      }
    }
    if (p["rotateSec"].is<int>()) { int v = p["rotateSec"].as<int>(); c.paletteRotateSec = v < 0 ? 0 : (v > 65535 ? 65535 : v); }
    if (p["custom"].is<JsonArray>()) {
      c.customPalettes.clear();
      for (JsonObject po : p["custom"].as<JsonArray>()) {
        if (c.customPalettes.size() >= (size_t)MAX_CUSTOM) break;
        Palette pl;
        if (po["name"].is<const char*>()) pl.name = po["name"].as<std::string>();
        for (JsonVariant col : po["colors"].as<JsonArray>())
          pl.colors.push_back(hexToRgb565(col.as<std::string>()));
        c.customPalettes.push_back(pl);
      }
    }
  }
  return true;
}
