#pragma once
#include <stdint.h>
#include "cat_proc.h"
#include "pet_state.h"

// Gesture choreography for the treatcat scene, extracted from treatcat.cpp so the behavior
// engine is natively testable: WHICH once-play gesture starts WHEN, per mood. Arduino-free and
// integer-ms only (the C3 has no FPU). The renderer stays in cat_proc.h; the tap/feed reactions
// stay in treatcat.cpp -- this owns only the clocks and the pools.
//
// rnd(n) -> uniform 0..n-1. Injected so the native suite can script every branch; the device
// passes a wrapper over Arduino random().
typedef uint32_t (*CatRnd)(uint32_t);

// Small mood-local bags. Starting at a random slot and walking past the last animation gives
// the tiny pools a no-immediate-repeat rule without spending three 104-byte ShuffleBags on
// them. Duplicate KNEADING keeps the content cat biased toward its quietest gesture; the three
// grooming variants are longer, rarer self-care beats. Duplicates are still skipped when
// kneading was the previous pick.
static const CatAnim CAT_CONTENT_GESTURES[] = {
  CA_KNEADING, CA_KNEADING, CA_TAIL_HUG, CA_ITCH, CA_STRETCHING, CA_SLOWBLINK,
  CA_GROOM_FACE, CA_GROOM_FORELEG, CA_GROOM_BELLY, CA_ADORE, CA_HEAD_BUNT, CA_POUNCE
};
static const CatAnim CAT_NEEDY_GESTURES[] = { CA_BEGGING, CA_PLEASE, CA_SNIFF, CA_PROTEST };
static const CatAnim CAT_SLEEP_ENTRIES[] = { CA_YAWNING, CA_NODDING, CA_CURL_UP };

// Idle rhythm: real cats act in BOUTS -- a cluster of two or three gestures a few seconds
// apart, then a long calm. The old uniform 8-20 s clock made every gap statistically alike;
// clusters + longer calms is what reads as an animal deciding things.
static const uint32_t CAT_BOUT_GAP_MIN_MS = 3500, CAT_BOUT_GAP_SPAN_MS = 3000;  // inside a cluster
static const uint32_t CAT_CALM_MIN_MS = 10000, CAT_CALM_SPAN_MS = 16000;        // between clusters
static const uint32_t CAT_BEG_MIN_MS = 4500, CAT_BEG_SPAN_MS = 3500;      // needy paw between meow loops
static const uint32_t CAT_WAKE_SETTLE_MS = 4800;  // fresh wake: a beat of plain base before the next
                                                  // ask/fidget -- without it the gate expires inside
                                                  // the 3 s wake and a gesture chains onto its tail
static const uint32_t CAT_POST_MEAL_WASH_MS = 4500; // feed lick (1900) + a settle breath before the wash
static const uint32_t CAT_CHAIN_MIN_MS = 900, CAT_CHAIN_SPAN_MS = 700; // the pause inside a bout
static const uint32_t CAT_NAP_BIAS_MS = 20000;  // how long "just woke up" colors the next fidget
static const uint32_t CAT_DREAM_MIN_MS = 6000, CAT_DREAM_SPAN_MS = 12000; // whisker flicks mid-sleep

struct CatChoreo {
  uint32_t nextQuirkOk = 0, nextBegOk = 0;
  uint32_t washAt = 0;    // post-meal face wash deadline; 0 = none pending
  uint32_t chainAt = 0;   // pending bout follow-up deadline; 0 = none
  uint32_t napBiasUntil = 0;   // post-wake grooming bias window; 0 = none
  CatAnim  chainA = CA_COUNT;
  CatAnim  lastContent = CA_COUNT, lastNeedy = CA_COUNT, lastSleep = CA_COUNT;
  PetMood  lastMood = PET_CONTENT;
  uint8_t  boutLeft = 0;   // content gestures still owed to the current activity cluster

  // Re-arm the content gate. The opener of each cluster rolls how many gestures follow it
  // (0-2); while any are owed the gap stays short, and the gesture that pays the last one
  // gets the long calm. Washes and bout chains re-arm through here too, so a post-meal wash
  // can open a cluster of nearby self-care just like a pool pick can.
  uint32_t rearm(uint32_t now, CatRnd rnd) {
    if (boutLeft) boutLeft--;
    else          boutLeft = (uint8_t)rnd(3);
    return now + (boutLeft ? CAT_BOUT_GAP_MIN_MS + rnd(CAT_BOUT_GAP_SPAN_MS)
                           : CAT_CALM_MIN_MS + rnd(CAT_CALM_SPAN_MS));
  }

  // Scene entry. Mood, the pending wash and any bout chain reset (treatcatOnEnter also resets
  // catReact); the clocks and last-gesture memory survive across visits, as the old statics did.
  void enterScene() { lastMood = PET_CONTENT; washAt = 0; chainA = CA_COUNT; napBiasUntil = 0; }

  // A cat washes its face after a meal -- the iconic post-meal beat, so it is deterministic
  // rather than a pool roll. The caller's feed reaction (CA_LICKING) plays first; the wash
  // fires once the lick and a settle breath have passed and the cat is back to content idle.
  // A meal also interrupts any grooming bout in progress: the wash follows regardless.
  void onFeed(uint32_t now) { washAt = now + CAT_POST_MEAL_WASH_MS; chainA = CA_COUNT; }

  // Petting a content cat: mostly the stretch, but sometimes the cat SLOW-BLINKS BACK, wraps its
  // tail, returns the adoring gaze or leans into a head bunt. Counts as the last content gesture
  // so the pool cannot immediately repeat the same beat.
  CatAnim onPet(CatRnd rnd) {
    uint32_t r = rnd(100);
    CatAnim a = r < 40 ? CA_STRETCHING : r < 60 ? CA_SLOWBLINK
              : r < 75 ? CA_TAIL_HUG   : r < 90 ? CA_ADORE : CA_HEAD_BUNT;
    lastContent = a;
    return a;
  }

  // Dream twitches: a sleeping cat's whiskers flick now and then. This owns only the clock;
  // the caller maps a true return onto the existing twitchT render state (the same flick the
  // sniff uses). Armed on the first sleepy query, disarmed by waking, re-armed on each fire --
  // so a fresh doze never inherits a stale deadline.
  uint32_t dreamAt = 0;
  bool dreamTwitch(PetMood m, uint32_t now, CatRnd rnd) {
    if (m != PET_SLEEPY) { dreamAt = 0; return false; }
    if (!dreamAt) { dreamAt = now + CAT_DREAM_MIN_MS + rnd(CAT_DREAM_SPAN_MS); return false; }
    if (now < dreamAt) return false;
    dreamAt = now + CAT_DREAM_MIN_MS + rnd(CAT_DREAM_SPAN_MS);
    return true;
  }

  // Bout chains, rolled when a once-play gesture FINISHES: a face wash sometimes continues
  // down a foreleg, and an itch is often followed by licking the scratched spot (displacement
  // grooming). The follow-up starts after a short pause, not back-to-back -- the pause is what
  // makes it read as one bout instead of two random picks. No chain target is itself a chain
  // trigger, so a bout is one hop by construction. Roll order: percent, sub-pick (itch), delay.
  void onGestureEnd(CatAnim a, uint32_t now, CatRnd rnd) {
    if (a == CA_GROOM_FACE && rnd(100) < 45) chainA = CA_GROOM_FORELEG;
    else if (a == CA_ITCH && rnd(100) < 40)  chainA = rnd(3) < 2 ? CA_LICKING : CA_GROOM_BELLY;
    else return;
    chainAt = now + CAT_CHAIN_MIN_MS + rnd(CAT_CHAIN_SPAN_MS);
  }

  static CatAnim pick(const CatAnim* pool, int n, CatAnim& last, CatRnd rnd) {
    int start = (int)rnd((uint32_t)n);
    for (int i = 0; i < n; i++) {
      CatAnim a = pool[(start + i) % n];
      if (a != last || n == 1) { last = a; return a; }
    }
    last = pool[start];                     // only reachable if a future pool repeats one id
    return last;
  }

  // Frame step. reactActive = a once-play reaction is still on screen; it holds the idle/beg
  // gates but NOT the mood edges (dozing off or waking overrides whatever was playing, exactly
  // as the pre-extraction code clobbered catReact on an edge). Returns CA_COUNT for "nothing
  // new" or the gesture to start this frame.
  CatAnim next(PetMood m, uint32_t now, bool reactActive, CatRnd rnd) {
    // Mood edges are choreography, not just a new looping pose. Sleep gets one yawn/nod on the
    // way down; needy schedules a paw-up ask between meows; content returns to its quirk clock.
    if (m != lastMood) {
      PetMood was = lastMood; lastMood = m;
      chainA = CA_COUNT;   // hunger or sleep interrupts a bout; it does not resume
      if (m == PET_SLEEPY) {
        washAt = 0;        // a doze drops the pending wash: a groom minutes later reads as random
        napBiasUntil = 0;
        return pick(CAT_SLEEP_ENTRIES,
                    (int)(sizeof CAT_SLEEP_ENTRIES / sizeof CAT_SLEEP_ENTRIES[0]),
                    lastSleep, rnd);
      }
      uint32_t settle = (was == PET_SLEEPY) ? CAT_WAKE_SETTLE_MS : 0;
      if (m == PET_NEEDY) nextBegOk = now + 1800 + settle;
      else                nextQuirkOk = now + 2500 + settle;
      if (was == PET_SLEEPY) {
        // Cats groom when they rise; the bridge itself yawns and stretches, so the bias
        // that colors the FIRST fidget after this wake goes to the washes instead.
        napBiasUntil = now + CAT_NAP_BIAS_MS;
        return CA_WAKING;   // leaving the loaf: rise + stretch, not a snap
      }
      return CA_COUNT;
    }
    if (reactActive) return CA_COUNT;
    if (washAt && m == PET_CONTENT) {
      if (now < washAt) return CA_COUNT;   // pending wash also HOLDS the random gate below, so
                                           // no unrelated fidget wedges between meal and wash
      washAt = 0;
      lastContent = CA_GROOM_FACE;
      nextQuirkOk = rearm(now, rnd);
      return CA_GROOM_FACE;
    }
    if (chainA != CA_COUNT && m == PET_CONTENT) {
      if (now < chainAt) return CA_COUNT;   // pending chain holds the random gate, like the wash
      CatAnim a = chainA; chainA = CA_COUNT;
      lastContent = a;
      nextQuirkOk = rearm(now, rnd);
      return a;
    }
    if (m == PET_CONTENT && now >= nextQuirkOk) {
      CatAnim a = CA_COUNT;
      if (napBiasUntil && now <= napBiasUntil) {   // single-use: consumed by this pick either way
        uint32_t r = rnd(100);
        if (r < 40)      a = CA_GROOM_FORELEG;
        else if (r < 65) a = CA_GROOM_FACE;
        if (a != CA_COUNT) lastContent = a;
      }
      napBiasUntil = 0;
      if (a == CA_COUNT)
        a = pick(CAT_CONTENT_GESTURES,
                 (int)(sizeof CAT_CONTENT_GESTURES / sizeof CAT_CONTENT_GESTURES[0]),
                 lastContent, rnd);
      nextQuirkOk = rearm(now, rnd);
      return a;
    }
    if (m == PET_NEEDY && now >= nextBegOk) {
      CatAnim a = pick(CAT_NEEDY_GESTURES,
                       (int)(sizeof CAT_NEEDY_GESTURES / sizeof CAT_NEEDY_GESTURES[0]),
                       lastNeedy, rnd);
      nextBegOk = now + CAT_BEG_MIN_MS + rnd(CAT_BEG_SPAN_MS);
      return a;
    }
    return CA_COUNT;
  }
};
