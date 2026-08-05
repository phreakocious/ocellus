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

constexpr float ARC_SPAN_MAX = 2.9f;    // rad: cap total arc width (~166deg) so it stays a bottom arc.
                                        // The pair margin lives in pairChord (2 font px of ink-to-ink
                                        // whitespace); the cap only decides how far up the sides a
                                        // long name may climb before dropping a font size.

// Glyph collision radius: half-diagonal of the scale * (VGA_FONT_W x VGA_FONT_H) cell. Derived
// from the font constants rather than a literal so it cannot drift if the font is re-baked.
// sqrtf runs once per splash (never per frame) -- safe on the FPU-less C3.
inline float glyphRadiusFor(int scale) {
  float hw = VGA_FONT_W * 0.5f * scale, hh = VGA_FONT_H * 0.5f * scale;
  return sqrtf(hw * hw + hh * hh);
}

// Ink bounding box of one glyph in font pixels, inclusive; the cell center is (4, 8). Spacing
// must use INK, not the 8x16 cell: the cell reserves ascender/descender rows most glyphs never
// touch, so cell-box spacing left a glyph-height of empty whitespace between x-height letters
// stacked up the arc's steep ends ("the p and s are far from the other letters", 2026-08-04).
// Empty glyphs (space, unbaked codes) get a thin virtual box so a gap still advances the arc.
struct InkBox { int8_t x0, x1, y0, y1; };
inline InkBox inkBoxFor(char ch) {
  InkBox b{3, 4, 7, 8};
  uint8_t u = (uint8_t)ch;
  if (u < VGA_FONT_FIRST || u > VGA_FONT_LAST) return b;
  const uint8_t* rows = VGA_FONT[u - VGA_FONT_FIRST];
  int x0 = VGA_FONT_W, x1 = -1, y0 = VGA_FONT_H, y1 = -1;
  for (int r = 0; r < VGA_FONT_H; r++) {
    uint8_t bits = rows[r];
    if (!bits) continue;
    if (r < y0) y0 = r;
    y1 = r;
    for (int c = 0; c < VGA_FONT_W; c++)
      if (bits & (0x80 >> c)) { if (c < x0) x0 = c; if (c > x1) x1 = c; }
  }
  if (y1 >= 0) { b.x0 = (int8_t)x0; b.x1 = (int8_t)x1; b.y0 = (int8_t)y0; b.y1 = (int8_t)y1; }
  return b;
}

// Chord needed between the cell centers of letters a (earlier) and b (next) at `uMid` radians
// from straight-down. The neighbor direction is the local tangent: |dx| = c*cos(u) rightward,
// |dy| = c*sin(u) -- DOWNWARD on the left half (uMid > 0: the next letter sits lower) and
// upward on the right. Ink rects are clear iff separated in x OR y, so take the cheaper axis.
// History, both directions of wrong: width-only spacing (2026-08-02) piled "Phre" into a heap
// up the left side; cell-height spacing (the first fix) exiled the end letters. The margin is
// additive ink-to-ink whitespace -- 2 font px reads like the bottom row's classic gap (typical
// ink is 6-7 px wide in the 8 px cell, so 2 px of air ~= the old 1.15 * cell-width spacing).
inline float pairChord(char a, char b, float uMid, int scale) {
  InkBox A = inkBoxFor(a), B = inkBoxFor(b);
  const float m = 2.0f * scale;                       // ink-to-ink whitespace, px
  float cu = fabsf(cosf(uMid)), su = fabsf(sinf(uMid));
  float needX = (float)(A.x1 - B.x0 + 1) * scale + m; // A's right edge clear of B's left
  float needY = (uMid > 0.0f)
      ? (float)(A.y1 - B.y0 + 1) * scale + m          // b below a: A's bottom clear of B's top
      : (float)(B.y1 - A.y0 + 1) * scale + m;         // b above a: B's bottom clear of A's top
  float byX = (needX <= 0.0f) ? 0.0f : (cu > 1e-4f ? needX / cu : 1e9f);
  float byY = (needY <= 0.0f) ? 0.0f : (su > 1e-4f ? needY / su : 1e9f);
  float c = byX < byY ? byX : byY;
  float cmin = 4.0f * scale;                          // half a cell: keeps the arc progressing even
  return c > cmin ? c : cmin;                         // when disjoint inks would allow a pile-up
}

// Slot offsets from straight-down (radians, positive = left), non-uniform: each pair's gap is
// solved for ITS letters at ITS place on the arc -- the arc is kerned. Fixed point: angles
// place the pairs, pairs re-price their chords, 4 passes settle well inside the 2 px ink
// margin (chords are bounded by cell height + margin, and each pass is a contraction on the
// smooth chord field). Returns the total span. Shared by the size picker and computeSlots so
// they can never disagree. Boot-only trig, same license as the sqrtf note above.
inline float slotOffsets(const char* name, int len, int scale, float* u) {
  float glyphR = glyphRadiusFor(scale), rArc = 120.0f - glyphR - 4.0f;
  float step[MAX_LETTERS];
  int np = len - 1;
  float seed = ((float)VGA_FONT_W * scale) / rArc;
  for (int k = 0; k < np; k++) step[k] = seed;
  float span = 0.0f;
  for (int it = 0; it < 4; it++) {
    span = 0.0f; for (int k = 0; k < np; k++) span += step[k];
    float a = span * 0.5f;
    for (int i = 0; i < len; i++) { u[i] = a; if (i < np) a -= step[i]; }
    for (int k = 0; k < np; k++) {
      float c = pairChord(name[k], name[k + 1], (u[k] + u[k + 1]) * 0.5f, scale);
      if (c > 2.0f * rArc) c = 2.0f * rArc;
      step[k] = 2.0f * asinf(c / (2.0f * rArc));
    }
  }
  span = 0.0f; for (int k = 0; k < np; k++) span += step[k];
  float a = span * 0.5f;
  for (int i = 0; i < len; i++) { u[i] = a; if (i < np) a -= step[i]; }
  return span;
}

// Largest font scale whose whole arc still fits within ARC_SPAN_MAX -- readable glyphs,
// shrinking only as far as THIS name forces (ink-kerned spacing makes the ladder name-
// dependent: "phreakocious" packs tighter than twelve capital Ws). The unit test pins the
// boundaries for reference names. Widening the cap still costs nothing at the rim: the worst
// glyph-box corner is rArc + glyphR = 116.0 at every scale.
inline int scaleFor(const char* name, int len) {
  float u[MAX_LETTERS];
  for (int s = 4; s > 1; s--)
    if (slotOffsets(name, len, s, u) <= ARC_SPAN_MAX) return s;
  return 1;
}

inline Geometry geometryFor(const char* name, int len) {
  Geometry g;
  g.cx = 120; g.cy = 120; g.R = 120;
  g.scale = scaleFor(name, len);
  g.glyphR = glyphRadiusFor(g.scale);
  g.rArc = g.R - g.glyphR - 4.0f;   // ride the bottom rim, glyph fully on-screen
  g.gravity = 0.5f;
  g.restitution = 0.88f;            // "rubber" -- lively bounces, gentle settle
  return g;
}

// Slot centers along the bottom arc, name order left-to-right. One-time trig (boot only).
inline void computeSlots(Trajectories& T, const char* name, const Geometry& g) {
  const float DOWN = 1.5707963f;                 // straight down (screen y grows downward)
  float u[MAX_LETTERS];
  slotOffsets(name, T.count, g.scale, u);        // same spacing the size picker solved against
  for (int i = 0; i < T.count; i++) {
    float a = DOWN + u[i];                       // u[0] positive = leftmost letter
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

// Compute the whole reveal. `name` supplies the glyphs the kerned spacing measures; `len` may
// be shorter than the string but never longer. Caller renders the glyphs (this owns only
// geometry + motion). Returns by filling T.
inline void compute(Trajectories& T, const char* name, int len, uint32_t seed) {
  if (len < 1) len = 1;
  if (len > MAX_LETTERS) len = MAX_LETTERS;
  T.count = len;
  Geometry g = geometryFor(name, len);
  computeSlots(T, name, g);
  T.maxFrames = 0;
  for (int i = 0; i < len; i++) {
    simLetter(T, i, g, seed);
    if (T.frames[i] > T.maxFrames) T.maxFrames = T.frames[i];
  }
}

}  // namespace bounce
