#include <unity.h>
#include <initializer_list>
#include "../../cat_choreo.h"

void setUp() {}
void tearDown() {}

// Scripted RNG: tests enqueue the exact rolls a scenario consumes, so every branch is
// reachable deterministically. rnd(n) must return 0..n-1; % keeps a sloppy script legal.
static uint32_t s_script[16];
static int s_len = 0, s_at = 0;
static void script(std::initializer_list<uint32_t> v) {
  s_len = 0; s_at = 0;
  for (uint32_t x : v) s_script[s_len++] = x;
}
static uint32_t srnd(uint32_t n) {
  TEST_ASSERT_TRUE_MESSAGE(s_at < s_len, "test consumed more rolls than scripted");
  return s_script[s_at++] % n;
}

// These tests LOCK the pre-extraction treatcat.cpp behavior: scene entry fires the first
// fidget immediately (the quirk clock starts at 0), mood edges are choreography (sleep gets
// an entry gesture, waking gets the bridge + a settle window), and the tiny pools keep their
// walk-forward no-immediate-repeat rule. Timing constants are asserted through behavior, not
// by reading the constants back.

// Re-arm scripts everywhere: a content fire consumes pick rolls, then the bout roll (opener
// only), then the gap roll -- see rearm(). An opener rolling bout=0 takes the long calm
// (10-26 s); with a bout pending the gap is short (3.5-6.5 s).

void test_scene_entry_fires_first_fidget_immediately() {
  CatChoreo c; c.enterScene();
  script({3, 0, 0});                           // start slot 3 = ITCH; bout roll 0 -> calm; gap 0
  TEST_ASSERT_EQUAL_INT(CA_ITCH, c.next(PET_CONTENT, 1000, false, srnd));
  // gate re-armed a long calm out: one ms early is quiet, on time fires again
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000 + 9999, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000 + 10000, false, srnd));
}

void test_active_reaction_holds_the_quirk_gate() {
  CatChoreo c; c.enterScene();
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000, true, srnd));
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000, false, srnd));
}

void test_no_immediate_repeat_walks_past_both_kneading_slots() {
  CatChoreo c; c.enterScene();
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000, false, srnd));
  // start slot 1 is the duplicate KNEADING: the walk must pass BOTH copies to TAIL_HUG
  script({1, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.next(PET_CONTENT, 20000, false, srnd));
}

void test_a_bout_clusters_gestures_then_a_long_calm() {
  CatChoreo c; c.enterScene();
  script({0, 2, 0});                           // opener rolls a bout of 2: short gap follows
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000, false, srnd));
  script({0, 0});                              // follower: no bout roll, just pick + gap
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000 + 3499, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.next(PET_CONTENT, 1000 + 3500, false, srnd));
  script({0, 0});                              // last follower: bout exhausts, calm gap
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000 + 7000, false, srnd));
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000 + 7000 + 9999, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.next(PET_CONTENT, 1000 + 7000 + 10000, false, srnd));
}

void test_calm_span_roll_widens_the_gap() {
  CatChoreo c; c.enterScene();
  script({0, 0, 15999});                       // bout 0, max calm roll: re-arm 10000+15999 out
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 1000, false, srnd));
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000 + 25998, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.next(PET_CONTENT, 1000 + 25999, false, srnd));
}

void test_sleep_edge_returns_entry_gesture_even_mid_reaction() {
  CatChoreo c; c.enterScene();
  script({0});                                  // slot 0 = YAWNING
  TEST_ASSERT_EQUAL_INT(CA_YAWNING, c.next(PET_SLEEPY, 1000, true, srnd));
  // no repeat on the next doze: slot 0 again must walk to NODDING
  script({0});
  TEST_ASSERT_EQUAL_INT(CA_WAKING, c.next(PET_CONTENT, 5000, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_NODDING, c.next(PET_SLEEPY, 9000, false, srnd));
}

void test_wake_returns_the_bridge_then_settles_before_fidgeting() {
  CatChoreo c; c.enterScene();
  script({0});
  TEST_ASSERT_EQUAL_INT(CA_YAWNING, c.next(PET_SLEEPY, 1000, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_WAKING, c.next(PET_CONTENT, 60000, false, srnd));
  // freshly woken: 2500 re-arm + 4800 settle before the first fidget may fire
  script({65, 0, 0, 0});                        // 65 declines the post-nap grooming bias
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 60000 + 7299, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
}

void test_wake_into_needy_delays_the_first_beg() {
  CatChoreo c; c.enterScene();
  script({0});
  TEST_ASSERT_EQUAL_INT(CA_YAWNING, c.next(PET_SLEEPY, 1000, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_WAKING, c.next(PET_NEEDY, 60000, false, srnd));
  // 1800 re-arm + 4800 settle
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_NEEDY, 60000 + 6599, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_BEGGING, c.next(PET_NEEDY, 60000 + 6600, false, srnd));
}

void test_needy_cycle_reaches_sniff_and_rearms() {
  CatChoreo c; c.enterScene();
  // content -> needy edge re-arms the beg clock 1800 out, no waking bridge
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_NEEDY, 1000, false, srnd));
  script({2, 0});                               // slot 2 = SNIFF
  TEST_ASSERT_EQUAL_INT(CA_SNIFF, c.next(PET_NEEDY, 1000 + 1800, false, srnd));
  // re-armed BEG_MIN_MS out from the pick
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_NEEDY, 2800 + 4499, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_BEGGING, c.next(PET_NEEDY, 2800 + 4500, false, srnd));
}

void test_each_mood_pool_reaches_its_new_gesture() {
  CatChoreo content; content.enterScene();
  script({10, 0, 0});                          // appended content slot = HEAD_BUNT
  TEST_ASSERT_EQUAL_INT(CA_HEAD_BUNT, content.next(PET_CONTENT, 1000, false, srnd));

  CatChoreo needy; needy.enterScene();
  TEST_ASSERT_EQUAL_INT(CA_COUNT, needy.next(PET_NEEDY, 1000, false, srnd));
  script({3, 0});                              // appended needy slot = PROTEST
  TEST_ASSERT_EQUAL_INT(CA_PROTEST, needy.next(PET_NEEDY, 2800, false, srnd));

  CatChoreo sleepy; sleepy.enterScene();
  script({2});                                 // appended sleep-entry slot = CURL_UP
  TEST_ASSERT_EQUAL_INT(CA_CURL_UP, sleepy.next(PET_SLEEPY, 1000, false, srnd));

  CatChoreo play; play.enterScene();
  script({11, 0, 0});                          // appended content slot = POUNCE
  TEST_ASSERT_EQUAL_INT(CA_POUNCE, play.next(PET_CONTENT, 1000, false, srnd));
}

// A cat washes its face after a meal -- the iconic post-meal beat, so it is DETERMINISTIC,
// not a pool roll. The feed reaction (treatcat's CA_LICKING, 1900 ms) plays first; the wash
// waits out the lick plus a settle breath, and while it is pending the random fidget gate
// holds so an unrelated gesture cannot wedge between meal and wash.

void test_feed_schedules_a_face_wash() {
  CatChoreo c; c.enterScene();
  c.onFeed(1000);
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1000 + 4499, false, srnd));
  script({0, 0});                               // re-arm rolls only: the pick itself is fixed
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 1000 + 4500, false, srnd));
  // the wash counts as the last gesture: the next random pick must walk past a repeat
  script({6, 0, 0});                            // start slot 6 = GROOM_FACE
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FORELEG, c.next(PET_CONTENT, 1000 + 4500 + 10000, false, srnd));
}

void test_pending_wash_holds_the_random_fidget() {
  CatChoreo c; c.enterScene();
  c.onFeed(1000);
  // quirk clock is at 0, so without the hold a random gesture would fire here
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 2000, false, srnd));
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 5500, false, srnd));
}

void test_wash_waits_out_an_active_reaction() {
  CatChoreo c; c.enterScene();
  c.onFeed(1000);
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 5500, true, srnd));
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 6000, false, srnd));
}

void test_doze_cancels_the_pending_wash() {
  CatChoreo c; c.enterScene();
  c.onFeed(1000);
  script({0});
  TEST_ASSERT_EQUAL_INT(CA_YAWNING, c.next(PET_SLEEPY, 2000, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_WAKING, c.next(PET_CONTENT, 60000, false, srnd));
  // deadline long past, but the doze dropped it: the settle expires into a NORMAL pool roll
  // (65 declines the post-nap grooming bias -- the point here is no FORCED wash)
  script({65, 0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
}

// Grooming bouts: real cats rarely stop at one pass. A finished FACE wash sometimes continues
// down a foreleg; a finished ITCH is often followed by licking the scratched spot (displacement
// grooming). Chains are one short beat later -- a pause, then the bout continues -- and no chain
// TARGET is itself a chain TRIGGER, so a bout is one hop by construction. Roll order in
// onGestureEnd is fixed: percent, sub-pick (itch only), delay span.

void test_face_wash_can_chain_into_a_foreleg_wash() {
  CatChoreo c; c.enterScene();
  script({6, 0, 0});                            // pool pick: slot 6 = GROOM_FACE
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 1000, false, srnd));
  script({44, 0});                              // 44 < 45 chains; delay roll 0 -> +900 ms
  c.onGestureEnd(CA_GROOM_FACE, 6200, srnd);
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 6200 + 899, false, srnd));
  script({0, 0});                               // re-arm rolls at the chain fire
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FORELEG, c.next(PET_CONTENT, 6200 + 900, false, srnd));
}

void test_chain_roll_can_decline() {
  CatChoreo c; c.enterScene();
  script({45});                                 // 45 >= 45: no chain scheduled
  c.onGestureEnd(CA_GROOM_FACE, 6200, srnd);
  // nothing held: the random gate fires normally (quirk clock still at 0 here)
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 6300, false, srnd));
}

void test_itch_chains_into_licking_the_spot() {
  CatChoreo c; c.enterScene();
  script({39, 0, 699});                         // 39 < 40 chains; sub-pick 0 = LICKING; max delay
  c.onGestureEnd(CA_ITCH, 3000, srnd);
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 3000 + 1598, false, srnd));
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_LICKING, c.next(PET_CONTENT, 3000 + 1599, false, srnd));
}

void test_itch_can_chain_into_a_belly_wash() {
  CatChoreo c; c.enterScene();
  script({0, 2, 0});                            // sub-pick 2 = GROOM_BELLY
  c.onGestureEnd(CA_ITCH, 3000, srnd);
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_GROOM_BELLY, c.next(PET_CONTENT, 3900, false, srnd));
}

void test_mood_edge_drops_the_pending_chain() {
  CatChoreo c; c.enterScene();
  script({44, 0});
  c.onGestureEnd(CA_GROOM_FACE, 1000, srnd);
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_NEEDY, 1200, false, srnd));   // got hungry mid-bout
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1500, false, srnd)); // fed elsewhere; edge re-arms
  // chain deadline long past yet nothing forced: the next gesture is a plain pool roll
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000, false, srnd));
}

void test_feed_drops_the_pending_chain_for_the_wash() {
  CatChoreo c; c.enterScene();
  script({44, 0});
  c.onGestureEnd(CA_GROOM_FACE, 1000, srnd);
  c.onFeed(1100);                               // meal interrupts the bout; the wash follows anyway
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 1900, false, srnd));
  script({0, 0});
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 1100 + 4500, false, srnd));
}

// Post-nap grooming: cats groom when they rise. The WAKING bridge already yawns and
// stretches, so the bias goes to the washes: the first pool pick after a wake rolls
// 40% GROOM_FORELEG / 25% GROOM_FACE before falling through to the normal walk. The
// bias is single-use and expires 20 s after waking -- a fidget half a minute later
// has lost the connection to the nap.

static void wake_at(CatChoreo& c, uint32_t sleepT, uint32_t wakeT) {
  script({0});
  TEST_ASSERT_EQUAL_INT(CA_YAWNING, c.next(PET_SLEEPY, sleepT, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_WAKING, c.next(PET_CONTENT, wakeT, false, srnd));
}

void test_first_fidget_after_a_nap_biases_toward_a_foreleg_wash() {
  CatChoreo c; c.enterScene();
  wake_at(c, 1000, 60000);
  script({39, 0, 0});                           // 39 < 40: the single bias roll decides
  TEST_ASSERT_EQUAL_INT(CA_COUNT, c.next(PET_CONTENT, 60000 + 7299, false, srnd));
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FORELEG, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
}

void test_nap_bias_can_pick_the_face_wash() {
  CatChoreo c; c.enterScene();
  wake_at(c, 1000, 60000);
  script({64, 0, 0});                           // 40..64: face wash
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FACE, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
}

void test_nap_bias_can_decline_into_the_plain_pool() {
  CatChoreo c; c.enterScene();
  wake_at(c, 1000, 60000);
  script({65, 0, 0, 0});                        // >= 65 falls through to the normal walk
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
}

void test_nap_bias_is_single_use() {
  CatChoreo c; c.enterScene();
  wake_at(c, 1000, 60000);
  script({39, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_GROOM_FORELEG, c.next(PET_CONTENT, 60000 + 7300, false, srnd));
  // next pick is a plain walk, no bias roll consumed
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000 + 7300 + 10000, false, srnd));
}

void test_nap_bias_expires() {
  CatChoreo c; c.enterScene();
  wake_at(c, 1000, 60000);
  // first pick lands 21 s after the wake: plain walk, no bias roll
  script({0, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_KNEADING, c.next(PET_CONTENT, 60000 + 21000, false, srnd));
}

// Petting a content cat: mostly the stretch, with four quieter affection answers including the
// new head bunt. The pick counts as the last content gesture so the pool cannot immediately
// repeat it.

void test_petting_can_reach_every_affection_response() {
  CatChoreo c; c.enterScene();
  script({39});
  TEST_ASSERT_EQUAL_INT(CA_STRETCHING, c.onPet(srnd));
  script({40});
  TEST_ASSERT_EQUAL_INT(CA_SLOWBLINK, c.onPet(srnd));
  script({59});
  TEST_ASSERT_EQUAL_INT(CA_SLOWBLINK, c.onPet(srnd));
  script({60});
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.onPet(srnd));
  script({75});
  TEST_ASSERT_EQUAL_INT(CA_ADORE, c.onPet(srnd));
  script({90});
  TEST_ASSERT_EQUAL_INT(CA_HEAD_BUNT, c.onPet(srnd));
}

void test_pet_response_counts_as_last_gesture() {
  CatChoreo c; c.enterScene();
  script({70});
  TEST_ASSERT_EQUAL_INT(CA_TAIL_HUG, c.onPet(srnd));
  // pool walk starting on TAIL_HUG's slot must step past it
  script({2, 0, 0});
  TEST_ASSERT_EQUAL_INT(CA_ITCH, c.next(PET_CONTENT, 50000, false, srnd));
}

// Dream twitches: a sleeping cat's whiskers flick now and then. The choreo owns only the
// CLOCK (armed on the first sleepy query, 6-18 s between flicks, disarmed by waking); the
// glue maps a true return onto the existing twitchT render state.

void test_dream_twitches_fire_only_while_sleeping() {
  CatChoreo c; c.enterScene();
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_CONTENT, 1000, srnd));   // awake: never, no rolls
  script({0});                                  // sleepy: arms the clock 6000+roll out
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 2000, srnd));
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 2000 + 5999, srnd));
  script({0});                                  // fire consumes the NEXT arming roll
  TEST_ASSERT_TRUE(c.dreamTwitch(PET_SLEEPY, 2000 + 6000, srnd));
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 2000 + 6001, srnd));
}

void test_waking_disarms_the_dream_twitch_clock() {
  CatChoreo c; c.enterScene();
  script({0});
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 2000, srnd));
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_CONTENT, 9000, srnd));   // woke first: no twitch
  // dozing again re-arms from scratch rather than firing the stale deadline
  script({11999});                              // max roll: 6000+11999 out
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 10000, srnd));
  TEST_ASSERT_FALSE(c.dreamTwitch(PET_SLEEPY, 10000 + 17998, srnd));
  script({0});
  TEST_ASSERT_TRUE(c.dreamTwitch(PET_SLEEPY, 10000 + 17999, srnd));
}

// Soak: an hour of content idle under a real PRNG, gestures finishing on their CAT_POSE
// durations and rolling their bout chains. Guards the EMERGENT rhythm the scripted scenarios
// cannot: bouts + chains together must neither spam (a cat that seizes constantly) nor
// starve (a cat that goes dead). Bounds are generous on purpose -- this is a tripwire for a
// broken interaction, not a tuning assertion.
static uint32_t s_lcg = 12345;
static uint32_t lcg(uint32_t n) { s_lcg = s_lcg * 1664525u + 1013904223u; return (s_lcg >> 8) % n; }

void test_soak_an_hour_of_idle_neither_spams_nor_starves() {
  CatChoreo c; c.enterScene();
  uint32_t busyUntil = 0, lastStart = 0, minSpacing = 0xffffffffu;
  int gestures = 0;
  CatAnim running = CA_COUNT;
  for (uint32_t now = 1; now < 3600000; now += 33) {
    if (running != CA_COUNT && now >= busyUntil) {
      c.onGestureEnd(running, now, lcg);
      running = CA_COUNT;
    }
    CatAnim a = c.next(PET_CONTENT, now, running != CA_COUNT, lcg);
    if (a == CA_COUNT) continue;
    if (gestures) { uint32_t sp = now - lastStart; if (sp < minSpacing) minSpacing = sp; }
    gestures++; lastStart = now;
    running = a; busyUntil = now + CAT_POSE[a].durMs;
  }
  TEST_ASSERT_TRUE_MESSAGE(gestures >= 120, "cat went dead: fewer than 120 gestures in an hour");
  TEST_ASSERT_TRUE_MESSAGE(gestures <= 500, "cat is spamming: more than 500 gestures in an hour");
  // shortest legal cycle: the 1600 ms itch plus the 900 ms minimum chain pause
  TEST_ASSERT_TRUE_MESSAGE(minSpacing >= 2400, "two gestures started nearly back-to-back");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scene_entry_fires_first_fidget_immediately);
  RUN_TEST(test_active_reaction_holds_the_quirk_gate);
  RUN_TEST(test_no_immediate_repeat_walks_past_both_kneading_slots);
  RUN_TEST(test_sleep_edge_returns_entry_gesture_even_mid_reaction);
  RUN_TEST(test_wake_returns_the_bridge_then_settles_before_fidgeting);
  RUN_TEST(test_wake_into_needy_delays_the_first_beg);
  RUN_TEST(test_needy_cycle_reaches_sniff_and_rearms);
  RUN_TEST(test_each_mood_pool_reaches_its_new_gesture);
  RUN_TEST(test_a_bout_clusters_gestures_then_a_long_calm);
  RUN_TEST(test_calm_span_roll_widens_the_gap);
  RUN_TEST(test_feed_schedules_a_face_wash);
  RUN_TEST(test_pending_wash_holds_the_random_fidget);
  RUN_TEST(test_wash_waits_out_an_active_reaction);
  RUN_TEST(test_doze_cancels_the_pending_wash);
  RUN_TEST(test_face_wash_can_chain_into_a_foreleg_wash);
  RUN_TEST(test_chain_roll_can_decline);
  RUN_TEST(test_itch_chains_into_licking_the_spot);
  RUN_TEST(test_itch_can_chain_into_a_belly_wash);
  RUN_TEST(test_mood_edge_drops_the_pending_chain);
  RUN_TEST(test_feed_drops_the_pending_chain_for_the_wash);
  RUN_TEST(test_first_fidget_after_a_nap_biases_toward_a_foreleg_wash);
  RUN_TEST(test_nap_bias_can_pick_the_face_wash);
  RUN_TEST(test_nap_bias_can_decline_into_the_plain_pool);
  RUN_TEST(test_nap_bias_is_single_use);
  RUN_TEST(test_nap_bias_expires);
  RUN_TEST(test_petting_can_reach_every_affection_response);
  RUN_TEST(test_pet_response_counts_as_last_gesture);
  RUN_TEST(test_dream_twitches_fire_only_while_sleeping);
  RUN_TEST(test_waking_disarms_the_dream_twitch_clock);
  RUN_TEST(test_soak_an_hour_of_idle_neither_spams_nor_starves);
  UNITY_END();
  return 0;
}
