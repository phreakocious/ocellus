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

// Power-on animation id. mode "fixed" -> fixedId, "random" -> randomPick, anything else -> resumeId.
// Non-playable (debug, reserved holes, garbage) -> 0 (carried Phase-1 review item: startupId bound).
inline uint8_t resolveStartupId(const std::string& mode, uint8_t fixedId,
                                uint8_t resumeId, uint8_t randomPick) {
  uint8_t id = (mode == "fixed") ? fixedId
             : (mode == "random") ? randomPick
             : resumeId;
  return isPlayableId(id) ? id : 0;
}
