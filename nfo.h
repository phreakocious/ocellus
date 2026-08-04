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
constexpr int NFO_PICKS     = 9;
// 26 fixed lines + up to 9 bracket rows (nine picks, none pairing) = 35. 40 is headroom.
constexpr int NFO_MAX_LINES = 40;

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
inline int nfoBuild(uint8_t grid[][NFO_COLS], int maxLines, uint32_t (*rng)(uint32_t)) {
  if (maxLines <= 0) return 0;   // guards row()'s grid[maxLines-1] from going negative
  using namespace nfo_detail;
  int n = 0;
  auto row = [&]() -> uint8_t* { return grid[n < maxLines ? n : maxLines - 1]; };
  auto adv = [&]() { if (n < maxLines) n++; };

  rule(row(), NFO_TL, NFO_TR);                                     adv();
  centre(row(), "N E K O R A M E N G A N G");                      adv();
  centre(row(), "P R O U D L Y  P R E S E N T S");                 adv();
  rule(row(), NFO_ML, NFO_MR);                                     adv();
  left(row(), "Release : ocellus_v1.0-NRG");                       adv();
  left(row(), "Supply  : kitsune");                                adv();
  left(row(), "Cracked : phreakocious");                           adv();
  centre(row(), "Date : 08/03/26  Type : GADGET");                 adv();
  rule(row(), NFO_ML, NFO_MR);                                     adv();
  centre(row(), "A W E S O M E   G R E E T Z");                    adv();
  centre(row(), "");                                               adv();
  centre(row(), "props and respect out to these");                 adv();
  centre(row(), "eLiTe folks for their support:");                 adv();
  centre(row(), "");                                               adv();

  // Bracket block: draw NFO_PICKS handles from the same bag the marquee uses, pack two-up
  // where the predicate allows. Row count varies with the shuffle -- that is why the caller
  // gets a length back instead of assuming one.
  ShuffleBag bag; bag.n = 0; bag.last = -1;
  const char* picks[NFO_PICKS];
  for (int i = 0; i < NFO_PICKS; i++)
    picks[i] = GREETZ_NAMES[shufflebagPick(bag, GREETZ_NAME_COUNT, rng)];

  char buf[NFO_COLS + 1];
  for (int i = 0; i < NFO_PICKS; ) {
    if (i + 1 < NFO_PICKS && nfoPairFits(picks[i], picks[i + 1])) {
      snprintf(buf, sizeof buf, "[ %s ] [ %s ]", picks[i], picks[i + 1]); i += 2;
    } else {
      snprintf(buf, sizeof buf, "[ %s ]", picks[i]); i += 1;
    }
    centre(row(), buf); adv();
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
