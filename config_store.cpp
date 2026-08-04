#include "config_store.h"
#include <Preferences.h>

void loadConfig(Config& c) {
  Preferences p;
  p.begin("ocellus", true);
  String s = p.getString("cfg", "");
  p.end();
  if (s.length()) configFromJson(std::string(s.c_str()), c);
}

void saveConfig(const Config& c) {
  Preferences p;
  p.begin("ocellus", false);
  p.putString("cfg", configToJson(c).c_str());
  p.end();
}

uint32_t treatsLoad() {
  Preferences p;
  p.begin("ocellus", true);
  uint32_t t = p.getUInt("treats", 0);
  p.end();
  return t;
}

void treatsSave(uint32_t treats) {
  Preferences p;
  p.begin("ocellus", false);
  p.putUInt("treats", treats);
  p.end();
}

uint8_t resumeIdLoad() {
  Preferences p;
  p.begin("ocellus", true);
  uint8_t id = p.getUChar("lastid", 0xFF);
  p.end();
  return id;
}

// Its own one-byte key rather than a Config field: saving through gConfig would rewrite the whole
// config JSON (~400-700 B) on every pick, and drag the codec, the protocol and config.html along
// for a value the user never sets by hand.
void resumeIdSave(uint8_t id) {
  Preferences p;
  p.begin("ocellus", false);
  p.putUChar("lastid", id);
  p.end();
}
