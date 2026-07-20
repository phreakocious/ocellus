#include <unity.h>
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
    TEST_ASSERT_EQUAL_UINT8(0,  nextFavorite(0, 45));   // 45 (last) -> wraps to 0
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
    TEST_ASSERT_EQUAL_UINT8(45, prevFavorite(0, 0));    // 0 -> wraps to 45 (Swirl, last playable)
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
    TEST_ASSERT_EQUAL_UINT8(45, stepFavorite(0, 0, -1));   // 1 detent CCW from 0 wraps to 45 (Swirl)
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
void test_resolve_startup_modes_and_clamp() {
    TEST_ASSERT_EQUAL_UINT8(7,  resolveStartupId("fixed",  7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(15, resolveStartupId("random", 7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(3,  resolveStartupId("resume", 7, 3, 15));
    TEST_ASSERT_EQUAL_UINT8(38, resolveStartupId("fixed", 38, 3, 15));  // Bloom's new id valid
    TEST_ASSERT_EQUAL_UINT8(45, resolveStartupId("fixed", 45, 3, 15));  // Swirl (above debug) is playable
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", 46, 3, 15));  // unknown id -> 0
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", DEBUG_ID, 3, 15));  // debug never boots
    TEST_ASSERT_EQUAL_UINT8(0,  resolveStartupId("fixed", 99, 3, 15));  // out-of-range -> 0
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
    RUN_TEST(test_resolve_startup_modes_and_clamp);
    return UNITY_END();
}
