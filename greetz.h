#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "shufflebag.h"

// Content engine for the greetz scroller (animation id 47). Pure and Arduino-free so the host
// suite drives it: the RNG is injected, the caller owns the buffer, nothing allocates.
// This file is the source of truth for the content.

constexpr int    GREETZ_NAME_COUNT = 28;
constexpr size_t GREETZ_BUF        = 768;   // assembled loop is ~581 chars worst case
constexpr int    GREETZ_PAD        = 16;    // leading spaces; >= one screen (240px / 16px cell)

// Casing here is deliberate -- this is how each person spells their own handle. Do not uppercase.
inline const char* const GREETZ_NAMES[GREETZ_NAME_COUNT] = {
  "doc", "entropyth", "tonix", "Lulu", "Yukaia", "buttersnatcher", "Bitquark", "Sel",
  "bash", "bse", "trinaught", "mixtape", "preterition", "Crick3t", "flyingtoasters",
  "Piggles", "Kilk", "HellGiraffe", "tense future", "Acid T", "funktribe", "Donds",
  "Ming", "jekylzz", "wosdjco", "DT & DEF CON", "phreakocious", "kitsune"
};

inline const char* const GREETZ_HEADER =
  "GREETZ TO MEMBERS AND FRIENDS OF NEKORAMENGANG (NRG) IN NO PARTICULAR ORDER";
// "SHOUT OUT" and "BLAME THE BEER" are later retro-pastiche, not period scene text -- neither
// appears in ~110 archived nfos. "Special greets to" and a trailing "and anyone we forgot!"
// are what the corpus actually says. Shared with the .nfo crawl, which derives its wording
// from these so the two renderings of id 47 cannot drift apart.
inline const char* const GREETZ_SHOUT_PRE  = "SPECIAL GREETS TO ";
inline const char* const GREETZ_SHOUT_POST = " & THE WALL OF SHEEP";
inline const char* const GREETZ_MEMORY = "IN LOVING MEMORY OF BIND";
inline const char* const GREETZ_BEER   = "AND ANYONE WE FORGOT!";
inline const char* const GREETZ_CLOSER = "OVER AND OUT ------>";
inline const char* const GREETZ_SEP_SEG  = "   :::   ";
inline const char* const GREETZ_SEP_NAME = " :: ";

struct GreetzState {
  ShuffleBag bag;
  uint32_t   loops;    // loops assembled so far
  uint32_t   swapAt;   // loop number on which RIVERDADDY replaces RIVERSIDE
};

inline void greetzInit(GreetzState& s, uint32_t (*rng)(uint32_t)) {
  s.bag.n = 0; s.bag.last = -1;
  s.loops = 0;
  s.swapAt = 3 + rng(3);          // first fire lands on loop 3, 4, or 5
}

// Append src to out, never exceeding cap-1 bytes. Returns the new length.
// ponytail: truncation is the safety net, not a mode -- GREETZ_BUF is sized for the real content.
inline size_t greetzAppend(char* out, size_t cap, size_t len, const char* src) {
  while (*src && len + 1 < cap) out[len++] = *src++;
  out[len] = '\0';
  return len;
}

// Advance the loop counter and the RiverDaddy schedule by exactly one loop; returns true if this
// loop is a RIVERDADDY loop. Animation id 47 has TWO renderings (the marquee via greetzBuild and
// the .nfo crawl via nfoBuild) that alternate and share one GreetzState, so this must live in one
// place: duplicating the arithmetic let a change to one cadence silently skew the other, and each
// builder's own tests would still pass in isolation.
inline bool greetzAdvanceSchedule(GreetzState& s, uint32_t (*rng)(uint32_t)) {
  s.loops++;
  bool daddy = (s.loops == s.swapAt);
  if (daddy) s.swapAt = s.loops + 3 + rng(3);     // next fire 3-5 loops out
  return daddy;
}

// Assemble one loop's scroll string. Advances the loop counter and the RiverDaddy schedule.
// Call this only at scroll wrap -- rebuilding mid-scroll would change text still on screen.
inline size_t greetzBuild(GreetzState& s, char* out, size_t cap, uint32_t (*rng)(uint32_t)) {
  if (cap == 0) return 0;
  out[0] = '\0';
  size_t len = 0;

  bool daddy = greetzAdvanceSchedule(s, rng);

  for (int i = 0; i < GREETZ_PAD; i++) len = greetzAppend(out, cap, len, " ");
  len = greetzAppend(out, cap, len, GREETZ_HEADER);
  len = greetzAppend(out, cap, len, GREETZ_SEP_SEG);

  for (int i = 0; i < GREETZ_NAME_COUNT; i++) {
    if (i) len = greetzAppend(out, cap, len, GREETZ_SEP_NAME);
    len = greetzAppend(out, cap, len, GREETZ_NAMES[shufflebagPick(s.bag, GREETZ_NAME_COUNT, rng)]);
  }

  len = greetzAppend(out, cap, len, GREETZ_SEP_SEG);
  len = greetzAppend(out, cap, len, GREETZ_SHOUT_PRE);
  len = greetzAppend(out, cap, len, daddy ? "RIVERDADDY" : "RIVERSIDE");
  len = greetzAppend(out, cap, len, GREETZ_SHOUT_POST);
  len = greetzAppend(out, cap, len, GREETZ_SEP_SEG);
  len = greetzAppend(out, cap, len, GREETZ_MEMORY);
  len = greetzAppend(out, cap, len, GREETZ_SEP_SEG);
  len = greetzAppend(out, cap, len, GREETZ_BEER);
  len = greetzAppend(out, cap, len, GREETZ_SEP_SEG);
  len = greetzAppend(out, cap, len, GREETZ_CLOSER);
  return len;
}
