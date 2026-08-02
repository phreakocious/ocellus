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
