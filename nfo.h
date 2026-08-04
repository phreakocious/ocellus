#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "greetz.h"

// Content engine for the .nfo crawl (a second rendering of animation id 47). Pure and
// Arduino-free so the host suite drives it, exactly like greetz.h.
//
// The grid holds CP437 BYTES, not UTF-8. Box art transcribed as UTF-8 would spend three
// cells per character and render as mojibake.

constexpr int NFO_COLS      = 32;
constexpr int NFO_INNER     = NFO_COLS - 2;
// 36 fixed lines + up to GREETZ_NAME_COUNT comma-run rows (one name each in the worst case)
// = 64. No headroom needed: the static_assert below is the guard.
constexpr int NFO_MAX_LINES = 64;
constexpr int NFO_FIXED_LINES = 36;
// row()'s clamp is memory-SAFE but not content-safe: past the bound it repeatedly overwrites the
// last row, so growing the roster would silently swallow the trailing block (the shout-out,
// memory, sign-off and disclaimer) with every existing test still green -- names are emitted
// before those lines, and `n <= NFO_MAX_LINES` cannot tell "fits" from "was clamped".
// Bound against the full count, not the emitted count, so it still holds if the filter changes.
static_assert(NFO_MAX_LINES >= NFO_FIXED_LINES + GREETZ_NAME_COUNT,
              "NFO_MAX_LINES too small for the roster -- the tail of the box would be dropped");

// The release header credits these two by name, so the greets run omits them -- otherwise each
// appears twice in the same greets list. Both the header rows and the omission are built from
// these constants, so the credit and the filter cannot drift apart.
inline const char* const NFO_SUPPLY  = "kitsune";
inline const char* const NFO_CRACKED = "phreakocious";
inline const char* const NFO_RELEASE = "ocellus_v1.0-NRG";

constexpr uint8_t NFO_TL = 0xC9, NFO_TR = 0xBB, NFO_BL = 0xC8, NFO_BR = 0xBC;
constexpr uint8_t NFO_H  = 0xCD, NFO_V  = 0xBA, NFO_ML = 0xCC, NFO_MR = 0xB9;

// Widest label in the release block. Values get what is left after " : ".
constexpr int NFO_LABEL_W = 10;                                  // "PROTECTiON"
constexpr int NFO_VALUE_W = NFO_INNER - NFO_LABEL_W - 3;         // 16
// Greets rows stop a column short of each border. A comma run filling the interior edge-to-edge
// is legible on paper but not through the crawl's round mask.
constexpr int NFO_GREET_W = NFO_INNER - 2;                       // 28
static_assert(sizeof("ocellus_v1.0-NRG") - 1 <= NFO_VALUE_W,
              "release name would be truncated in the field block");

namespace nfo_detail {
inline void blank(uint8_t* row) { row[0] = NFO_V; row[NFO_COLS-1] = NFO_V;
                                  for (int i = 1; i < NFO_COLS-1; i++) row[i] = ' '; }
inline void rule(uint8_t* row, uint8_t l, uint8_t r) { row[0] = l; row[NFO_COLS-1] = r;
                                  for (int i = 1; i < NFO_COLS-1; i++) row[i] = NFO_H; }
inline void put(uint8_t* row, int at, const char* s) {
  for (int i = 0; s[i] && at + i < NFO_COLS - 1; i++) row[at + i] = (uint8_t)s[i];
}
inline void centre(uint8_t* row, const char* s) {
  int n = (int)strlen(s); if (n > NFO_INNER) n = NFO_INNER;
  blank(row); put(row, 1 + (NFO_INNER - n) / 2, s);
}
inline void left(uint8_t* row, const char* s) { blank(row); put(row, 1, s); }
// One column short of the border -- a sign-off kissing the ║ reads as a clipped line, and the
// round mask is already eating the box edges at the near end of the crawl.
inline void right(uint8_t* row, const char* s) {
  int n = (int)strlen(s); if (n > NFO_INNER - 1) n = NFO_INNER - 1;
  blank(row); put(row, NFO_COLS - 2 - n, s);
}
// Centred run of n tildes -- the heading underline every period nfo puts under its title.
inline void tilde(uint8_t* row, int n) {
  if (n > NFO_INNER) n = NFO_INNER;
  blank(row);
  for (int i = 0, at = 1 + (NFO_INNER - n) / 2; i < n; i++) row[at + i] = '~';
}
// Right-aligned label so every colon lands in one column, as in the Fairlight/Razor blocks.
inline void field(uint8_t* row, const char* label, const char* value) {
  char line[NFO_COLS + 1];
  snprintf(line, sizeof line, "%*s : %s", NFO_LABEL_W, label, value);
  left(row, line);
}
}  // namespace nfo_detail

// Fills `grid` and returns the number of lines written. Never writes more than maxLines.
// Takes the SAME GreetzState the marquee (greetzBuild) uses -- s.bag and the s.loops/swapAt
// RiverDaddy schedule are shared, so whichever rendering (marquee or crawl) greetzOnEnter
// picks, the egg advances at one consistent rate instead of the crawl silently not counting.
inline int nfoBuild(GreetzState& s, uint8_t grid[][NFO_COLS], int maxLines,
                     uint32_t (*rng)(uint32_t)) {
  if (maxLines <= 0) return 0;   // guards row()'s grid[maxLines-1] from going negative
  using namespace nfo_detail;
  int n = 0;
  auto row = [&]() -> uint8_t* { return grid[n < maxLines ? n : maxLines - 1]; };
  auto adv = [&]() { if (n < maxLines) n++; };

  // The SAME advance the marquee uses -- one shared helper, not a copy, so the two renderings
  // of id 47 cannot drift apart on the egg's cadence.
  bool daddy = greetzAdvanceSchedule(s, rng);

  rule(row(), NFO_TL, NFO_TR);                                     adv();
  centre(row(), "N E K O R A M E N G A N G");                      adv();
  centre(row(), "P R O U D L Y  P R E S E N T S");                 adv();
  tilde(row(), 26);                                                adv();
  centre(row(), "");                                               adv();
  centre(row(), "o c e l l u s  v 1 . 0");                         adv();
  centre(row(), "(c) NEKORAMEN GANG 2026");                        adv();
  rule(row(), NFO_ML, NFO_MR);                                     adv();

  // Release block. Labels carry the CP437-era lowercase-i leetspeak (SUPPLiER, RATiNG) that
  // shows up all over the corpus; values keep whatever casing their owner uses.
  field(row(), "RELEASE", NFO_RELEASE);                            adv();
  field(row(), "SUPPLiER", NFO_SUPPLY);                            adv();
  field(row(), "CRACKER", NFO_CRACKED);                            adv();
  field(row(), "PROTECTiON", "none");                              adv();
  field(row(), "RELEASED", "08/03/26");                            adv();
  field(row(), "TYPE", "GADGET");                                  adv();
  {
    // The [####----] meter sits beside the rating in ~40% of the corpus. 0xDB falls in the
    // 0xC0-0xDF range the blitter widens to the full 9-dot cell, so the filled run joins with
    // no gaps; 0xB0 does not, which is exactly what makes the empty cells read as lighter.
    char meter[] = "[..........]";
    for (int i = 0; i < 10; i++) meter[1 + i] = (char)(i < 9 ? 0xDB : 0xB0);
    field(row(), "RATiNG", meter);                                 adv();
  }
  rule(row(), NFO_ML, NFO_MR);                                     adv();

  centre(row(), "G R E E T S  G O  O U T  T O");                   adv();
  tilde(row(), 28);                                                adv();
  centre(row(), "");                                               adv();

  // Greets run: draw EVERY handle from the same bag the marquee uses (s.bag, not a local bag).
  // Row count varies with the shuffle -- that is why the caller gets a length back instead of
  // assuming one. All GREETZ_NAME_COUNT names appear, matching the marquee's "everyone once per
  // loop" guarantee -- showing only a subset reads as a truncated greetz list to anyone who
  // knows the crew.
  // Draw EVERY handle even though two are not emitted: the bag is SHARED with the marquee, so
  // drawing fewer than GREETZ_NAME_COUNT would leave it in a different state than greetzBuild
  // leaves it and desynchronise the shuffle between the two renderings. Filter the emission,
  // never the draw.
  const char* picks[GREETZ_NAME_COUNT];
  int np = 0;
  for (int i = 0; i < GREETZ_NAME_COUNT; i++) {
    const char* nm = GREETZ_NAMES[shufflebagPick(s.bag, GREETZ_NAME_COUNT, rng)];
    if (strcmp(nm, NFO_SUPPLY) == 0 || strcmp(nm, NFO_CRACKED) == 0) continue;  // credited above
    picks[np++] = nm;
  }

  // Greedy comma packing, the way every period greets list is set. A name longer than the
  // interior would be snprintf-truncated rather than overflow, but none is: the host suite
  // asserts every handle fits a row on its own.
  {
    char buf[NFO_COLS + 1];
    int len = 0; buf[0] = '\0';
    for (int i = 0; i < np; i++) {
      int add = (int)strlen(picks[i]) + (len ? 2 : 0);
      if (len && len + add > NFO_GREET_W) { centre(row(), buf); adv(); len = 0; buf[0] = '\0'; }
      if (len) { buf[len++] = ','; buf[len++] = ' '; buf[len] = '\0'; }
      len += snprintf(buf + len, sizeof buf - len, "%s", picks[i]);
    }
    if (len) { centre(row(), buf); adv(); }
  }

  // GREETZ_BEER closes the roster the way the corpus does -- "...and anyone we forgot!" tacked
  // onto the end of the run. It is short enough to sit on one row, so unlike the old wording
  // the nfo no longer keeps its own wrapped copy of the constant.
  centre(row(), GREETZ_BEER);                                      adv();

  // RiverDaddy shout-out, wrapped to 3 rows -- greetz.h assembles GREETZ_SHOUT_PRE + name +
  // GREETZ_SHOUT_POST into one run for the marquee, which a 30-column interior can't hold on
  // one line. The wrap text is DERIVED from those two constants (trimming the marquee's joiner
  // whitespace) rather than hardcoded, so the two renderings can't drift apart in wording.
  {
    char shout[NFO_COLS + 1];
    size_t preLen = strlen(GREETZ_SHOUT_PRE);
    while (preLen > 0 && GREETZ_SHOUT_PRE[preLen - 1] == ' ') preLen--;   // drop the joiner space
    snprintf(shout, sizeof shout, "%.*s", (int)preLen, GREETZ_SHOUT_PRE);
    centre(row(), "");                                             adv();
    centre(row(), shout);              /* "SPECIAL GREETS TO"    */adv();
    centre(row(), daddy ? "RIVERDADDY" : "RIVERSIDE");              adv();
    const char* post = GREETZ_SHOUT_POST;
    while (*post == ' ') post++;                                   // drop the joiner space,
    snprintf(shout, sizeof shout, "%s", post);                     // keep the leading '&'
    centre(row(), shout);              /* "& THE WALL OF SHEEP" */ adv();
  }

  centre(row(), "");                                               adv();
  centre(row(), "and to all our suppliers,");                      adv();
  centre(row(), "couriers and bbs sysops --");                     adv();
  centre(row(), "you know who you are!");                          adv();
  centre(row(), "");                                               adv();
  centre(row(), GREETZ_MEMORY);                                    adv();
  centre(row(), "");                                               adv();
  {
    // Built from the same two constants the release block credits, so the sign-off cannot
    // drift from the header the way a hardcoded pair of handles would.
    char sig[NFO_COLS + 1];
    snprintf(sig, sizeof sig, "- %s + %s", NFO_CRACKED, NFO_SUPPLY);
    right(row(), sig);                                             adv();
  }
  rule(row(), NFO_ML, NFO_MR);                                     adv();
  centre(row(), "SUPPORT THE PEOPLE WHO MAKE");                    adv();
  centre(row(), "THE THINGS YOU LOVE!");                           adv();
  rule(row(), NFO_BL, NFO_BR);                                     adv();
  return n;
}
