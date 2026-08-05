#include <unity.h>
#include "../../pet_state.h"

void setUp() {}
void tearDown() {}

// helper: drain until a predicate holds, bounded so a broken model can't hang the suite
template <class F> static void run_until(PetState& p, bool care, F done) {
  int guard = 0;
  while (!done(p) && guard++ < 500000) p.tick(33.0f, care);
}

void test_float_decay_below_one_point_per_frame() {
  PetState p; p.reset(0);
  float before = p.fullness();
  p.tick(33.0f, true);                    // ~0.0013 pt: an int stat would truncate to 0
  TEST_ASSERT_TRUE(p.fullness() < before);
  TEST_ASSERT_TRUE(before - p.fullness() < 1.0f);
}

void test_ambient_never_decays() {
  PetState p; p.reset(0);
  for (int i = 0; i < 500000; i++) p.tick(33.0f, false);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.fullness());
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.energy());
  TEST_ASSERT_FALSE(p.asleep());
  TEST_ASSERT_EQUAL_INT(PET_CONTENT, p.mood(false));
}

void test_feed_only_when_hungry_and_one_feed_clears() {
  PetState p; p.reset(0);
  TEST_ASSERT_FALSE(p.feed());                          // full cat: tap is a pet, not a feed
  TEST_ASSERT_EQUAL_UINT32(0, p.treats());
  run_until(p, true, [](PetState& q){ return q.hungry(); });
  TEST_ASSERT_TRUE(p.hungry());
  TEST_ASSERT_TRUE(p.feed());                           // ate
  TEST_ASSERT_EQUAL_UINT32(1, p.treats());
  TEST_ASSERT_FALSE(p.hungry());                        // one feed from the floor cleared hunger
  TEST_ASSERT_FALSE(p.feed());                          // now content: spam-tap is a no-write pet
  TEST_ASSERT_EQUAL_UINT32(1, p.treats());
}

void test_treats_uint32_no_wrap() {
  PetState p; p.reset(255);
  run_until(p, true, [](PetState& q){ return q.hungry(); });
  TEST_ASSERT_TRUE(p.feed());
  TEST_ASSERT_EQUAL_UINT32(256, p.treats());            // not 0 (would be an 8-bit wrap)
}

void test_sleep_latch_enters_low_holds_to_high() {
  PetState p; p.reset(0);
  run_until(p, true, [](PetState& q){ return q.asleep(); });
  TEST_ASSERT_TRUE(p.asleep());
  TEST_ASSERT_TRUE(p.energy() <= PET_SLEEP_LO + 1.0f);  // entered near LO, not earlier
  p.tick(33.0f, true);
  TEST_ASSERT_TRUE(p.asleep());                         // recovering just above LO: does NOT wake early
  run_until(p, true, [](PetState& q){ return !q.asleep(); });
  TEST_ASSERT_TRUE(p.energy() >= PET_SLEEP_HI - 1.0f);  // woke at HI, not earlier
}

void test_force_wake_beats_latch_through_next_frame() {
  PetState p; p.reset(0);
  run_until(p, true, [](PetState& q){ return q.asleep(); });
  p.forceWake();
  TEST_ASSERT_FALSE(p.asleep());
  TEST_ASSERT_TRUE(p.energy() > PET_SLEEP_LO);
  p.tick(33.0f, true);                                  // the very next update
  TEST_ASSERT_FALSE(p.asleep());                        // cannot re-sleep this frame
  TEST_ASSERT_TRUE(p.energy() > PET_SLEEP_LO);
}

void test_mood_priority() {
  PetState p; p.reset(0);
  TEST_ASSERT_EQUAL_INT(PET_CONTENT, p.mood(true));     // full & awake
  run_until(p, true, [](PetState& q){ return q.hungry(); });
  TEST_ASSERT_TRUE(p.asleep());                         // energy naps before fullness bottoms out
  TEST_ASSERT_EQUAL_INT(PET_SLEEPY, p.mood(true));      // sleep outranks hunger
  p.forceWake();
  TEST_ASSERT_EQUAL_INT(PET_NEEDY, p.mood(true));       // awake + hungry -> needy
}

// debugSet is the bench hook behind {"cmd":"petsim"}. The stats it takes are trivial; the latch it
// has to re-derive is not -- an energy under SLEEP_LO with a stale "awake" latch is a cat that is
// asleep by every predicate while still drawing an awake pose.
void test_debug_set_rederives_the_sleep_latch() {
  PetState p;
  p.reset(0);
  TEST_ASSERT_FALSE(p.asleep());

  p.debugSet(50.0f, PET_SLEEP_LO - 1.0f);          // below the low mark -> latched asleep
  TEST_ASSERT_TRUE(p.asleep());
  TEST_ASSERT_EQUAL_FLOAT(50.0f, p.fullness());

  p.debugSet(50.0f, PET_SLEEP_HI + 1.0f);          // at/above the high mark -> awake again
  TEST_ASSERT_FALSE(p.asleep());

  // Inside the hysteresis band the latch is history, not a function of energy: it must be left
  // exactly as it was, in BOTH directions.
  float mid = (PET_SLEEP_LO + PET_SLEEP_HI) * 0.5f;
  p.debugSet(50.0f, mid);
  TEST_ASSERT_FALSE(p.asleep());                   // came from awake -> stays awake
  p.debugSet(50.0f, PET_SLEEP_LO - 1.0f);
  p.debugSet(50.0f, mid);
  TEST_ASSERT_TRUE(p.asleep());                    // came from asleep -> stays asleep

  p.debugSet(-999.0f, 999.0f);                     // clamped to the floors/ceiling, never wild
  TEST_ASSERT_EQUAL_FLOAT(PET_FULL_FLOOR, p.fullness());
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.energy());
}

// petCmdParse is the parse half of {"cmd":"pet"}/petsim/pettap (treatcat.cpp applies the result).
// The anchoring and malformed-line handling live here because the wrapper TU is Arduino-bound.

// The pre-anchor matcher hit "pet" ANYWHERE in the line, so a config set whose name (or a gif
// clip name, palette name...) was exactly pet/petsim/pettap was hijacked before the JSON handler
// and that config could never save. Anchored = compact-JSON cmd key only.
void test_petcmd_anchored_to_cmd_key() {
  PetCmd c;
  TEST_ASSERT_FALSE(petCmdParse("{\"cmd\":\"set\",\"config\":{\"name\":\"pet\"}}", c));
  TEST_ASSERT_FALSE(petCmdParse("{\"cmd\":\"set\",\"config\":{\"name\":\"petsim\"}}", c));
  TEST_ASSERT_FALSE(petCmdParse("{\"cmd\":\"set\",\"config\":{\"name\":\"pettap\"}}", c));
  TEST_ASSERT_FALSE(petCmdParse("{\"cmd\":\"get\"}", c));
  TEST_ASSERT_FALSE(petCmdParse("", c));
}

void test_petcmd_plain_pet_is_read_only() {
  PetCmd c;
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"pet\"}", c));
  TEST_ASSERT_FALSE(c.sim);
  TEST_ASSERT_FALSE(c.tap);
  TEST_ASSERT_FALSE(c.hasFull); TEST_ASSERT_FALSE(c.hasEn); TEST_ASSERT_FALSE(c.hasTreats);
}

void test_petcmd_pettap() {
  PetCmd c;
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"pettap\"}", c));
  TEST_ASSERT_TRUE(c.tap);
  TEST_ASSERT_FALSE(c.sim);     // "cmd":"pet" must not match inside "cmd":"pettap"
}

void test_petcmd_petsim_keys_all_optional() {
  PetCmd c;
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\":5,\"en\":9,\"treats\":4}", c));
  TEST_ASSERT_TRUE(c.sim); TEST_ASSERT_FALSE(c.tap);
  TEST_ASSERT_TRUE(c.hasFull);   TEST_ASSERT_EQUAL_FLOAT(5.0f, c.full);
  TEST_ASSERT_TRUE(c.hasEn);     TEST_ASSERT_EQUAL_FLOAT(9.0f, c.en);
  TEST_ASSERT_TRUE(c.hasTreats); TEST_ASSERT_EQUAL_UINT32(4, c.treats);

  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\":30.5}", c));
  TEST_ASSERT_TRUE(c.hasFull);   TEST_ASSERT_EQUAL_FLOAT(30.5f, c.full);
  TEST_ASSERT_FALSE(c.hasEn); TEST_ASSERT_FALSE(c.hasTreats);

  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"en\":15}", c));
  TEST_ASSERT_FALSE(c.hasFull);
  TEST_ASSERT_TRUE(c.hasEn);     TEST_ASSERT_EQUAL_FLOAT(15.0f, c.en);

  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\"}", c));    // bare petsim: reply-only, no force
  TEST_ASSERT_TRUE(c.sim);
  TEST_ASSERT_FALSE(c.hasFull); TEST_ASSERT_FALSE(c.hasEn); TEST_ASSERT_FALSE(c.hasTreats);
}

// The pre-extraction code did atof(strchr(pf, ':') + 1): a key with no ':' after it (hand-typed,
// or the tail RX-dropped during the 14 ms flush) made strchr return NULL -> read from address 1
// -> LoadProhibited panic and reboot. Every malformation here must parse as key-absent.
void test_petcmd_missing_colon_is_key_absent_not_a_crash() {
  PetCmd c;
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\"}", c));
  TEST_ASSERT_TRUE(c.sim);
  TEST_ASSERT_FALSE(c.hasFull);
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\"", c));    // truncated at the key
  TEST_ASSERT_FALSE(c.hasFull);
  // a later key's ':' must not be read as this key's value (the old strchr scanned to end of line)
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\",\"en\":50}", c));
  TEST_ASSERT_FALSE(c.hasFull);
  TEST_ASSERT_TRUE(c.hasEn); TEST_ASSERT_EQUAL_FLOAT(50.0f, c.en);
  // spaced form falls through to key-absent, same convention as the cmd anchor
  TEST_ASSERT_TRUE(petCmdParse("{\"cmd\":\"petsim\",\"full\" : 5}", c));
  TEST_ASSERT_FALSE(c.hasFull);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_float_decay_below_one_point_per_frame);
  RUN_TEST(test_ambient_never_decays);
  RUN_TEST(test_feed_only_when_hungry_and_one_feed_clears);
  RUN_TEST(test_treats_uint32_no_wrap);
  RUN_TEST(test_sleep_latch_enters_low_holds_to_high);
  RUN_TEST(test_force_wake_beats_latch_through_next_frame);
  RUN_TEST(test_mood_priority);
  RUN_TEST(test_debug_set_rederives_the_sleep_latch);
  RUN_TEST(test_petcmd_anchored_to_cmd_key);
  RUN_TEST(test_petcmd_plain_pet_is_read_only);
  RUN_TEST(test_petcmd_pettap);
  RUN_TEST(test_petcmd_petsim_keys_all_optional);
  RUN_TEST(test_petcmd_missing_colon_is_key_absent_not_a_crash);
  return UNITY_END();
}
