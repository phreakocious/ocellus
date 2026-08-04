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
// 30 fixed lines (incl. the RiverDaddy shout-out: 3 content rows plus a leading blank) + up to
// GREETZ_NAME_COUNT - 2 bracket rows, none pairing, = 56. 60 is headroom.
constexpr int NFO_MAX_LINES = 60;
constexpr int NFO_FIXED_LINES = 30;
// row()'s clamp is memory-SAFE but not content-safe: past the bound it repeatedly overwrites the
// last row, so growing the roster would silently swallow the trailing block (cheers / memory /
// beer / OVER AND OUT and the closing rule) with every existing test still green -- names are
// emitted before those lines, and `n <= NFO_MAX_LINES` cannot tell "fits" from "was clamped".
// Bound against the full count, not the emitted count, so it still holds if the filter changes.
static_assert(NFO_MAX_LINES >= NFO_FIXED_LINES + GREETZ_NAME_COUNT,
              "NFO_MAX_LINES too small for the roster -- the tail of the box would be dropped");

// The release header credits these two by name, so the shuffled bracket list omits them --
// otherwise each appears twice in the same box. Both the header rows and the omission are
// built from these constants, so the credit and the filter cannot drift apart.
inline const char* const NFO_SUPPLY  = "kitsune";
inline const char* const NFO_CRACKED = "phreakocious";

constexpr uint8_t NFO_TL = 0xC9, NFO_TR = 0xBB, NFO_BL = 0xC8, NFO_BR = 0xBC;
constexpr uint8_t NFO_H  = 0xCD, NFO_V  = 0xBA, NFO_ML = 0xCC, NFO_MR = 0xB9;

// "[ a ] [ b ]" = (len(a)+4) + 1 + (len(b)+4). Against NFO_INNER that is len(a)+len(b) <= 21.
inline bool nfoPairFits(const char* a, const char* b) {
  return (int)strlen(a) + (int)strlen(b) + 9 <= NFO_INNER;
}

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
  rule(row(), NFO_ML, NFO_MR);                                     adv();
  left(row(), "Release : ocellus_v1.0-NRG");                       adv();
  {
    char credit[NFO_COLS + 1];
    snprintf(credit, sizeof credit, "Supply  : %s", NFO_SUPPLY);
    left(row(), credit);                                           adv();
    snprintf(credit, sizeof credit, "Cracked : %s", NFO_CRACKED);
    left(row(), credit);                                           adv();
  }
  centre(row(), "Date : 08/03/26  Type : GADGET");                 adv();
  rule(row(), NFO_ML, NFO_MR);                                     adv();
  centre(row(), "A W E S O M E   G R E E T Z");                    adv();
  centre(row(), "");                                               adv();
  centre(row(), "props and respect out to these");                 adv();
  centre(row(), "eLiTe folks for their support:");                 adv();
  centre(row(), "");                                               adv();

  // Bracket block: draw EVERY handle from the same bag the marquee uses (s.bag, not a local
  // bag), pack two-up where the predicate allows. Row count varies with the shuffle -- that is
  // why the caller gets a length back instead of assuming one. All GREETZ_NAME_COUNT names
  // appear, matching the marquee's "everyone once per loop" guarantee -- showing only a subset
  // reads as a truncated greetz list to anyone who knows the crew.
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

  char buf[NFO_COLS + 1];
  for (int i = 0; i < np; ) {
    if (i + 1 < np && nfoPairFits(picks[i], picks[i + 1])) {
      snprintf(buf, sizeof buf, "[ %s ] [ %s ]", picks[i], picks[i + 1]); i += 2;
    } else {
      snprintf(buf, sizeof buf, "[ %s ]", picks[i]); i += 1;
    }
    centre(row(), buf); adv();
  }

  // RiverDaddy shout-out, wrapped to 3 rows -- greetz.h assembles GREETZ_SHOUT_PRE + name +
  // GREETZ_SHOUT_POST into one ~50-char run for the marquee, which a 30-column interior can't
  // hold on one line. The wrap text is DERIVED from those two constants (trimming the marquee's
  // joiner whitespace) rather than hardcoded, so the two renderings can't drift apart in wording.
  {
    char shout[NFO_COLS + 1];
    size_t preLen = strlen(GREETZ_SHOUT_PRE);
    while (preLen > 0 && GREETZ_SHOUT_PRE[preLen - 1] == ' ') preLen--;   // drop the joiner space
    snprintf(shout, sizeof shout, "%.*s", (int)preLen, GREETZ_SHOUT_PRE);
    centre(row(), "");                                             adv();
    centre(row(), shout);              /* "SPECIAL SHOUT OUT TO" */adv();
    centre(row(), daddy ? "RIVERDADDY" : "RIVERSIDE");              adv();
    const char* post = GREETZ_SHOUT_POST;
    while (*post == ' ') post++;                                   // drop the joiner space,
    snprintf(shout, sizeof shout, "%s", post);                     // keep the leading '&'
    centre(row(), shout);              /* "& THE WALL OF SHEEP" */ adv();
  }

  centre(row(), "");                                               adv();
  centre(row(), "cheers to all our suppliers,");                   adv();
  centre(row(), "couriers and bbs ops keeping");                   adv();
  centre(row(), "the old school spirit alive!");                   adv();
  centre(row(), "");                                               adv();
  centre(row(), GREETZ_MEMORY);                                    adv();
  centre(row(), "");                                               adv();
  // GREETZ_BEER is 33 chars against a 30-column interior, so the nfo owns a wrapped
  // rendering of it. The constant is the source of the sentiment, not the bytes.
  centre(row(), "if we forgot you,");                              adv();
  centre(row(), "blame the beer!");                                adv();
  centre(row(), "");                                               adv();
  centre(row(), GREETZ_CLOSER);                                    adv();
  rule(row(), NFO_BL, NFO_BR);                                     adv();
  return n;
}
