#pragma once
#include "config.h"

void loadConfig(Config& c);        // reads NVS "ocellus"/"cfg"; leaves defaults if absent
void saveConfig(const Config& c);  // writes config JSON to NVS
