#pragma once
#include <cstdint>
#include <cmath>
#include "vga_font.h"

// Boot "bounce" name reveal, computed by REVERSE SIMULATION so a genuinely random-looking
// entry lands *exactly* in order along a bottom arc -- no snap, no cheat.
//
// The trick (the user's): we know the finished arrangement (each letter's arc slot). For each
// letter we pick a random ARRIVAL velocity, negate it, and run real physics forward from the
// slot (gravity + reflect off the round wall) until the letter flies off the rim -- recording
// every position. Forward PLAYBACK is that recording replayed in reverse: the letter appears
// off-screen, does its bounces, and arrives on its slot at the final frame. Because playback is
// stored positions replayed, the landing is pixel-exact by construction: no time-reversible
// integrator or drift math needed.
//
// Energy note: to make forward playback look like a ball LOSING energy each bounce (natural
// settle), the backward sim must GAIN energy -- so on a backward bounce we divide speed by
// restitution (amplify) rather than multiply.
//
// A per-letter cap of 3-4 bounces bounds every trajectory and *guarantees* an exit on its own
// (once capped, the wall stops reflecting and the letter sails off the rim), so there's no
// "re-roll if it never leaves" fallback.
//
// Pure / Arduino-free (like matrix_name.h) so the native suite exercises the physics on the host.
namespace bounce {

constexpr int   MAX_LETTERS = 16;    // arc gets crowded past this; caller falls back to another style
constexpr int   MAX_FRAMES  = 200;   // trajectory buffer cap (~3.2s at 16ms); a runaway sim stops here

struct Geometry {
  float cx, cy;       // display center (px)
  float R;            // visible round-display radius (px)
  float rArc;         // radius of the settle arc (px)
  float glyphR;       // glyph collision radius (px) -- keeps the glyph fully inside R at a bounce
  float gravity;      // px / frame^2 (screen y grows downward)
  float restitution;  // 0..1 wall energy retained on a forward bounce
  int   scale;        // VGA font scale the caller renders at (carried here so it's picked once)
};

struct Trajectories {
  int     count;
  int     frames[MAX_LETTERS];              // forward-playback length per letter
  int     maxFrames;                        // max over letters == total animation length
  int16_t x[MAX_LETTERS][MAX_FRAMES];       // forward playback: [i][0] = off-screen entry ... [i][frames-1] = slot
  int16_t y[MAX_LETTERS][MAX_FRAMES];
  int16_t slotX[MAX_LETTERS], slotY[MAX_LETTERS];
};

// Deterministic LCG so trajectories reproduce for the same seed (host-testable; no Arduino random()).
inline uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
inline float    frand(uint32_t& s) { return (lcg(s) >> 8) * (1.0f / 16777216.0f); }  // [0,1)

constexpr float ARC_SPACING  = 1.15f;   // adjacent glyph centers = 1.15 * glyph width (slight gap)
constexpr float ARC_SPAN_MAX = 2.44f;   // rad: cap total arc width (~140deg) so it stays a bottom arc

// Glyph collision radius: half-diagonal of the scale * (VGA_FONT_W x VGA_FONT_H) cell. Derived
// from the font constants rather than a literal so it cannot drift if the font is re-baked.
// sqrtf runs once per splash (never per frame) -- safe on the FPU-less C3.
inline float glyphRadiusFor(int scale) {
  float hw = VGA_FONT_W * 0.5f * scale, hh = VGA_FONT_H * 0.5f * scale;
  return sqrtf(hw * hw + hh * hh);
}

// Angular gap between adjacent letters at a given font scale. rArc reserves the glyph's
// half-diagonal so nothing clips the round edge. Shared by the size picker and computeSlots so
// they can never disagree.
inline float arcStepRad(int len, int scale) {
  float glyphR = glyphRadiusFor(scale), rArc = 120.0f - glyphR - 4.0f;
  return (len > 1) ? (ARC_SPACING * (float)VGA_FONT_W * scale) / rArc : 0.0f;
}

// Largest font scale whose whole arc still fits within ARC_SPAN_MAX -- readable glyphs,
// shrinking only as far as a long name forces. Ladder: 1-6 -> 4x, 7-8 -> 3x, 9-14 -> 2x,
// 15-16 -> 1x. (A 1-letter name takes 4x: arcStepRad returns 0 for len <= 1.)
//
// The cap is what sets those boundaries, and it cannot separate them: a 6-letter name needs
// 2.2936 rad for 4x and a 14-letter name needs 2.4380 for 2x, so any cap clearing the second
// clears the first. 2.44 was chosen on glass (2026-08-02) to lift BOTH -- 14 letters read too
// small at 1x. Widening costs nothing at the rim: the worst glyph-box corner is rArc + glyphR,
// which is 116.0 at every scale regardless of how wide the arc spreads.
inline int scaleFor(int len) {
  for (int s = 4; s > 1; s--)
    if (arcStepRad(len, s) * (len - 1) <= ARC_SPAN_MAX) return s;
  return 1;
}

inline Geometry geometryFor(int len) {
  Geometry g;
  g.cx = 120; g.cy = 120; g.R = 120;
  g.scale = scaleFor(len);
  g.glyphR = glyphRadiusFor(g.scale);
  g.rArc = g.R - g.glyphR - 4.0f;   // ride the bottom rim, glyph fully on-screen
  g.gravity = 0.5f;
  g.restitution = 0.88f;            // "rubber" -- lively bounces, gentle settle
  return g;
}

// Slot centers along the bottom arc, name order left-to-right. One-time trig (boot only).
inline void computeSlots(Trajectories& T, const Geometry& g) {
  const float DOWN = 1.5707963f;                 // straight down (screen y grows downward)
  float step = arcStepRad(T.count, g.scale);     // same spacing the size picker solved against
  float a0 = DOWN + step * (T.count - 1) * 0.5f; // largest angle = leftmost letter (i=0)
  for (int i = 0; i < T.count; i++) {
    float a = a0 - step * i;
    T.slotX[i] = (int16_t)lroundf(g.cx + g.rArc * cosf(a));
    T.slotY[i] = (int16_t)lroundf(g.cy + g.rArc * sinf(a));
  }
}

// Reverse-sim one letter into T.x[i]/T.y[i] (already in forward-playback order).
inline void simLetter(Trajectories& T, int i, const Geometry& g, uint32_t& seed) {
  // Forward ARRIVAL velocity: drops into the slot moving down-and-sideways ("a bit chaotic").
  float speed = 5.0f + frand(seed) * 4.0f;                 // px/frame
  float ang   = 1.5707963f + (frand(seed) - 0.5f) * 2.0f;  // straight-down +/- ~57deg
  float avx = speed * cosf(ang);
  float avy = speed * fabsf(sinf(ang));                    // force downward arrival (sin>=0)
  int   cap = 3 + (int)(lcg(seed) & 1u);                   // 3 or 4 bounces before it may leave

  // Backward run: start at slot with NEGATED arrival velocity; gravity unchanged.
  float px = T.slotX[i], py = T.slotY[i];
  float vx = -avx, vy = -avy;
  const float Rw   = g.R - g.glyphR;   // bounce boundary (glyph stays fully inside R)
  const float Roff = g.R + g.glyphR;   // fully off-screen threshold
  const float invE = 1.0f / g.restitution;

  int m = 0, bounces = 0;
  while (m < MAX_FRAMES) {
    T.x[i][m] = (int16_t)lroundf(px);
    T.y[i][m] = (int16_t)lroundf(py);
    m++;

    vy += g.gravity;                   // semi-implicit Euler
    float nx = px + vx, ny = py + vy;
    float dx = nx - g.cx, dy = ny - g.cy;
    float d2 = dx * dx + dy * dy;

    if (d2 >= Rw * Rw) {               // crossed the (inner) wall
      if (bounces < cap) {             // still bouncing: reflect off the radial normal, lose energy
        float d = sqrtf(d2);           // sqrt only on a bounce event (rare), not per frame
        float nnx = dx / d, nny = dy / d;
        float vdotn = vx * nnx + vy * nny;
        vx = (vx - 2.0f * vdotn * nnx) * invE;   // amplify going backward -> forward looks damped
        vy = (vy - 2.0f * vdotn * nny) * invE;
        nx = g.cx + nnx * (Rw - 1.0f); // nudge just inside so we don't re-trigger next step
        ny = g.cy + nny * (Rw - 1.0f);
        bounces++;
      } else if (m < MAX_FRAMES && d2 >= Roff * Roff) {  // capped out and now fully off-screen: exit
        // m < MAX_FRAMES guards the writes below: without it, a trajectory that runs all the way
        // to the cap writes T.x[i][MAX_FRAMES] / T.y[i][MAX_FRAMES] -- one past the array.
        T.x[i][m] = (int16_t)lroundf(nx);
        T.y[i][m] = (int16_t)lroundf(ny);
        m++;
        px = nx; py = ny;
        break;
      }
    }
    px = nx; py = ny;
  }

  // Forward playback = backward path reversed in place: [0]=off-screen entry, [m-1]=slot.
  for (int a = 0, b = m - 1; a < b; a++, b--) {
    int16_t tx = T.x[i][a]; T.x[i][a] = T.x[i][b]; T.x[i][b] = tx;
    int16_t ty = T.y[i][a]; T.y[i][a] = T.y[i][b]; T.y[i][b] = ty;
  }
  T.frames[i] = m;
}

// Compute the whole reveal. `len` letters, `seed` for the random entries. Caller renders the
// glyphs (this owns only geometry + motion). Returns by filling T.
inline void compute(Trajectories& T, int len, uint32_t seed) {
  if (len < 1) len = 1;
  if (len > MAX_LETTERS) len = MAX_LETTERS;
  T.count = len;
  Geometry g = geometryFor(len);
  computeSlots(T, g);
  T.maxFrames = 0;
  for (int i = 0; i < len; i++) {
    simLetter(T, i, g, seed);
    if (T.frames[i] > T.maxFrames) T.maxFrames = T.frames[i];
  }
}

}  // namespace bounce
