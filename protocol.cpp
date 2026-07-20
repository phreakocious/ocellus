#include "protocol.h"
#include "animations.h"
#include "palette.h"
#include <ArduinoJson.h>

static_assert(PRESET_COUNT == PALETTE_PRESET_COUNT,
              "preset count drift: palette.h PRESET_COUNT vs animations.h PALETTE_PRESETS[]");

std::string catalogJson() {
  JsonDocument d;
  d["type"] = "catalog";
  JsonArray a = d["animations"].to<JsonArray>();
  for (const AnimInfo& info : ANIMS) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = info.id;
    o["name"] = info.name;
    o["group"] = info.group;
  }
  JsonArray p = d["palettes"].to<JsonArray>();
  for (int i = 0; i < PALETTE_PRESET_COUNT; i++) {
    JsonObject o = p.add<JsonObject>();
    o["id"] = i;
    o["name"] = PALETTE_PRESETS[i];
    o["builtin"] = true;
  }
  std::string out;
  serializeJson(d, out);
  return out;
}

std::string handleLine(const std::string& line, Config& cfg, bool& changed, int* animSelOut) {
  changed = false;
  if (animSelOut) *animSelOut = -1;
  JsonDocument d;
  if (deserializeJson(d, line)) return "{\"type\":\"err\",\"msg\":\"bad json\"}";
  std::string cmd = d["cmd"].is<const char*>() ? d["cmd"].as<std::string>() : "";
  if (cmd == "catalog") return catalogJson();
  if (cmd == "get")     return configToJson(cfg);
  if (cmd == "anim") {   // jump the live animation (not persisted); id validated against the registry
    if (!d["id"].is<int>()) return "{\"type\":\"err\",\"msg\":\"no id\"}";
    int id = d["id"].as<int>();
    if (!animIdKnown(id)) return "{\"type\":\"err\",\"msg\":\"bad id\"}";  // registry membership: rejects the reserved holes, allows debug ids
    if (animSelOut) *animSelOut = id;
    JsonDocument r; r["type"] = "anim"; r["id"] = id;
    std::string out; serializeJson(r, out); return out;
  }
  if (cmd == "set") {
    if (!d["config"].is<JsonObject>()) return "{\"type\":\"err\",\"msg\":\"no config\"}";
    std::string sub;
    serializeJson(d["config"], sub);
    if (!configFromJson(sub, cfg)) return "{\"type\":\"err\",\"msg\":\"bad config\"}";
    changed = true;
    return configToJson(cfg);
  }
  return "{\"type\":\"err\",\"msg\":\"unknown cmd\"}";
}
