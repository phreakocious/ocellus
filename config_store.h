#pragma once
#include "config.h"

void loadConfig(Config& c);        // reads NVS "ocellus"/"cfg"; leaves defaults if absent
void saveConfig(const Config& c);  // writes config JSON to NVS

uint32_t treatsLoad();          // treatcat lifetime treat counter; 0 on blank NVS
void     treatsSave(uint32_t treats);

// Last DELIBERATELY-picked animation id, for `resume` across a cold boot -- RTC_DATA_ATTR only
// survives deep sleep. 0xFF on blank NVS (not a playable id, so it falls through to 0).
uint8_t  resumeIdLoad();
void     resumeIdSave(uint8_t id);
