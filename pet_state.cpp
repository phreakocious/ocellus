#include "pet_state.h"
#include <string.h>
#include <stdlib.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void PetState::reset(uint32_t treats) {
  full = 100.0f; en = 100.0f; tr = treats; sleeping = false;
}

void PetState::debugSet(float fullness, float energy) {
  full = clampf(fullness, PET_FULL_FLOOR, 100.0f);
  en   = clampf(energy,   PET_ENERGY_FLOOR, 100.0f);
  // Re-derive the latch from the new energy using the SAME thresholds tick() does. Inside the
  // hysteresis band [SLEEP_LO, SLEEP_HI] there is no single right answer, so leave the latch
  // alone there -- that band exists precisely to make the state depend on which way you came.
  if (en <= PET_SLEEP_LO) sleeping = true;
  else if (en >= PET_SLEEP_HI) sleeping = false;
}

void PetState::tick(float dtMs, bool careEnabled) {
  if (!careEnabled) { sleeping = false; return; }   // ambient: frozen, never hungry
  float dt = dtMs / 1000.0f;                          // REAL seconds (NOT treatcat's 0.45 lab-time)
  full = clampf(full - PET_FULLNESS_DRAIN * dt, PET_FULL_FLOOR, 100.0f);
  if (sleeping) {
    en = clampf(en + PET_ENERGY_RECOVER * dt, PET_ENERGY_FLOOR, 100.0f);
    if (en >= PET_SLEEP_HI) sleeping = false;         // hysteresis exit
  } else {
    en = clampf(en - PET_ENERGY_DRAIN * dt, PET_ENERGY_FLOOR, 100.0f);
    if (en <= PET_SLEEP_LO) sleeping = true;          // hysteresis enter
  }
}

bool PetState::hungry() const { return full < PET_HUNGRY_THRESH; }

void PetState::forceWake() {
  sleeping = false;
  float floor = PET_SLEEP_LO + PET_WAKE_MARGIN;
  if (en < floor) en = floor;                         // postcondition: energy > SLEEP_LO
}

bool PetState::feed() {
  if (!hungry()) return false;
  full = clampf(full + PET_FEED_GAIN, PET_FULL_FLOOR, 100.0f);
  tr++;
  return true;
}

PetMood PetState::mood(bool careEnabled) const {
  if (!careEnabled) return PET_CONTENT;
  if (sleeping)     return PET_SLEEPY;
  if (hungry())     return PET_NEEDY;
  return PET_CONTENT;
}

// "key":<value> in compact JSON. nullptr when the key is missing or the ':' is not immediately
// after it, so a truncated `"full"` tail (RX drop) or a spaced `"full" : 5` reads as absent
// instead of dereferencing strchr(NULL)+1 or stealing a later key's value.
static const char* petVal(const char* line, const char* key) {
  const char* p = strstr(line, key);
  if (!p) return nullptr;
  p += strlen(key);
  return *p == ':' ? p + 1 : nullptr;
}

bool petCmdParse(const char* line, PetCmd& out) {
  out = PetCmd{};
  out.sim = strstr(line, "\"cmd\":\"petsim\"") != nullptr;
  out.tap = strstr(line, "\"cmd\":\"pettap\"") != nullptr;
  // the closing quote keeps "pet" from matching inside petsim/pettap, so order is free
  if (!out.sim && !out.tap && strstr(line, "\"cmd\":\"pet\"") == nullptr) return false;
  if (out.sim) {
    if (const char* v = petVal(line, "\"full\""))   { out.full   = (float)atof(v);     out.hasFull   = true; }
    if (const char* v = petVal(line, "\"en\""))     { out.en     = (float)atof(v);     out.hasEn     = true; }
    if (const char* v = petVal(line, "\"treats\"")) { out.treats = (uint32_t)atoi(v);  out.hasTreats = true; }
  }
  return true;
}
