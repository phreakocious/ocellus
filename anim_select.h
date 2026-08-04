#pragma once
#include <cstdint>
#include <string>
#include "animations.h"

// Next playable animation id, searching forward from `cur` (exclusive) and wrapping,
// whose favorites bit is set. mask == 0 means "no favorites set" => every playable
// counts. Masked with PLAYABLE_MASK so non-playable ids (debug 42..44, anything unknown) are
// never landed on, even if a stale config set their bits.
inline uint8_t nextFavorite(uint64_t mask, uint8_t cur) {
  mask &= PLAYABLE_MASK;
  if (mask == 0) mask = PLAYABLE_MASK;
  for (int step = 1; step <= ANIM_COUNT; step++) {
    uint8_t id = (uint8_t)((cur + step) % ANIM_COUNT);
    if (mask & (1ull << id)) return id;
  }
  return cur;  // unreachable: mask is never 0 after the guard
}

// Previous playable animation id, searching backward from `cur` (exclusive) and wrapping,
// whose favorites bit is set. Mirror of nextFavorite for swipe-left paging.
inline uint8_t prevFavorite(uint64_t mask, uint8_t cur) {
  mask &= PLAYABLE_MASK;
  if (mask == 0) mask = PLAYABLE_MASK;
  for (int step = 1; step <= ANIM_COUNT; step++) {
    uint8_t id = (uint8_t)((cur + ANIM_COUNT - step) % ANIM_COUNT);
    if (mask & (1ull << id)) return id;
  }
  return cur;  // unreachable: mask is never 0 after the guard
}

// Advance `delta` favorites forward (delta > 0) or backward (delta < 0) from `cur`; delta == 0 is a
// no-op. One rotary-encoder detent = one step, so a fast spin advances N modes rather than one.
// Delta is clamped to +/-ANIM_COUNT: more than a full wrap adds nothing, and it keeps the loop
// bounded (and INT_MIN from overflowing when negated).
inline uint8_t stepFavorite(uint64_t mask, uint8_t cur, int delta) {
  if (delta >  ANIM_COUNT) delta =  ANIM_COUNT;
  if (delta < -ANIM_COUNT) delta = -ANIM_COUNT;
  for (int i = 0; i < delta;  i++) cur = nextFavorite(mask, cur);
  for (int i = 0; i > delta;  i--) cur = prevFavorite(mask, cur);
  return cur;
}

// The carousel's scrollable list: exactly the ids nextFavorite() walks, ascending. Lives here
// beside nextFavorite so the two cannot drift apart, and so the invariant is testable.
inline int carouselList(uint64_t mask, uint8_t* out, int cap) {
  mask &= PLAYABLE_MASK;
  if (mask == 0) mask = PLAYABLE_MASK;
  int n = 0;
  for (int id = 0; id < ANIM_COUNT && n < cap; id++)
    if (mask & (1ull << id)) out[n++] = (uint8_t)id;
  return n;
}

// Power-on animation id. mode "fixed" -> fixedId, "random" -> randomPick, anything else -> resumeId.
// Every branch is clamped: debug ids, the id space's holes and outright garbage fall through rather
// than being landed on (carried Phase-1 review item: startupId bound).
//
// `resume` with NOTHING TO RESUME falls back to fixedId, not to 0. A fresh unit has no stored pick
// (blank NVS, and .rtc.data was reloaded on the cold boot), and coming up on eye 0 made every new
// board look identical out of the box -- the configured startup animation, Greetz by default, is a
// better introduction. The caller signals "nothing stored" by passing a non-playable resumeId,
// which is what resumeIdLoad() returns (0xFF) on blank NVS.
inline uint8_t resolveStartupId(const std::string& mode, uint8_t fixedId,
                                uint8_t resumeId, uint8_t randomPick) {
  if (mode == "fixed")  return isPlayableId(fixedId)    ? fixedId    : 0;
  if (mode == "random") return isPlayableId(randomPick) ? randomPick : 0;
  if (isPlayableId(resumeId)) return resumeId;
  return isPlayableId(fixedId) ? fixedId : 0;
}
