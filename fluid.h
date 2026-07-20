#pragma once
#include <cstdint>
#include <cmath>

// Pure, Arduino-free 2-D slosh model for the round display. A damped angular mode follows gravity
// for bulk orientation; on top of it the free surface is a 1-D shallow-water column field (real
// volume exchange, so waves travel, reflect, steepen, run up the walls, and expose the bowl floor
// on a hard tilt) plus ballistic spray droplets shed from fast-rising crests. Coordinates passed
// to signedDepth() are normalized to a unit-radius bowl, +x right / +y down.
namespace fluid {

constexpr float PI_F     = 3.14159265358979323846f;
constexpr int   COLS     = 40;             // shallow-water columns across the free surface
constexpr int   DROP_COUNT = 24;
constexpr float SLICE_S  = 1.0f / 120.0f;  // fixed physics step; step() accumulates wall time
constexpr float FLOW_MAX = 1.4f;           // bowl-radii/s; CFL guard together with grav derating
constexpr float MIN_TILT_G = 0.06f;        // ignore near-flat direction noise below this strength
constexpr float DIRECT_TILT_G = 0.60f;     // stronger tilts retarget gravity without a wall-slosh hold
// Ambient swell shared phase: wraps every 16 s; the forcing terms use integer multiples of this
// rate so every sinf stays continuous across the wrap.
constexpr float SWELL_BASE_W = 2.0f * PI_F / 16.0f;

struct Droplet {
  float x, y;       // screen-space position in unit-bowl coordinates
  float vx, vy;     // bowl-radii / second
  float life;       // seconds remaining; <=0 means unused
};

struct Sim {
  float angle;                    // liquid down-normal; free surface is perpendicular to this
  float angularVelocity;          // rad/s, retained so a tilt overshoots and rebounds
  float targetAngle;              // gravity equilibrium currently pulling the pool
  float pendingAngle;             // low-tilt direction gathering time before it becomes the target
  float tiltHoldTime;             // seconds spent pushing waves toward pendingAngle
  float wallStrength;             // seeded per-gesture torque variation
  float wallImpactTimer;          // seconds until the next irregular wave kick
  float level;                    // normalized waterline offset giving the requested fill area
  float span;                     // half-width of the free surface, sqrt(1-level^2); fixed after init
  float elev[COLS];               // surface elevation per column, positive = crest (extra water)
  float flow[COLS + 1];           // velocity at column faces; [0]/[COLS] are the walls, always 0
  float bowl[COLS];               // resting water depth per column (bowl floor to still waterline)
  float dx;                       // column width in bowl units
  float grav;                     // wave gravity, derated at init so CFL holds at any fill level
  float depthCap;                 // flux depth ceiling: keeps wave speed CFL-safe when water piles up
  float agitation;                // 0..1 envelope for highlights/meniscus
  float swellPhase;               // ambient-swell shared phase, radians, wraps at 2*pi
  float timeAcc;                  // wall-time accumulator; physics advances in exact SLICE_S steps
  float dropCredit;               // deterministic fractional droplet-spawn accumulator
  uint32_t rng;
  Droplet drops[DROP_COUNT];
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float wrapPi(float a) {
  while (a >  PI_F) a -= 2.0f * PI_F;
  while (a < -PI_F) a += 2.0f * PI_F;
  return a;
}

// Fraction of a unit circle on the +down side of a line q=level. Monotonic 1 -> 0.
inline float fillFraction(float level) {
  level = clampf(level, -1.0f, 1.0f);
  return (acosf(level) - level * sqrtf(clampf(1.0f - level * level, 0.0f, 1.0f))) / PI_F;
}

inline float levelForFillPct(int fillPct) {
  float wanted = clampf((float)fillPct / 100.0f, 0.05f, 0.95f);
  float lo = -1.0f, hi = 1.0f;
  for (int i = 0; i < 24; i++) {
    float mid = (lo + hi) * 0.5f;
    if (fillFraction(mid) > wanted) lo = mid; else hi = mid;
  }
  return (lo + hi) * 0.5f;
}

inline uint32_t nextRand(Sim& s) {
  s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
  return s.rng;
}

inline float rand01(Sim& s) { return (float)(nextRand(s) & 0x00FFFFFFu) / 16777216.0f; }

inline void init(Sim& s, int fillPct, uint32_t seed) {
  s.angle = s.targetAngle = PI_F * 0.5f;       // down on screen until the first sensor sample
  s.pendingAngle = s.targetAngle;
  s.tiltHoldTime = 0.0f;
  s.wallStrength = 1.0f;
  s.wallImpactTimer = 0.0f;
  s.angularVelocity = 0.0f;
  s.level = levelForFillPct(fillPct);
  s.span = sqrtf(clampf(1.0f - s.level * s.level, 0.01f, 1.0f));
  s.dx = 2.0f * s.span / (float)COLS;
  float maxDepth = 0.015f;
  for (int i = 0; i < COLS; i++) {
    float x = -s.span + ((float)i + 0.5f) * s.dx;
    float d = sqrtf(clampf(1.0f - x * x, 0.0f, 1.0f)) - s.level;
    s.bowl[i] = d < 0.015f ? 0.015f : d;
    if (s.bowl[i] > maxDepth) maxDepth = s.bowl[i];
    s.elev[i] = 0.0f;
  }
  for (int i = 0; i <= COLS; i++) s.flow[i] = 0.0f;
  // Wave speed is sqrt(grav*depth); derate grav so speed plus the flow clamp can never cross more
  // than ~0.85 columns per physics slice (CFL), whatever fill level the config asks for.
  constexpr float WANTED_GRAV = 12.0f;
  float cMax = 0.85f * s.dx / SLICE_S - FLOW_MAX;
  if (cMax < 0.5f) cMax = 0.5f;
  float gLimit = cMax * cMax / maxDepth;
  s.grav = WANTED_GRAV < gLimit ? WANTED_GRAV : gLimit;
  s.depthCap = cMax * cMax / s.grav;   // >= maxDepth by construction; binds only on hard pile-up
  s.agitation = s.swellPhase = s.timeAcc = s.dropCredit = 0.0f;
  s.rng = seed ? seed : 0x6D2B79F5u;
  for (int i = 0; i < DROP_COUNT; i++) s.drops[i].life = 0.0f;
}

// Surface height sampled at u = across/span in -1..1, linear between column centers. Positive
// return pushes the surface toward +down (a trough); columns store elevation, hence the flip.
// Past the chord ends it holds the wall column's value, so run-up stays visible at the rim.
inline float waveHeight(const Sim& s, float u) {
  float fi = (clampf(u, -1.0f, 1.0f) + 1.0f) * (0.5f * (float)COLS) - 0.5f;
  int i = (int)floorf(fi);
  float t = fi - (float)i;
  if (i < 0) { i = 0; t = 0.0f; } else if (i >= COLS - 1) { i = COLS - 2; t = 1.0f; }
  return -(s.elev[i] + (s.elev[i + 1] - s.elev[i]) * t);
}

// Positive is liquid, zero is the free surface, negative is air. The c/sn overload takes a
// precomputed cos/sin of s.angle so repeat callers don't redo the trig (and can't derive a
// mismatched rotation basis).
inline float signedDepth(const Sim& s, float x, float y, float c, float sn) {
  float down = x * c + y * sn;
  float across = -x * sn + y * c;
  return down - s.level - waveHeight(s, across / s.span);
}

inline float signedDepth(const Sim& s, float x, float y) {
  return signedDepth(s, x, y, cosf(s.angle), sinf(s.angle));
}

inline void substep(Sim& s, float sensedAngle, float tilt, bool haveTilt, float stir, float dt) {
  // A weak in-plane gravity vector is a shallow tilt of a face-up display. Its direction is noisy,
  // and immediately turning the whole pool makes a tiny nudge look like a 90-degree gravity flip.
  // Hold the current equilibrium briefly instead, feeding the requested direction into the water
  // as a wall shove. Larger tilts bypass the hold so deliberate orientation changes stay
  // responsive. All strengths are in g (main.cpp normalizes the QMI8658's raw counts).
  constexpr float MAX_HOLD_S = 0.75f;
  constexpr float MIN_HOLD_S = 0.16f;
  constexpr float WALL_TORQUE_GAIN = 60.0f;
  float wallDrive = 0.0f;
  if (haveTilt && tilt >= DIRECT_TILT_G) {
    s.targetAngle = s.pendingAngle = sensedAngle;
    s.tiltHoldTime = 0.0f;
    s.wallImpactTimer = 0.0f;
  } else if (haveTilt && tilt >= MIN_TILT_G) {
    float towardTarget = wrapPi(sensedAngle - s.targetAngle);
    if (fabsf(towardTarget) < 0.07f) {
      s.pendingAngle = sensedAngle;
      s.tiltHoldTime = 0.0f;
    } else {
      if (!(s.tiltHoldTime > 0.0f)) {
        s.pendingAngle = sensedAngle;
        s.wallStrength = 0.78f + rand01(s) * 0.62f;
        s.wallImpactTimer = rand01(s) * 0.07f;
      }
      // Track hand movement without letting low-magnitude direction noise whip the candidate around.
      float track = clampf(wrapPi(sensedAngle - s.pendingAngle), -4.0f * dt, 4.0f * dt);
      s.pendingAngle = wrapPi(s.pendingAngle + track);
      float strength = clampf((tilt - MIN_TILT_G) / (DIRECT_TILT_G - MIN_TILT_G), 0.0f, 1.0f);
      float hold = MAX_HOLD_S + (MIN_HOLD_S - MAX_HOLD_S) * strength;
      s.tiltHoldTime += dt;
      if (s.tiltHoldTime >= hold) {
        s.targetAngle = s.pendingAngle;
        s.tiltHoldTime = 0.0f;
      } else {
        float wallErr = clampf(wrapPi(s.pendingAngle - s.angle), -1.5f, 1.5f);
        // Rock the entire free surface before moving its equilibrium. The clamp makes a shallow
        // nudge dramatic without allowing a near-sideways flick to pin angular velocity at its cap.
        wallDrive = clampf(WALL_TORQUE_GAIN * s.wallStrength * tilt * wallErr, -18.0f, 18.0f);
      }
    }
  } else if (!haveTilt || tilt < MIN_TILT_G) {
    // An unreadable/near-flat sample must neither finish a pending turn nor accumulate noise energy.
    s.tiltHoldTime = 0.0f;
    s.wallImpactTimer = 0.0f;
  }

  // Underdamped angular spring for bulk orientation. Deliberately ringy (zeta 0.15): the overshoot
  // is what excites the water field below into back-and-forth slosh instead of a clean rotation.
  constexpr float ANGULAR_FREQ = 4.3f;
  constexpr float ANGULAR_ZETA = 0.15f;
  float err = wrapPi(s.targetAngle - s.angle);
  float accel = ANGULAR_FREQ * ANGULAR_FREQ * err
              - 2.0f * ANGULAR_ZETA * ANGULAR_FREQ * s.angularVelocity
              + wallDrive;
  s.angularVelocity = clampf(s.angularVelocity + accel * dt, -12.0f, 12.0f);
  s.angle = wrapPi(s.angle + s.angularVelocity * dt);

  // --- 1-D shallow water across the pool frame ---
  // Uniform tangential forcing: residual gravity toward true down (the water leans before the
  // frame catches up, then sloshes back when it does), gyro stir drag, the Euler pseudo-force of
  // the rotating frame, and a slow two-tone ambient swell so a resting pool never freezes flat.
  s.swellPhase += SWELL_BASE_W * dt;
  if (s.swellPhase > 2.0f * PI_F) s.swellPhase -= 2.0f * PI_F;
  float lean = haveTilt ? 0.45f * s.grav * tilt * sinf(wrapPi(sensedAngle - s.angle)) : 0.0f;
  float forceU = lean + 0.70f * stir - 0.30f * accel
               + 0.012f * s.grav * sinf(5.0f * s.swellPhase);
  // Per-position forcing: centrifugal push toward both walls while the pool swings, plus the
  // asymmetric half of the ambient swell.
  float forceX = 0.5f * s.angularVelocity * s.angularVelocity
               + (0.010f * s.grav / s.span) * sinf(8.0f * s.swellPhase + 1.3f);

  // Irregular wall shoves while a low-tilt gesture is held: a velocity surge injected near the
  // wall the water is being pushed from, so each nudge launches an asymmetric traveling wave.
  float surge = 0.0f;
  int surgeLo = 0, surgeHi = 0;
  if (wallDrive != 0.0f) {
    s.wallImpactTimer -= dt;
    if (s.wallImpactTimer <= 0.0f) {
      surge = (wallDrive < 0.0f ? -1.0f : 1.0f) * tilt * (0.9f + rand01(s) * 1.4f);
      int band = COLS / 3;
      surgeLo = wallDrive > 0.0f ? 1 : COLS - band;
      surgeHi = surgeLo + band;
      s.wallImpactTimer += 0.11f + rand01(s) * 0.18f;
    }
  }

  float invDx = 1.0f / s.dx;
  float nf[COLS + 1];
  nf[0] = nf[COLS] = 0.0f;
  float flowSum = 0.0f;
  for (int i = 1; i < COLS; i++) {
    float xf = -s.span + (float)i * s.dx;
    float grad = (s.elev[i] - s.elev[i - 1]) * invDx;
    // Upwind advection: the nonlinearity that steepens crests and breaks clean reflections.
    float adv = s.flow[i] > 0.0f ? s.flow[i] * (s.flow[i] - s.flow[i - 1]) * invDx
                                 : s.flow[i] * (s.flow[i + 1] - s.flow[i]) * invDx;
    float a = -s.grav * grad - adv + forceU + forceX * xf;
    // Wall friction: a boundary band ((x/span)^8) bleeds energy, so reflections come back ragged
    // instead of mirror-clean; strong flow through the band also picks up random churn.
    float e = xf / s.span; e *= e; e *= e; e *= e;         // (x/span)^8
    float damp = 1.0f - (0.6f + 7.0f * e) * dt;
    float v = (s.flow[i] + a * dt) * (damp > 0.0f ? damp : 0.0f);
    if (i >= surgeLo && i < surgeHi) v += surge;           // one-slice velocity kick
    if (e > 0.15f && fabsf(v) > 0.35f) v += (rand01(s) - 0.5f) * 9.0f * fabsf(v) * dt;
    nf[i] = clampf(v, -FLOW_MAX, FLOW_MAX);
    flowSum += fabsf(nf[i]);
  }

  // Conservative flux update (donor-cell depth): volume is exact, and a column running dry on a
  // hard tilt just exposes the bowl floor instead of going negative.
  float q[COLS + 1];
  q[0] = q[COLS] = 0.0f;
  for (int i = 1; i < COLS; i++) {
    float dDon = nf[i] > 0.0f ? (s.bowl[i - 1] + s.elev[i - 1]) : (s.bowl[i] + s.elev[i]);
    q[i] = dDon > 0.0f ? nf[i] * (dDon < s.depthCap ? dDon : s.depthCap) : 0.0f;
  }
  s.dropCredit = clampf(s.dropCredit + s.agitation * s.agitation * 25.0f * dt, 0.0f, 3.0f);
  float c = cosf(s.angle), sn = sinf(s.angle), tx = -sn, ty = c;
  for (int i = 0; i < COLS; i++) {
    float rise = (q[i] - q[i + 1]) * invDx;   // d(elev)/dt
    s.elev[i] += rise * dt;
    s.flow[i] = nf[i];
    // Fast-rising crests shed spray: ballistic droplets that break away and fall back in.
    // Purely visual -- they carry no volume out of the field.
    if (rise > 0.55f && s.elev[i] > 0.03f && s.dropCredit >= 1.0f) {
      for (int d = 0; d < DROP_COUNT; d++) {
        if (s.drops[d].life > 0.0f) continue;
        s.dropCredit -= 1.0f;
        float across = -s.span + ((float)i + 0.2f + rand01(s) * 0.6f) * s.dx;
        float down = s.level - s.elev[i] - 0.02f;
        float launch = rise * (1.1f + rand01(s) * 0.9f);
        float side = 0.5f * (nf[i] + nf[i + 1]) + (rand01(s) - 0.5f) * 0.4f;
        Droplet& b = s.drops[d];
        b.x = c * down + tx * across;
        b.y = sn * down + ty * across;
        b.vx = -c * launch + tx * side;
        b.vy = -sn * launch + ty * side;
        b.life = 1.2f;
        break;
      }
    }
  }
  s.flow[COLS] = 0.0f;

  float energy = fabsf(s.angularVelocity) * 0.12f + (flowSum / (float)COLS) * 1.3f
               + fabsf(stir) * 0.15f;
  float wanted = clampf(energy, 0.0f, 1.0f);
  float rate = wanted > s.agitation ? 7.0f : 1.0f;   // slower decay: churn visibly lingers
  s.agitation += (wanted - s.agitation) * clampf(rate * dt, 0.0f, 1.0f);

  // Droplets fly ballistically under real sensed gravity and splash back into the surface.
  float gDir = (haveTilt && tilt >= MIN_TILT_G) ? sensedAngle : s.angle;
  float gMag = 0.75f * s.grav * clampf(tilt, 0.35f, 1.5f);
  float gvx = gMag * cosf(gDir), gvy = gMag * sinf(gDir);
  for (int i = 0; i < DROP_COUNT; i++) {
    Droplet& b = s.drops[i];
    if (b.life <= 0.0f) continue;
    b.vx += gvx * dt; b.vy += gvy * dt;
    b.x += b.vx * dt; b.y += b.vy * dt;
    b.life -= dt;
    if (b.life <= 0.0f || b.x * b.x + b.y * b.y > 0.96f ||
        signedDepth(s, b.x, b.y, c, sn) > 0.01f) b.life = 0.0f;
  }
}

// Advance by elapsed wall time. Physics runs on a fixed 1/120 s step behind an accumulator, so
// every frame rate produces the identical slice sequence (and identical RNG draws) -- droppable
// frames cost nothing but latency. Long mode-switch gaps are deliberately capped.
// gx/gy are the screen-plane gravity vector in g, so its magnitude expresses tilt strength. stir is
// board spin in rad/s; it churns the water (waves/agitation) but never rotates the pool's
// equilibrium -- spinning a glass stirs the water, it doesn't relocate it.
inline void step(Sim& s, float gx, float gy, float stir, float dt) {
  if (!(dt > 0.0f)) return;
  s.timeAcc += clampf(dt, 0.0f, 0.10f);
  float tiltSq = gx * gx + gy * gy;
  bool haveTilt = tiltSq > 1.0e-8f;
  float tilt = haveTilt ? sqrtf(tiltSq) : 0.0f;
  float sensedAngle = haveTilt ? wrapPi(atan2f(gy, gx)) : s.targetAngle;
  while (s.timeAcc >= SLICE_S) {
    s.timeAcc -= SLICE_S;
    substep(s, sensedAngle, tilt, haveTilt, stir, SLICE_S);
  }
}

inline int activeDrops(const Sim& s) {
  int n = 0; for (int i = 0; i < DROP_COUNT; i++) if (s.drops[i].life > 0.0f) n++;
  return n;
}

}  // namespace fluid
