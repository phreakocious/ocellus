#include <cmath>
#include <unity.h>
#include "../../fluid.h"

using namespace fluid;

static float angleError(float a, float b) { return fabsf(wrapPi(a - b)); }

static float surfPeak(const Sim& s) {
  float m = 0.0f;
  for (int i = 0; i < COLS; i++) if (fabsf(s.elev[i]) > m) m = fabsf(s.elev[i]);
  return m;
}

// Signed water volume error of the column field; flux-form updates must keep this ~0.
static float fieldVolume(const Sim& s) {
  float v = 0.0f;
  for (int i = 0; i < COLS; i++) v += s.elev[i];
  return v * s.dx;
}

static void advance(Sim& s, float gx, float gy, float stir, float seconds, float dt) {
  int n = (int)lroundf(seconds / dt);
  for (int i = 0; i < n; i++) step(s, gx, gy, stir, dt);
}

void test_fill_level_matches_requested_area() {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.40f, fillFraction(levelForFillPct(40)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, fillFraction(levelForFillPct(25)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.70f, fillFraction(levelForFillPct(70)));
}

void test_init_is_a_settled_downward_pool() {
  Sim s; init(s, 40, 123);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, PI_F * 0.5f, s.angle);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, PI_F * 0.5f, s.pendingAngle);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.tiltHoldTime);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, s.wallStrength);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.wallImpactTimer);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.40f, fillFraction(s.level));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.angularVelocity);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.timeAcc);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.dropCredit);
  TEST_ASSERT_TRUE(s.grav > 1.0f);
  TEST_ASSERT_TRUE(s.depthCap * s.grav > 0.9f);        // CFL headroom actually reserved
  TEST_ASSERT_EQUAL_INT(0, activeDrops(s));
  for (int i = 0; i < COLS; i++) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.elev[i]);
    TEST_ASSERT_TRUE(s.bowl[i] > 0.0f);
  }
  for (int i = 0; i <= COLS; i++) TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.flow[i]);
}

void test_gravity_turns_surface_continuously() {
  Sim s; init(s, 40, 1);
  advance(s, 1.0f, 0.0f, 0.0f, 6.0f, 1.0f / 60.0f);   // ringy spring (zeta 0.15) needs ~5 s to settle
  TEST_ASSERT_TRUE(angleError(s.angle, 0.0f) < 0.08f);
  TEST_ASSERT_TRUE(fabsf(s.angularVelocity) < 0.25f);
}

void test_low_tilt_sloshes_water_before_retargeting_gravity() {
  Sim s; init(s, 40, 11);
  advance(s, 0.25f, 0.0f, 0.0f, 0.25f, 1.0f / 60.0f);

  TEST_ASSERT_TRUE(angleError(s.targetAngle, PI_F * 0.5f) < 0.001f);   // equilibrium held...
  TEST_ASSERT_TRUE(angleError(s.angle, PI_F * 0.5f) > 0.08f);          // ...but the pool rocks
  TEST_ASSERT_TRUE(surfPeak(s) > 0.04f);                               // and the water surges

  advance(s, 0.25f, 0.0f, 0.0f, 0.75f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(angleError(s.targetAngle, 0.0f) < 0.08f);           // hold expired: retargeted
}

void test_strong_tilt_retargets_gravity_without_hold() {
  Sim s; init(s, 40, 12);
  step(s, 0.8f, 0.0f, 0.0f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(angleError(s.targetAngle, 0.0f) < 0.001f);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.tiltHoldTime);
}

void test_low_tilt_gestures_get_seeded_variation() {
  Sim a, b; init(a, 40, 101); init(b, 40, 202);
  advance(a, 0.25f, 0.0f, 0.0f, 0.30f, 1.0f / 60.0f);
  advance(b, 0.25f, 0.0f, 0.0f, 0.30f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(fabsf(a.wallStrength - b.wallStrength) > 0.01f);
  float difference = angleError(a.angle, b.angle);
  for (int i = 0; i < COLS; i++) difference += fabsf(a.elev[i] - b.elev[i]);
  TEST_ASSERT_TRUE(difference > 0.02f);
}

void test_angle_wrap_takes_short_path() {
  Sim s; init(s, 40, 2);
  s.angle = PI_F - 0.03f;
  float target = -PI_F + 0.03f;
  step(s, cosf(target), sinf(target), 0.0f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(s.angularVelocity > 0.0f);       // +0.06 rad across the seam, not a -2*pi turn
  TEST_ASSERT_TRUE(angleError(s.angle, target) < 0.06f);
}

// Physics runs on a fixed 1/120 s accumulator, so with constant inputs the k-th slice is
// bit-identical whatever the frame rate; runs may differ only by the <1 leftover slice.
void test_timestep_independent_across_frame_rates() {
  Sim slow, fast; init(slow, 40, 77); init(fast, 40, 77);
  advance(slow, 0.6f, 0.8f, 0.08f, 2.0f, 1.0f / 25.0f);
  advance(fast, 0.6f, 0.8f, 0.08f, 2.0f, 1.0f / 60.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.0050f, slow.angle, fast.angle);
  TEST_ASSERT_FLOAT_WITHIN(0.0100f, slow.angularVelocity, fast.angularVelocity);
  TEST_ASSERT_FLOAT_WITHIN(0.0100f, slow.agitation, fast.agitation);
  for (int i = 0; i < COLS; i++) TEST_ASSERT_FLOAT_WITHIN(0.0050f, slow.elev[i], fast.elev[i]);
}

// Same seed + same inputs => identical trajectories. Catches a Sim field added but not set in
// init(): host-test stack garbage diverges the runs, while device static zero-init would hide it.
void test_deterministic_same_seed() {
  Sim a, b; init(a, 40, 42); init(b, 40, 42);
  for (int i = 0; i < 400; i++) {
    float g = ((i / 25) & 1) ? 0.0f : PI_F * 0.5f;
    step(a, cosf(g), sinf(g), 0.12f, 1.0f / 60.0f);
    step(b, cosf(g), sinf(g), 0.12f, 1.0f / 60.0f);
  }
  TEST_ASSERT_EQUAL_UINT32(a.rng, b.rng);            // identical RNG consumption
  TEST_ASSERT_EQUAL_FLOAT(a.angle, b.angle);
  TEST_ASSERT_EQUAL_FLOAT(a.angularVelocity, b.angularVelocity);
  TEST_ASSERT_EQUAL_FLOAT(a.pendingAngle, b.pendingAngle);
  TEST_ASSERT_EQUAL_FLOAT(a.tiltHoldTime, b.tiltHoldTime);
  TEST_ASSERT_EQUAL_FLOAT(a.wallStrength, b.wallStrength);
  TEST_ASSERT_EQUAL_FLOAT(a.wallImpactTimer, b.wallImpactTimer);
  TEST_ASSERT_EQUAL_FLOAT(a.agitation, b.agitation);
  TEST_ASSERT_EQUAL_FLOAT(a.timeAcc, b.timeAcc);
  TEST_ASSERT_EQUAL_FLOAT(a.dropCredit, b.dropCredit);
  for (int i = 0; i < COLS; i++) TEST_ASSERT_EQUAL_FLOAT(a.elev[i], b.elev[i]);
  for (int i = 0; i <= COLS; i++) TEST_ASSERT_EQUAL_FLOAT(a.flow[i], b.flow[i]);
  for (int i = 0; i < DROP_COUNT; i++) {
    TEST_ASSERT_EQUAL_FLOAT(a.drops[i].life, b.drops[i].life);
    if (a.drops[i].life > 0.0f) {
      TEST_ASSERT_EQUAL_FLOAT(a.drops[i].x, b.drops[i].x);
      TEST_ASSERT_EQUAL_FLOAT(a.drops[i].y, b.drops[i].y);
    }
  }
}

void test_flick_excites_waves_then_settles() {
  Sim s; init(s, 40, 4);
  advance(s, 1.0f, 0.0f, 0.0f, 0.35f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(surfPeak(s) > 0.10f);             // a 90-degree flip slams the water hard
  TEST_ASSERT_TRUE(s.agitation > 0.3f);

  advance(s, 1.0f, 0.0f, 0.0f, 8.0f, 1.0f / 60.0f);
  TEST_ASSERT_TRUE(surfPeak(s) < 0.05f);             // flick energy gone, only ambient swell left
  TEST_ASSERT_TRUE(s.agitation < 0.15f);
}

static float sampledFill(const Sim& s) {
  int inside = 0, liquid = 0;
  constexpr int N = 241;
  for (int iy = 0; iy < N; iy++) {
    float y = -1.0f + 2.0f * iy / (N - 1);
    for (int ix = 0; ix < N; ix++) {
      float x = -1.0f + 2.0f * ix / (N - 1);
      if (x * x + y * y > 1.0f) continue;
      inside++;
      if (signedDepth(s, x, y) >= 0.0f) liquid++;
    }
  }
  return (float)liquid / (float)inside;
}

void test_waves_preserve_visible_volume() {
  Sim s; init(s, 40, 5);
  float calm = sampledFill(s);
  s.angle = 1.17f;
  for (int i = 0; i < COLS; i++)                     // zero-mean surface deformation
    s.elev[i] = 0.06f * sinf(2.0f * PI_F * ((float)i + 0.5f) / (float)COLS);
  float moving = sampledFill(s);
  TEST_ASSERT_FLOAT_WITHIN(0.008f, 0.40f, calm);
  TEST_ASSERT_FLOAT_WITHIN(0.018f, calm, moving);
}

// Dynamic counterpart: the flux-form update must conserve water exactly (wall fluxes are zero),
// so hard rocking can never pump volume in or out of the pool.
void test_stepping_preserves_visible_volume() {
  Sim s; init(s, 40, 9);
  float calm = sampledFill(s);
  for (int i = 0; i < 240; i++) {                     // 4 s of hard rocking, measured mid-agitation
    float a = ((i / 15) & 1) ? 0.3f : PI_F - 0.3f;
    step(s, cosf(a), sinf(a), 0.1f, 1.0f / 60.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0f, fieldVolume(s));   // column field: exact conservation
  TEST_ASSERT_FLOAT_WITHIN(0.030f, calm, sampledFill(s));    // rendered area: still mid-slosh
}

// Violent shaking must shed spray -- droplets that break away from the surface -- and every
// droplet must stay finite and inside the bowl until it splashes back down.
void test_hard_shake_sheds_bounded_spray() {
  Sim s; init(s, 40, 6);
  int dropsSeen = 0;
  for (int i = 0; i < 300; i++) {
    float a = ((i / 20) & 1) ? 0.0f : PI_F;
    step(s, cosf(a), sinf(a), 4.0f, 1.0f / 60.0f);
    dropsSeen += activeDrops(s);
    for (int d = 0; d < DROP_COUNT; d++) if (s.drops[d].life > 0.0f) {
      TEST_ASSERT_TRUE(isfinite(s.drops[d].x));
      TEST_ASSERT_TRUE(isfinite(s.drops[d].y));
      TEST_ASSERT_TRUE(s.drops[d].x * s.drops[d].x + s.drops[d].y * s.drops[d].y < 1.0f);
    }
  }
  TEST_ASSERT_TRUE(dropsSeen > 0);
  for (int i = 0; i < COLS; i++) TEST_ASSERT_TRUE(isfinite(s.elev[i]));   // field survived the abuse
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0f, fieldVolume(s));
}

// Regression for the "pool races around the rim when spun" bug: stir must churn the water
// (waves/agitation), never rotate the pool's equilibrium away from gravity. Spinning a
// glass doesn't move the water; it stirs it.
void test_spin_churns_but_does_not_rotate_pool() {
  Sim s; init(s, 40, 8);
  advance(s, 0.0f, 1.0f, 2.5f, 3.0f, 1.0f / 60.0f);   // sustained ~143 deg/s spin, gravity fixed down
  TEST_ASSERT_TRUE(angleError(s.angle, PI_F * 0.5f) < 0.15f);   // pool stays at physical down
  TEST_ASSERT_TRUE(s.agitation > 0.3f);                          // but the water is visibly churned
}

// Gentle ~1 Hz hand-rocking must deflect the surface visibly (>= ~6 px at R=118 -> 0.05 in bowl
// units), or the liquid reads as a rigid rotating chord.
void test_gentle_rocking_makes_visible_waves() {
  Sim s; init(s, 40, 10);
  float peak = 0.0f;
  for (int i = 0; i < 180; i++) {                      // 3 s at 60 fps
    float t = (float)i / 60.0f;
    float a = PI_F * 0.5f + 0.4f * sinf(2.0f * PI_F * t);
    step(s, cosf(a), sinf(a), 0.0f, 1.0f / 60.0f);
    if (surfPeak(s) > peak) peak = surfPeak(s);
  }
  TEST_ASSERT_TRUE(peak > 0.05f);
}

// A resting pool must settle (no drift, near-zero agitation, no spray) yet never freeze: the
// ambient swell keeps a small, bounded roll on the surface so the liquid always reads as alive.
void test_rest_settles_but_surface_stays_alive() {
  Sim s; init(s, 40, 7);
  advance(s, 0.0f, 1.0f, 0.0f, 8.0f, 1.0f / 30.0f);
  TEST_ASSERT_TRUE(angleError(s.angle, PI_F * 0.5f) < 0.001f);
  TEST_ASSERT_TRUE(fabsf(s.angularVelocity) < 0.01f);
  TEST_ASSERT_TRUE(s.agitation < 0.05f);
  TEST_ASSERT_EQUAL_INT(0, activeDrops(s));

  float lo = 1.0e9f, hi = -1.0e9f, pk = 0.0f;          // near-wall column over one swell period
  for (int i = 0; i < 16 * 30; i++) {
    step(s, 0.0f, 1.0f, 0.0f, 1.0f / 30.0f);
    if (s.elev[2] < lo) lo = s.elev[2];
    if (s.elev[2] > hi) hi = s.elev[2];
    if (surfPeak(s) > pk) pk = surfPeak(s);
  }
  TEST_ASSERT_TRUE(hi - lo > 0.008f);                  // visibly rolling (~1 px+ at R=118)
  TEST_ASSERT_TRUE(pk < 0.06f);                        // but calm, nowhere near handling energy
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fill_level_matches_requested_area);
  RUN_TEST(test_init_is_a_settled_downward_pool);
  RUN_TEST(test_gravity_turns_surface_continuously);
  RUN_TEST(test_low_tilt_sloshes_water_before_retargeting_gravity);
  RUN_TEST(test_strong_tilt_retargets_gravity_without_hold);
  RUN_TEST(test_low_tilt_gestures_get_seeded_variation);
  RUN_TEST(test_angle_wrap_takes_short_path);
  RUN_TEST(test_timestep_independent_across_frame_rates);
  RUN_TEST(test_deterministic_same_seed);
  RUN_TEST(test_flick_excites_waves_then_settles);
  RUN_TEST(test_waves_preserve_visible_volume);
  RUN_TEST(test_stepping_preserves_visible_volume);
  RUN_TEST(test_hard_shake_sheds_bounded_spray);
  RUN_TEST(test_spin_churns_but_does_not_rotate_pool);
  RUN_TEST(test_gentle_rocking_makes_visible_waves);
  RUN_TEST(test_rest_settles_but_surface_stays_alive);
  return UNITY_END();
}
