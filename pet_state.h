#pragma once
#include <stdint.h>

// Derived each frame, never stored. Priority: SLEEPY (latch) > NEEDY (hungry) > CONTENT.
enum PetMood { PET_CONTENT = 0, PET_NEEDY, PET_SLEEPY };

// Tuning knobs, stats are 0..100. Invariants that MUST hold if retuned:
//   PET_FULL_FLOOR + PET_FEED_GAIN > PET_HUNGRY_THRESH   (one feed clears hunger from the floor)
//   PET_SLEEP_LO   + PET_WAKE_MARGIN < PET_SLEEP_HI       (force-wake lands inside the latch band)
constexpr float PET_HUNGRY_THRESH  = 40.0f;   // drives PET_NEEDY/PLEA AND the feed-vs-pet split (same number)
constexpr float PET_FULL_FLOOR     = 10.0f;
constexpr float PET_ENERGY_FLOOR   = 10.0f;
constexpr float PET_FEED_GAIN      = 55.0f;   // 10 + 55 = 65 > 40
constexpr float PET_SLEEP_LO       = 20.0f;   // enter nap at/below
constexpr float PET_SLEEP_HI       = 60.0f;   // wake at/above (hysteresis)
constexpr float PET_WAKE_MARGIN    = 15.0f;   // 20 + 15 = 35 < 60
// Units per REAL second of active render (pet_state.cpp tick()). Ship values, restored from the
// HIL-TEST set of 1.0/1.0/2.0 that stood here from 787dd41 until now -- those were 25x fast so a
// bench run could walk every mood inside a minute, and they invert the mood ordering that
// test_mood_priority asserts, which is what had that test red on this branch all along.
//   fullness 100 -> HUNGRY_THRESH 40 at 0.040/s  = ~25 min
//   energy   100 -> SLEEP_LO 20      at 0.055/s  = ~24 min
//   nap      20  -> SLEEP_HI 60      at 0.40/s   = ~100 s
// To bench-test moods again, put the fast set back TEMPORARILY -- do not ship it.
constexpr float PET_FULLNESS_DRAIN = 0.040f;
constexpr float PET_ENERGY_DRAIN   = 0.055f;
constexpr float PET_ENERGY_RECOVER = 0.40f;

class PetState {
public:
  // Fresh cat: stats full, latch clear. treats carried in from NVS (0 on blank).
  void reset(uint32_t treats);

  // Advance decay/recovery + sleep latch by dtMs of ACTIVE render (REAL ms, not lab-time).
  // careEnabled == false: no decay, latch forced clear (ambient boards).
  void tick(float dtMs, bool careEnabled);

  bool hungry() const;          // fullness < PET_HUNGRY_THRESH

  // Tap while asleep: clear latch, energy = max(energy, SLEEP_LO + WAKE_MARGIN).
  void forceWake();

  // hungry  -> fullness += FEED_GAIN (clamp 100), treats++, returns true (ate).
  // content -> no change, returns false (caller pets instead).
  bool feed();

  PetMood mood(bool careEnabled) const;

  // Bench override, mirroring battery's batsim: force the stats so feed, the nap latch and the
  // every-5-treats fortune can all be exercised on a real unit without waiting out the shipped
  // ~25 min decay, and without putting the fast HIL rates back in a binary that might get given
  // away. Keeps the sleep latch consistent with the energy it is handed -- setting energy under
  // SLEEP_LO while the latch said "awake" would leave a cat that is asleep by every test but
  // still drawing an awake pose.
  void debugSet(float fullness, float energy);

  // Separate from debugSet because treats is the one stat that PERSISTS (treatsSave on each feed),
  // so forcing it is a different kind of act from nudging a decaying float. Needed to reach the
  // every-5-treats fortune at all: one feed costs 10-23 min of decay before the next is possible.
  void debugSetTreats(uint32_t t) { tr = t; }

  float    fullness() const { return full; }
  float    energy()   const { return en; }
  uint32_t treats()   const { return tr; }
  bool     asleep()   const { return sleeping; }

private:
  float    full = 100.0f;
  float    en   = 100.0f;
  uint32_t tr   = 0;
  bool     sleeping = false;   // latched
};
