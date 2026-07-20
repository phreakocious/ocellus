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
