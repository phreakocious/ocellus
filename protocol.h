#pragma once
#include <string>
#include "config.h"

std::string catalogJson();
// Parses one JSON command line, mutates cfg for "set", returns a JSON response.
// Sets changed=true only when cfg was modified (caller persists).
// animSelOut (optional): the "anim" command writes a requested live-animation id here (else -1).
std::string handleLine(const std::string& line, Config& cfg, bool& changed, int* animSelOut = nullptr);
