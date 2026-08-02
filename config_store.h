#pragma once
#include "config.h"

void loadConfig(Config& c);        // reads NVS "ocellus"/"cfg"; leaves defaults if absent
void saveConfig(const Config& c);  // writes config JSON to NVS

uint32_t treatsLoad();          // treatcat lifetime treat counter; 0 on blank NVS
void     treatsSave(uint32_t treats);
