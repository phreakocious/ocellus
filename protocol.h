#pragma once
#include <string>
#include "config.h"

std::string catalogJson();

// Persist hook for "set": returns false when the store refused the write (NVS's 4000-byte string
// cap, full partition). A function pointer, not <Preferences>, keeps this module Arduino-free --
// the device injects saveConfig, the native tests a fake. Same structure as the slide/gif stores.
using ConfigPersistFn = bool (*)(const Config&);

// Parses one JSON command line, mutates cfg for "set", returns a JSON response.
// Sets changed=true only when cfg was modified. With persist == nullptr the caller persists on
// changed (legacy wiring); with a persist fn, "set" persists HERE and a refused write answers
// {"type":"err","msg":"cfg too big"} instead of the config echo -- the echo is serialized from the
// RAM struct, so it cannot see a failed NVS write and would pass the host's echo-verify. changed
// stays true on a refused write (RAM took the set; the caller still applies side effects).
// animSelOut (optional): the "anim" command writes a requested live-animation id here (else -1).
std::string handleLine(const std::string& line, Config& cfg, bool& changed, int* animSelOut = nullptr,
                       ConfigPersistFn persist = nullptr);
