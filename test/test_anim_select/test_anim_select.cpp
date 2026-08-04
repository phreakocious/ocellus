#include <unity.h>
#include <cstring>
#include "../../anim_select.h"

void test_next_of_all_when_mask_empty() {
    TEST_ASSERT_EQUAL_UINT8(1,  nextFavorite(0, 0));
    TEST_ASSERT_EQUAL_UINT8(30, nextFavorite(0, 29));   // 29 -> 30 (Fluid), unchanged
    TEST_ASSERT_EQUAL_UINT8(34, nextFavorite(0, 33));   // 33 (Wormhole) -> 34 (QR)
    TEST_ASSERT_EQUAL_UINT8(35, nextFavorite(0, 34));   // 34 (QR) -> 35 (Toasters)
    TEST_ASSERT_EQUAL_UINT8(36, nextFavorite(0, 35));   // 35 (Toasters) -> 36 (Boids)
    TEST_ASSERT_EQUAL_UINT8(37, nextFavorite(0, 36));   // 36 (Boids) -> 37 (Garden Eels)
    TEST_ASSERT_EQUAL_UINT8(38, nextFavorite(0, 37));   // 37 (Garden Eels) -> 38 (Bloom): id space is contiguous now
    TEST_ASSERT_EQUAL_UINT8(41, nextFavorite(0, 40));   // 40 -> 41 (Echo)
    TEST_ASSERT_EQUAL_UINT8(45, nextFavorite(0, 41));   // 41 (Echo) -> 45 (Swirl): debug ids 42..44 skipped
    TEST_ASSERT_EQUAL_UINT8(46, nextFavorite(0, 45));   // 45 (Swirl) -> 46 (treatcat)
    TEST_ASSERT_EQUAL_UINT8(47, nextFavorite(0, 46));   // 46 (treatcat) -> 47 (Greetz)
    TEST_ASSERT_EQUAL_UINT8(48, nextFavorite(0, 47));   // 47 (Greetz) -> 48 (GIFs)
    TEST_ASSERT_EQUAL_UINT8(49, nextFavorite(0, 48));   // 48 (GIFs) -> 49 (first ported lab effect)
    TEST_ASSERT_EQUAL_UINT8(55, nextFavorite(0, 54));   // 54 -> 55 (Fermat Spiral, last playable)
    TEST_ASSERT_EQUAL_UINT8(0,  nextFavorite(0, 55));   // 55 (last) -> wraps to 0
}
void test_next_skips_to_set_bit() {
    uint32_t mask = (1u << 4) | (1u << 17);
    TEST_ASSERT_EQUAL_UINT8(4,  nextFavorite(mask, 0));
    TEST_ASSERT_EQUAL_UINT8(17, nextFavorite(mask, 4));
    TEST_ASSERT_EQUAL_UINT8(4,  nextFavorite(mask, 17));  // wraps back
    TEST_ASSERT_EQUAL_UINT8(41, nextFavorite(1ull << 41, 0));   // boundary bit: Echo is id 41
    TEST_ASSERT_EQUAL_UINT8(1,  nextFavorite(1ull << 42, 0));   // only a non-playable (debug) bit set = as-if-empty -> 1
}
void test_single_favorite_returns_itself() {
    uint32_t mask = (1u << 9);
    TEST_ASSERT_EQUAL_UINT8(9, nextFavorite(mask, 9));  // only favorite -> stays
    TEST_ASSERT_EQUAL_UINT8(9, nextFavorite(mask, 3));
}
void test_prev_of_all_when_mask_empty() {
    TEST_ASSERT_EQUAL_UINT8(0,  prevFavorite(0, 1));    // 1 -> 0
    TEST_ASSERT_EQUAL_UINT8(29, prevFavorite(0, 30));   // 30 -> 29
    TEST_ASSERT_EQUAL_UINT8(55, prevFavorite(0, 0));    // 0 -> wraps to 55 (Fermat Spiral, last playable)
    TEST_ASSERT_EQUAL_UINT8(54, prevFavorite(0, 55));   // 55 -> 54
    TEST_ASSERT_EQUAL_UINT8(48, prevFavorite(0, 49));   // 49 (first ported) -> 48 (GIFs)
    TEST_ASSERT_EQUAL_UINT8(47, prevFavorite(0, 48));   // 48 (GIFs) -> 47 (Greetz)
    TEST_ASSERT_EQUAL_UINT8(37, prevFavorite(0, 38));   // 38 (Bloom) -> 37 (Garden Eels): contiguous now
}
void test_prev_skips_to_set_bit() {
    uint32_t mask = (1u << 4) | (1u << 17);
    TEST_ASSERT_EQUAL_UINT8(17, prevFavorite(mask, 0));   // backward from 0 wraps to 17
    TEST_ASSERT_EQUAL_UINT8(4,  prevFavorite(mask, 17));  // 17 -> 4
    TEST_ASSERT_EQUAL_UINT8(17, prevFavorite(mask, 4));   // 4 -> wraps back to 17
}
void test_prev_single_favorite_returns_itself() {
    uint32_t mask = (1u << 9);
    TEST_ASSERT_EQUAL_UINT8(9, prevFavorite(mask, 9));  // only favorite -> stays
    TEST_ASSERT_EQUAL_UINT8(9, prevFavorite(mask, 3));
}
void test_step_walks_n_detents() {
    TEST_ASSERT_EQUAL_UINT8(5,  stepFavorite(0, 0, 5));    // 5 detents CW from 0
    TEST_ASSERT_EQUAL_UINT8(55, stepFavorite(0, 0, -1));   // 1 detent CCW from 0 wraps to 55 (Fermat Spiral, last playable)
    TEST_ASSERT_EQUAL_UINT8(7,  stepFavorite(0, 7, 0));    // no movement -> unchanged
    uint64_t mask = (1ull << 4) | (1ull << 17);
    TEST_ASSERT_EQUAL_UINT8(17, stepFavorite(mask, 0, 2));   // 0 -> 4 -> 17
    TEST_ASSERT_EQUAL_UINT8(4,  stepFavorite(mask, 0, -2));  // 0 -> 17 -> 4 (backward)
}
void test_step_clamps_absurd_delta() {
    // A delta bigger than the id space can only wrap; clamping keeps the loop bounded and
    // keeps INT_MIN from overflowing when negated. With the reserved holes, ANIM_COUNT steps
    // is no longer exactly one wrap -- assert the clamp itself, not a wrap identity.
    TEST_ASSERT_EQUAL_UINT8(stepFavorite(0, 0, ANIM_COUNT),  stepFavorite(0, 0, 1000));
    TEST_ASSERT_EQUAL_UINT8(stepFavorite(0, 0, -ANIM_COUNT), stepFavorite(0, 0, -1000));
}

void test_anim_name_known_ids() {
  TEST_ASSERT_EQUAL_STRING("Radiate", animName(0));
  TEST_ASSERT_EQUAL_STRING("Matrix", animName(EYE_COUNT));
  TEST_ASSERT_EQUAL_STRING("GIFs", animName(GIF_ID));
}

// The id space has holes (42..44 are debug, and ANIM_COUNT is a BOUND not a count), so an
// unknown id must not index past the registry.
void test_anim_name_unknown_id_is_safe() {
  TEST_ASSERT_EQUAL_STRING("?", animName(200));
}

// Every playable must have a name -- a carousel entry rendering as "?" would be a silent
// catalog gap.
void test_every_playable_has_a_name() {
  for (int id = 0; id < ANIM_COUNT; id++) {
    if (!isPlayableId(id)) continue;
    TEST_ASSERT_NOT_EQUAL(0, strcmp("?", animName((uint8_t)id)));
  }
}

// carouselList's own contract: "exactly the ids nextFavorite() walks, ascending." Walk the
// produced list with nextFavorite and assert it lands on the next list entry every time,
// wrapping at the end -- pins the invariant instead of just restating the loop.
void test_carousel_list_matches_next_favorite_walk() {
  uint8_t list[64];
  const uint64_t masks[] = { 0, (1ull << 0) | (1ull << 5) | (1ull << GIF_ID), PLAYABLE_MASK };
  for (uint64_t m : masks) {
    int n = carouselList(m, list, 64);
    TEST_ASSERT_TRUE(n > 0);
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(isPlayableId(list[i]));
      if (i) TEST_ASSERT_TRUE(list[i] > list[i - 1]);          // ascending
      TEST_ASSERT_EQUAL_UINT8(list[(i + 1) % n], nextFavorite(m, list[i]));
    }
  }
}

// A mask naming only non-playable ids must fall back to every playable, exactly as nextFavorite does.
void test_carousel_list_ignores_non_playable_mask_bits() {
  uint8_t list[64];
  int n = carouselList(1ull << DEBUG_ID, list, 64);
  TEST_ASSERT_EQUAL_INT(PLAYABLE_ENTRY_COUNT, n);
}

void test_resolve_startup_modes_and_clamp() {
    TEST_ASSERT_EQUAL_UINT8(7,  resolveStartupId("fixed",  7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(15, resolveStartupId("random", 7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(3,  resolveStartupId("resume", 7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(38, resolveStartupId("fixed", 38, 3, 15));  // Bloom's new id valid
    TEST_ASSERT_EQUAL_UINT8(45, resolveStartupId("fixed", 45, 3, 15));  // Swirl playable
    TEST_ASSERT_EQUAL_UINT8(46, resolveStartupId("fixed", 46, 3, 15));  // treatcat playable
    TEST_ASSERT_EQUAL_UINT8(47, resolveStartupId("fixed", 47, 3, 15));  // Greetz playable
    TEST_ASSERT_EQUAL_UINT8(48, resolveStartupId("fixed", 48, 3, 15));  // GIFs playable
    TEST_ASSERT_EQUAL_UINT8(49, resolveStartupId("fixed", 49, 3, 15));  // first ported lab effect playable
    TEST_ASSERT_EQUAL_UINT8(55, resolveStartupId("fixed", 55, 3, 15));  // Fermat Spiral (last playable)
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", 56, 3, 15));  // unknown id (past the atlas block) -> 0
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", DEBUG_ID, 3, 15));  // debug never boots
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", 99, 3, 15));  // out-of-range -> 0
}

// `resume` with nothing to resume falls back to the fixed startup id, not to 0. A fresh unit has a
// blank NVS, so resumeIdLoad() hands back 0xFF and the board would otherwise come up on eye 0 --
// which is what made every new unit look identical out of the box.
void test_resume_with_nothing_stored_falls_back_to_fixed() {
    TEST_ASSERT_EQUAL_UINT8(47, resolveStartupId("resume", 47, 0xFF, 15));  // blank NVS -> startupId
    TEST_ASSERT_EQUAL_UINT8(47, resolveStartupId("resume", 47, DEBUG_ID, 15));  // debug id stored -> startupId
    TEST_ASSERT_EQUAL_UINT8(47, resolveStartupId("resume", 47, 56, 15));    // past the atlas block -> startupId
    TEST_ASSERT_EQUAL_UINT8(3,  resolveStartupId("resume", 47, 3, 15));     // a real stored pick still wins
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("resume", 47, 0, 15));     // eye 0 is a legitimate resume
    // Both unusable -> 0. Nothing else is safe to land on.
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("resume", DEBUG_ID, 0xFF, 15));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_next_of_all_when_mask_empty);
    RUN_TEST(test_next_skips_to_set_bit);
    RUN_TEST(test_single_favorite_returns_itself);
    RUN_TEST(test_prev_of_all_when_mask_empty);
    RUN_TEST(test_prev_skips_to_set_bit);
    RUN_TEST(test_prev_single_favorite_returns_itself);
    RUN_TEST(test_step_walks_n_detents);
    RUN_TEST(test_step_clamps_absurd_delta);
    RUN_TEST(test_anim_name_known_ids);
    RUN_TEST(test_anim_name_unknown_id_is_safe);
    RUN_TEST(test_every_playable_has_a_name);
    RUN_TEST(test_carousel_list_matches_next_favorite_walk);
    RUN_TEST(test_carousel_list_ignores_non_playable_mask_bits);
    RUN_TEST(test_resolve_startup_modes_and_clamp);
    RUN_TEST(test_resume_with_nothing_stored_falls_back_to_fixed);
    return UNITY_END();
}
