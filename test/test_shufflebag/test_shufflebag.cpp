#include <unity.h>
#include "shufflebag.h"

// deterministic LCG so the test is reproducible (no Arduino random())
static uint32_t s_state = 0x12345678u;
static uint32_t testRng(uint32_t n) { s_state = s_state * 1103515245u + 12345u; return (s_state >> 16) % n; }

void test_drain_covers_all_no_repeat() {
  ShuffleBag b{}; const int N = 10; bool seen[N] = {false};
  for (int i = 0; i < N; i++) { int idx = shufflebagPick(b, N, testRng);
    TEST_ASSERT_TRUE(idx >= 0 && idx < N); TEST_ASSERT_FALSE(seen[idx]); seen[idx] = true; }
  for (int i = 0; i < N; i++) TEST_ASSERT_TRUE(seen[i]);   // every index handed out exactly once
}

void test_no_immediate_repeat_across_refill() {
  ShuffleBag b{}; const int N = 8; int last = -1;
  for (int round = 0; round < 6; round++)
    for (int i = 0; i < N; i++) { int idx = shufflebagPick(b, N, testRng);
      TEST_ASSERT_NOT_EQUAL(last, idx); last = idx; }   // holds within a drain AND across refills
}

void setUp() {} void tearDown() {}
int main() { UNITY_BEGIN();
  RUN_TEST(test_drain_covers_all_no_repeat);
  RUN_TEST(test_no_immediate_repeat_across_refill);
  return UNITY_END(); }
