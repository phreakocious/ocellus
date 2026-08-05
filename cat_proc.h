// cat_proc.h — parametric cat renderer for treatcat (anim id 46).
// Spec: docs/superpowers/specs/2026-07-28-procedural-cat-design.md
// Arduino-free by contract: <stdint.h> + <math.h> only, so it compiles on the Mac
// for the tuning rig (tools/cat_tune.py) and the native tests. Header-only,
// single-TU: every function static, included by exactly one .cpp per binary.
#pragma once
#include <stdint.h>
#include <math.h>

enum CatAnim {
  CA_IDLE = 0, CA_MEOW, CA_SLEEPING, CA_LICKING, CA_STRETCHING, CA_ITCH,
  CA_KNEADING, CA_BEGGING, CA_YAWNING,
  CA_PLEASE, CA_TAIL_HUG, CA_NODDING,
  CA_SLOWBLINK, CA_SNIFF, CA_WAKING,
  CA_GROOM_FACE, CA_GROOM_FORELEG, CA_GROOM_BELLY, CA_ADORE,
  CA_HEAD_BUNT, CA_PROTEST, CA_CURL_UP,
  CA_POUNCE, CA_COUNT
};
enum CatPlay { CP_LOOP = 0, CP_HOLD, CP_ONCE };

// Grid re-aspected 2026-07-29: 68x46 was sized to preserve the OLD baked sprite's wide
// 200x136 composition. This cat is tall and front-facing — it saturated 45 of 46 rows while
// using 36 of 68 columns, so it could not grow. 112x104 at P=2 is the same screen footprint
// with 4x the cells and width headroom for the wider poses (stretch, sleep).
// 112 rows not 104: the extra 8 are ear headroom above the skull. At 104 the top of the
// headSize x earLen space clipped (head 1.3 + ear 10 and up), which would have capped the
// big-eared presets. 896 B is a cheap way to not constrain the art.
// CAT_P is the on-device blit scale ONLY -- no geometry in this header reads it, so changing it
// rescales the drawn cat without moving a single feature. 1, not 2: at x2 the 224x224 cat ran the
// full height of the panel and the affirmation box sat ON it, hiding 35% of a standing pose and
// 48% of the sleeping loaf. At x1 the whole cat clears the box's top edge.
static const int CAT_GW = 112, CAT_GH = 112, CAT_P = 1;

// Palette slots (per-preset pal[16]). Fur and accent are 4-step RAMPS ordered brightest to
// darkest and indexed by the lighting band; everything else is a flat colour. Renumbered
// 2026-07-29 with the lighting rewrite so the ramps are contiguous — the ASCII diag map reads
// as "1-4 is fur, 5-8 is accent" instead of the old split across 1/2/10/11.
enum : uint8_t {
  CI_TRANS = 0,
  CI_FUR_0 = 1, CI_FUR_1 = 2, CI_FUR_2 = 3, CI_FUR_3 = 4,   // highlight / base / shade / deep
  CI_ACC_0 = 5, CI_ACC_1 = 6, CI_ACC_2 = 7, CI_ACC_3 = 8,   // marking accent, same 4 steps
  CI_EYE   = 9,                 // the WHOLE eye: solid, dark, one catchlight
  CI_OUTLINE = 10,              // sticker outline (2026-07-29, artist pass). Repurposes the slot
                                // the pupil retired: a dedicated slot because the outline must sit
                                // CLOSER to the fill than the shadow band, and per-preset so each
                                // coat can tune its own rim.
  CI_NOSE  = 11, CI_EAR = 12,   // inner ear
  CI_MOUTH = 13,
  CI_HILITE = 14,               // eye catchlight
  CI_BLUSH = 15,                // cheek blush
};
static const int CAT_BANDS = 4;
static const uint8_t CAT_FUR[CAT_BANDS] = { CI_FUR_0, CI_FUR_1, CI_FUR_2, CI_FUR_3 };
static const uint8_t CAT_ACC[CAT_BANDS] = { CI_ACC_0, CI_ACC_1, CI_ACC_2, CI_ACC_3 };
// Face ramp: the skull is the read, and a full-depth terminator drops one eye into shadow.
// Same four slots, two of them collapsed, so nothing else in the pipeline changes.
static const uint8_t CAT_FUR_FLAT[CAT_BANDS] = { CI_FUR_0, CI_FUR_1, CI_FUR_1, CI_FUR_2 };
// Sticker ramp: one step flatter than the face's — both shadow bands collapse into base, only
// the lit crescent survives. Consequence: the far plane's rear-leg tone disappears in flat mode
// (band+1 still lands on base, CAT_BANDS-1 clamp included), so rear legs read only through the
// outline pass + halo seams in that mode — deliberate, matches the sticker references.
static const uint8_t CAT_FUR_STICKER[CAT_BANDS] = { CI_FUR_0, CI_FUR_1, CI_FUR_1, CI_FUR_1 };

// which step of a ramp an index is, or -1 if it is not on that ramp
static inline int catRampBand(const uint8_t* ramp, uint8_t idx) {
  for (int i = 0; i < CAT_BANDS; i++) if (ramp[i] == idx) return i;
  return -1;
}

// Part ids for the diagnostics owner map and clip bitmask. Rear and front legs are deliberately
// distinct: the head-on painter stack puts the whole rear pair behind the body and the whole
// front pair in front, and diagnostics must be able to catch a regression to side-paired limbs.
enum : uint8_t {
  CATP_TAIL = 0, CATP_REAR_LEGS, CATP_BODY, CATP_FRONT_LEGS,
  CATP_EARS, CATP_HEAD, CATP_FACE, CAT_PART_COUNT
};

// Collected DURING the primitive passes: painter-order overdraw destroys exactly
// this information in the finished grid (spec §Diagnostics). Caller-owned so the
// device (diag == nullptr) allocates none of it.
struct CatDiagnostics {
  uint8_t  clipped;                    // bitmask by CATP_*: part hit a grid edge
  uint16_t bboxX0, bboxY0, bboxX1, bboxY1;
  uint16_t cellsFilled;                // first-writes -> fill ratio
  uint16_t headBodyOverlap;            // cells written by both head and body
  uint16_t occluded[CAT_PART_COUNT];   // cells a part wrote that a later part overwrote
  uint8_t  owner[CAT_GH][CAT_GW];      // scratch: part id owning each cell
};

// rasterizer context threaded through every fill
// Every member carries a default so `CatRaster r;` is fully deterministic: a construction site
// that forgets one flag must get false, not stack garbage read as bool (UBSan caught exactly
// that -- the test harness's fresh() omitted remapLight and the remap branch went light at random).
struct CatRaster {
  uint8_t (*g)[CAT_GW] = nullptr;
  CatDiagnostics* d = nullptr;   // nullable
  uint8_t part = 0;         // CATP_* for diag attribution
  uint8_t clipTo = 0;       // nonzero: write only where the grid already holds this index
  int     clipCx = 0, clipCy = 0, clipR = 0;  // nonzero R: write only inside this disc. Geometric, not
                            // index-based -- the head and the body are both fur, so a marking that
                            // must stop at the skull cannot be clipped by what is under the brush.
  bool    remap = false;    // marking mode: fur band k -> accent band k, else skip
  bool    remapLight = false;  // with remap: lift toward the fur ramp's bright end instead (the blaze)
  bool    far = false;      // depth cue: shift one band darker (used by the rear-leg plane)
  bool    contact = false;  // draw a contact halo under this part before drawing it
  bool    darken = false;   // halo pass: step EXISTING fur/accent one band darker, paint nothing new
  bool    furOnly = false;  // paint only over fur/accent — keeps a flat colour off eyes and background
  bool    rim = false;      // under-draw pass: shEllipse/shCapsule paint the grown CI_OUTLINE silhouette
                            // instead of their normal shaded fill (spec §Sticker pass, under-draw)
  bool    mirror = false;   // exact scene-facing flip. catPlot reflects x at the single write gate,
                            // after shading/features are resolved, so every palette index travels
                            // with the drawing and the mirrored frame stays artistically identical.
  const uint8_t* ramp = nullptr; // non-null: shade per cell from the primitive's own normal into this ramp
};

// ---- lighting -------------------------------------------------------------
// Rewritten 2026-07-29. The old model drew each shape, then drew it AGAIN offset (-2,-2) and
// clipped inside itself. That is a screen-space trick, not a light: every shape got an
// identical-width rim regardless of its form, its size or where it sat, which is exactly why
// the cat read flat. Each primitive now shades per cell from its own surface normal.
// Q8 fixed point end to end — no FPU (the C3 has none), no sqrt, no trig in the inner loop.
// Mutable under CAT_TUNE only, so the rig can scrub the lamp without a constexpr rebuild;
// the device build keeps them const and folds them into the inner loop.
#ifdef CAT_TUNE
static int catLX = -141, catLY = -154, catLZ = 148;               // unit light dir, Q8, upper-left
static int catBandHi = 225, catBandMid = 132, catBandLo = 44;
#else
static const int catLX = -141, catLY = -154, catLZ = 148;
static const int catBandHi = 225, catBandMid = 132, catBandLo = 44;
#endif

// Lambert band from a Q8 surface normal's x/y. nz is reconstructed as 1 - (nx^2+ny^2)/2, a
// Taylor stand-in for sqrt that keeps the rim falloff — that falloff is what makes a filled
// ellipse read as a sphere instead of a disc, and it is the whole point of the rewrite.
static inline uint8_t catBandN(int nx, int ny) {
  int d2 = (nx * nx + ny * ny) >> 8;                 // Q8, 0 at centre .. 256 at the rim
  if (d2 > 256) d2 = 256;
  int nz = 256 - (d2 >> 1);
  int s = (nx * catLX + ny * catLY + nz * catLZ) >> 8;
  if (s >= catBandHi)  return 0;
  if (s >= catBandMid) return 1;
  if (s >= catBandLo)  return 2;
  return 3;
}
// r.far pushes a whole anatomical plane one step down the ramp — depth reuses the light ramp
// rather than pairing one arbitrarily chosen left/right leg at a different tone.
static inline uint8_t catShade(const CatRaster& r, int nx, int ny) {
  int b = catBandN(nx, ny) + (r.far ? 1 : 0);
  return r.ramp[b < CAT_BANDS ? b : CAT_BANDS - 1];
}

// one step darker on whichever ramp this index belongs to; -1 if it is on neither
static inline int catStepDarker(uint8_t cur, uint8_t& out) {
  int b = catRampBand(CAT_FUR, cur);
  if (b >= 0) { out = CAT_FUR[b + 1 < CAT_BANDS ? b + 1 : b]; return b; }
  b = catRampBand(CAT_ACC, cur);
  if (b >= 0) { out = CAT_ACC[b + 1 < CAT_BANDS ? b + 1 : b]; return b; }
  return -1;
}

static inline void catPlot(CatRaster& r, int x, int y, uint8_t idx) {
  // BEFORE the mirror gate, unlike every other test here: the disc is given in the same
  // unmirrored space the caller computed the shape in, so flipping x first would test against a
  // centre on the wrong side of the grid.
  if (r.clipR) {
    int dx = x - r.clipCx, dy = y - r.clipCy;
    if (dx * dx + dy * dy > r.clipR * r.clipR) return;
  }
  if (r.mirror) x = CAT_GW - 1 - x;   // single write gate: everything downstream (clipTo/remap
                                      // reads, owner map, bbox) already lives in mirrored space
  if (x < 0 || x >= CAT_GW || y < 0 || y >= CAT_GH) {
    // Passes that can only paint over what is already on the grid must not raise the clip flag:
    // off-grid there is nothing for them to paint, so an overhang is not a real clip. The halo
    // is deliberately larger than its part, and blush is deliberately wider than the cheek.
    if (r.d && !r.darken && !r.furOnly) r.d->clipped |= (uint8_t)(1u << r.part);
    return;
  }
  uint8_t cur = r.g[y][x];
  if (r.darken) {                      // contact halo: shade what is ALREADY there
    if (catStepDarker(cur, idx) < 0) return;      // background or a flat colour: leave it
  } else if (r.remap) {                // markings recolor fur only — silhouette-safe by construction
    int b = catRampBand(CAT_FUR, cur);
    if (b < 0) return;                 // not fur (eye, nose, background): leave it alone
    // band-parallel either way, so a marking keeps the body's shading: stripes cross to the
    // accent ramp; the blaze goes white-where-lit (CI_HILITE reused, the same trick whiskers
    // pull on CI_FUR_0 -- the palette is full) shading into pale fur, so the bib still reads
    // as part of the sphere instead of a flat sticker.
    idx = r.remapLight ? (b <= 1 ? CI_HILITE : CAT_FUR[b - 2]) : CAT_ACC[b];
  } else if (r.clipTo) {
    // A standalone branch, not `r.clipTo && cur != r.clipTo`: that form falls through to furOnly
    // below whenever cur already equals clipTo, and furOnly's fur/accent test doesn't know CI_EYE
    // (the catchlight's target) is a valid surface — it silently ate the catchlight. clipTo, once
    // set, must be the only gate; the two flags are simultaneously live for the eye catchlight
    // (furOnly clips the eye pass to the face, clipTo clips the catchlight to the eye it just drew).
    if (cur != r.clipTo) return;
  } else if (r.furOnly && (cur == CI_OUTLINE ||
           (catRampBand(CAT_FUR, cur) < 0 && catRampBand(CAT_ACC, cur) < 0))) return;
  if (r.d) {
    CatDiagnostics& D = *r.d;
    if (!r.rim) {
      // Outline is bookkeeping underlay, not part content (CI_OUTLINE can only exist via the
      // rim pass, so this check is exact, not a heuristic): covering background OR the ring is
      // a fresh fill, never an occlusion — nothing real was there before this part arrived.
      if (cur == CI_TRANS || cur == CI_OUTLINE) D.cellsFilled++;
      else if (D.owner[y][x] != r.part) {
        D.occluded[D.owner[y][x]]++;
        if (D.owner[y][x] == CATP_BODY && r.part == CATP_HEAD) D.headBodyOverlap++;
      }
      D.owner[y][x] = r.part;
    }
    // Rim writes skip owner/cellsFilled/occlusion entirely: fills re-cover most of the ring, and
    // rim-over-rim between part groups would double-book every part boundary the ring crosses.
    // Extent is still real, though — a clipped ring is genuine visible damage (the clip-flag
    // path above already covers that), so bbox tracks both rim and fill writes.
    if (x < D.bboxX0) D.bboxX0 = (uint16_t)x;
    if (x > D.bboxX1) D.bboxX1 = (uint16_t)x;
    if (y < D.bboxY0) D.bboxY0 = (uint16_t)y;
    if (y > D.bboxY1) D.bboxY1 = (uint16_t)y;
  }
  r.g[y][x] = idx;
}

// clamp a fill's bbox to the grid; flags the part as clipped if it lost anything
static bool catClampBox(CatRaster& r, int& x0, int& y0, int& x1, int& y1) {
  if (x0 < 0 || y0 < 0 || x1 >= CAT_GW || y1 >= CAT_GH) {
    if (r.d && !r.darken && !r.furOnly) r.d->clipped |= (uint8_t)(1u << r.part);   // see catPlot
  }
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > CAT_GW - 1) x1 = CAT_GW - 1;
  if (y1 > CAT_GH - 1) y1 = CAT_GH - 1;
  return x0 <= x1 && y0 <= y1;
}

// ---- the three primitives (spec §Primitives). Integer-only inner loops. ----

static void catEllipse(CatRaster& r, int cx, int cy, int rx, int ry, uint8_t idx) {
  if (rx < 0 || ry < 0) return;
  int x0 = cx - rx, y0 = cy - ry, x1 = cx + rx, y1 = cy + ry;
  if (!catClampBox(r, x0, y0, x1, y1)) return;
  int rx2 = rx * rx, ry2 = ry * ry;
  if (rx2 == 0 || ry2 == 0) {                        // degenerate: 1-cell row/column/point
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) catPlot(r, x, y, idx);
    return;
  }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      int ex = x - cx, ey = y - cy;
      if (ex * ex * ry2 + ey * ey * rx2 > rx2 * ry2) continue;
      // (ex/rx, ey/ry) IS the hemisphere normal — the containment test above already had it.
      catPlot(r, x, y, r.ramp ? catShade(r, ex * 256 / rx, ey * 256 / ry) : idx);
    }
}

static void catCapsule(CatRaster& r, int x0, int y0, int x1, int y1, int rad, uint8_t idx) {
  if (rad <= 0) {                                    // r=0 degenerates to Bresenham (brows, whiskers, mouth)
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;   // dy <= 0
    int err = dx + dy;
    for (;;) {
      catPlot(r, x0, y0, idx);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return;
  }
  int dx = x1 - x0, dy = y1 - y0;
  long long len2 = (long long)dx * dx + (long long)dy * dy;
  long long L = len2 ? len2 : 1;
  int bx0 = (x0 < x1 ? x0 : x1) - rad, bx1 = (x0 > x1 ? x0 : x1) + rad;
  int by0 = (y0 < y1 ? y0 : y1) - rad, by1 = (y0 > y1 ? y0 : y1) + rad;
  if (!catClampBox(r, bx0, by0, bx1, by1)) return;
  for (int y = by0; y <= by1; y++)
    for (int x = bx0; x <= bx1; x++) {
      long long px = x - x0, py = y - y0;
      long long t = px * dx + py * dy;               // parameter * len2, clamped to the segment
      if (t < 0) t = 0; else if (t > len2) t = len2;
      long long ex = px * L - (long long)dx * t;     // error vector scaled by len2
      long long ey = py * L - (long long)dy * t;
      if (ex * ex + ey * ey > (long long)rad * rad * L * L) continue;
      // A capsule is a cylinder: the normal is that same perpendicular error vector, so
      // normalising it by rad*L costs one divide and the legs/tail round off properly.
      if (r.ramp) {
        long long den = (long long)rad * L;
        catPlot(r, x, y, catShade(r, (int)(ex * 256 / den), (int)(ey * 256 / den)));
      } else catPlot(r, x, y, idx);
    }
}

static void catTri(CatRaster& r, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t idx) {
  long long A = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
  if (A == 0) return;
  if (A < 0) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }   // normalize winding
  int bx0 = x0, bx1 = x0, by0 = y0, by1 = y0;
  if (x1 < bx0) bx0 = x1; if (x2 < bx0) bx0 = x2;
  if (x1 > bx1) bx1 = x1; if (x2 > bx1) bx1 = x2;
  if (y1 < by0) by0 = y1; if (y2 < by0) by0 = y2;
  if (y1 > by1) by1 = y1; if (y2 > by1) by1 = y2;
  // Capture the shading frame BEFORE catClampBox trims the box, or a triangle touching a grid
  // edge would shade against a lopsided centre and change tone as it slid off screen.
  int scx = (bx0 + bx1) / 2, scy = (by0 + by1) / 2;
  int srx = (bx1 - bx0) / 2, sry = (by1 - by0) / 2;
  if (!catClampBox(r, bx0, by0, bx1, by1)) return;
  for (int y = by0; y <= by1; y++)
    for (int x = bx0; x <= bx1; x++) {
      long long e0 = (long long)(x1 - x0) * (y - y0) - (long long)(y1 - y0) * (x - x0);
      long long e1 = (long long)(x2 - x1) * (y - y1) - (long long)(y2 - y1) * (x - x1);
      long long e2 = (long long)(x0 - x2) * (y - y2) - (long long)(y0 - y2) * (x - x2);
      if (!(e0 >= 0 && e1 >= 0 && e2 >= 0)) continue;
      // An ear is flat, so it has no real normal — shade it as a gentle dome over its own
      // bounding box. Enough to keep it from reading as a paper cutout next to a lit head.
      catPlot(r, x, y, r.ramp ? catShade(r, srx ? (x - scx) * 256 / srx : 0,
                                            sry ? (y - scy) * 256 / sry : 0) : idx);
    }
}

// Distance test for one segment, lifted from catCapsule's inner loop. catCapsule keeps its own copy
// deliberately: there the perpendicular error vector IS the cylinder normal, so it is fused into the
// same loop rather than duplicated. ponytail: two call sites, no shared abstraction worth building.
static inline bool catNearSeg(int px, int py, int x0, int y0, int x1, int y1, int rad) {
  long long dx = x1 - x0, dy = y1 - y0, qx = px - x0, qy = py - y0;
  long long len2 = dx * dx + dy * dy, L = len2 ? len2 : 1;
  long long t = qx * dx + qy * dy;                   // parameter * len2, clamped to the segment
  if (t < 0) t = 0; else if (t > len2) t = len2;
  long long ex = qx * L - dx * t, ey = qy * L - dy * t;
  return ex * ex + ey * ey <= (long long)rad * rad * L * L;
}

// Rounded triangle = the hull of three discs = Minkowski(triangle, disc). Straight tangent sides
// from a wide base to a round tip, which is what an ear is. A capsule is the degenerate case with
// the base corners coincident, so this replaces the ear's two coaxial capsules AND their radius
// step -- the step was the "nipple" at high earPoint, and it was structural to drawing two shapes.
static void catRTri(CatRaster& r, int x0, int y0, int x1, int y1, int x2, int y2,
                    int rad, uint8_t idx) {
  if (rad < 0) rad = 0;
  long long A = (long long)(x1 - x0) * (y2 - y0) - (long long)(y1 - y0) * (x2 - x0);
  if (A < 0) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; A = -A; }  // normalize winding
  int bx0 = x0, bx1 = x0, by0 = y0, by1 = y0;
  if (x1 < bx0) bx0 = x1; if (x2 < bx0) bx0 = x2;
  if (x1 > bx1) bx1 = x1; if (x2 > bx1) bx1 = x2;
  if (y1 < by0) by0 = y1; if (y2 < by0) by0 = y2;
  if (y1 > by1) by1 = y1; if (y2 > by1) by1 = y2;
  bx0 -= rad; by0 -= rad; bx1 += rad; by1 += rad;
  // Capture the shading frame BEFORE catClampBox trims the box, or a shape touching a grid edge
  // would shade against a lopsided centre and change tone as it slid off screen (same rule as catTri).
  int scx = (bx0 + bx1) / 2, scy = (by0 + by1) / 2;
  int srx = (bx1 - bx0) / 2, sry = (by1 - by0) / 2;
  if (!catClampBox(r, bx0, by0, bx1, by1)) return;
  for (int y = by0; y <= by1; y++)
    for (int x = bx0; x <= bx1; x++) {
      bool in = false;
      if (A != 0) {
        long long e0 = (long long)(x1 - x0) * (y - y0) - (long long)(y1 - y0) * (x - x0);
        long long e1 = (long long)(x2 - x1) * (y - y1) - (long long)(y2 - y1) * (x - x1);
        long long e2 = (long long)(x0 - x2) * (y - y2) - (long long)(y0 - y2) * (x - x2);
        in = e0 >= 0 && e1 >= 0 && e2 >= 0;
      }
      if (!in && rad > 0)
        in = catNearSeg(x, y, x0, y0, x1, y1, rad) || catNearSeg(x, y, x1, y1, x2, y2, rad) ||
             catNearSeg(x, y, x2, y2, x0, y0, rad);
      if (!in) continue;
      // An ear is a flat flap with no real normal, so it shades as a gentle dome over its own
      // bounding box -- ONE frame for the whole shape, which is why no seam is possible inside it.
      catPlot(r, x, y, r.ramp ? catShade(r, srx ? (x - scx) * 256 / srx : 0,
                                           sry ? (y - scy) * 256 / sry : 0) : idx);
    }
}

// ---- shaded fills: one pass, banded per cell against CAT_FUR ----------------
// These were two passes each (fill, then fill again offset and clipped inside). Dropping the
// offset also retires the whole "any on-grid guard must budget the shading offset" bug class —
// shapes no longer extend CAT_LX/CAT_LY past the geometry they declare.
// r.contact prefixes a HALO pass: the same shape grown by CAT_CONTACT cells, darkening only
// what is already on the grid. The shadow lands on the surface underneath, which is where an
// occluder actually casts one — the first attempt darkened the incoming cells instead and put
// a dark cap over the whole top of the skull where the ear bases are buried.
// Used by the HEAD (twice — see shEllipse) and the near foreleg capsule. Halos compound: each
// pass steps a cell one more band down, so applying it to every limb made the old four-leg
// row read as banded sausages, but the skull needs two to separate from the chest.
static const int CAT_CONTACT = 2;

static void shEllipse(CatRaster& r, int cx, int cy, int rx, int ry,
                      const uint8_t* ramp = CAT_FUR) {
  if (r.rim) { catEllipse(r, cx, cy, rx + 1, ry + 1, CI_OUTLINE); return; }   // under-draw outline
  if (r.contact) { r.darken = true;   // twice: one band does not separate skull from chest
    catEllipse(r, cx, cy, rx + CAT_CONTACT, ry + CAT_CONTACT, 0);
    catEllipse(r, cx, cy, rx + CAT_CONTACT - 1, ry + CAT_CONTACT - 1, 0); r.darken = false; }
  r.ramp = ramp; catEllipse(r, cx, cy, rx, ry, CI_FUR_1); r.ramp = nullptr;
}
static void shCapsule(CatRaster& r, int x0, int y0, int x1, int y1, int rad,
                      const uint8_t* ramp = CAT_FUR) {
  if (r.rim) { catCapsule(r, x0, y0, x1, y1, rad + 1, CI_OUTLINE); return; }   // under-draw outline
  if (r.contact) { r.darken = true;
    catCapsule(r, x0, y0, x1, y1, rad + CAT_CONTACT, 0); r.darken = false; }
  r.ramp = ramp; catCapsule(r, x0, y0, x1, y1, rad, CI_FUR_1); r.ramp = nullptr;
}
// The ear is a rounded triangle again (spec 2026-07-29), so the shaded/rim wrapper is back. No
// contact halo: ears never had one, and the head's own halo is what separates them from the skull.
static void shRTri(CatRaster& r, int x0, int y0, int x1, int y1, int x2, int y2, int rad,
                   const uint8_t* ramp = CAT_FUR) {
  if (r.rim) { catRTri(r, x0, y0, x1, y1, x2, y2, rad + 1, CI_OUTLINE); return; }   // under-draw
  r.ramp = ramp; catRTri(r, x0, y0, x1, y1, x2, y2, rad, CI_FUR_1); r.ramp = nullptr;
}
// catTri survives for the nose, which is a flat colour and wants no ramp at all.

// ---- skeleton (spec §Skeleton) ---------------------------------------------
// All-float aggregate: catLerp walks it as a float array. Keep it that way.
struct CatPose {
  float bodyX, bodyY, bodyRx, bodyRy, breathe;      // base ellipse; breathe = sine amplitude on ry
  float headDx, headDy, headR, yaw, tilt, headYaw;  // yaw = body facing 0..1; tilt = feature-space radians
  float earL, earR;                                 // per-ear twitch angles
  float tailBase, tailCurl, tailSwish;              // 5-segment chain; swish written by catEval
  float legLift[4], legFwd;                         // [0] near front, [1] far front, [2]/[3] haunches (-x, +x)
  // `pupilD*` are legacy field names. The chibi eyes are solid, so these now offset the white
  // catchlight, not a pupil; the rig presents them under their real visual meaning.
  float eyeOpen, pupilDx, pupilDy, mouthOpen;
};
// Named, because catLerp walks CatPose as a float array and the CAT_TUNE pose-override seam
// indexes one slot per field. Bump this and the static_assert together when a field is added.
static const int CAT_POSE_FLOATS = 25;
static_assert(sizeof(CatPose) == CAT_POSE_FLOATS * sizeof(float),
              "CatPose must stay all-float for catLerp and the CAT_TUNE pose seam");

struct CatKey { float t; CatPose p; };
struct CatPoseDef { uint16_t durMs; CatPlay mode; float yaw; };

// The mathematical muzzle ceiling is ~0.75, but the ART ceiling is much lower. On the x1 rig the
// leading eye is already a sliver around 0.25-0.30 and the face reads as one-eyed profile by 0.45.
// Chibi poses keep both eyes in play, so authored targets stop at this perceptual three-quarter
// limit. The tuning slider still reaches 1.0: full profile remains a useful stress test.
static constexpr float CAT_HEAD_YAW_ART_MAX = 0.24f;
static const CatPoseDef CAT_POSE[CA_COUNT] = {
  // yaw * 90deg is the geometric angle; the apparent turn is stronger because the leading eye
  // foreshortens on its own azimuth before the nose reaches profile.
  /*CA_IDLE*/       { 4000, CP_LOOP, 0.12f },
  /*CA_MEOW*/       { 1600, CP_LOOP, CAT_HEAD_YAW_ART_MAX },
  // Sleeping is the one pose that wants LESS turn, not more: the closed-eye caret is a thin
  // horizontal mark, so at 0.24 the far one foreshortens to a stub on the skull's edge and the
  // face reads one-eyed. 0.12 keeps both carets. catEvalCur overwrites CatPose::headYaw from the
  // render state, so this table -- not the keyframe -- is where a pose's facing actually lives.
  /*CA_SLEEPING*/   { 5000, CP_LOOP, 0.12f },
  // Differentiated rather than all sitting on the ceiling, so the quirk gate's ITCH/STRETCHING
  // every 8-20s don't all present the same face. Licking keeps the ceiling because its head is
  // turned toward its own shoulder and wants the strongest turn available; stretching drops to
  // near-frontal because the pose's whole content is the body silhouette, and a turned head
  // fights it; itch sits between, where the reversing tilt stays legible.
  /*CA_LICKING*/    { 1900, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_STRETCHING*/ { 2600, CP_ONCE, 0.08f },
  /*CA_ITCH*/       { 1600, CP_ONCE, 0.16f },
  // Mood gestures. Kneading is the content cat's soft fidget; begging interrupts the needy
  // meow loop; yawning is a one-way bridge into the sleepy loaf rather than an idle-returning
  // quirk. Appending them preserves the original animation ids used by the tuning rig.
  /*CA_KNEADING*/   { 2400, CP_ONCE, 0.08f },
  /*CA_BEGGING*/    { 1900, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_YAWNING*/    { 2600, CP_ONCE, 0.12f },
  // Second mood pass. PLEASE keeps the needy face at the art ceiling while joining both paws;
  // TAIL_HUG stays frontal so the returning tail remains legible beside the torso; NODDING
  // shares yawning's quiet facing and likewise hands off directly to the sleeping loaf.
  /*CA_PLEASE*/     { 2200, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_TAIL_HUG*/   { 2800, CP_ONCE, 0.08f },
  /*CA_NODDING*/    { 3200, CP_ONCE, 0.12f },
  // Third pass, face-led. SLOWBLINK is the content cat's affection signal and must play slow
  // enough to be unmistakably deliberate next to the 120 ms reflex blink; SNIFF keeps the needy
  // face at the ceiling like the paw asks it rotates with; WAKING is the sleep bridge reversed,
  // so it shares the quiet 0.12 facing with yawning/nodding on both sides of the loaf.
  /*CA_SLOWBLINK*/  { 2400, CP_ONCE, 0.12f },
  /*CA_SNIFF*/      { 1600, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_WAKING*/     { 3000, CP_ONCE, 0.12f },
  // Fourth pass, grooming. FACE repeats lick-paw, dip-head, swipe-cheek three times; FORELEG
  // holds that arm across the chest for four short tongue beats; BELLY drops into a profile
  // recline, lifts one hind leg and curls the face into the exposed flank.
  /*CA_GROOM_FACE*/    { 5200, CP_ONCE, 0.20f },
  /*CA_GROOM_FORELEG*/ { 3800, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_GROOM_BELLY*/   { 4600, CP_ONCE, 0.12f },
  // Fifth pass, the adoring gaze -- face square to the viewer, everything happens in the eyes.
  /*CA_ADORE*/         { 2600, CP_ONCE, 0.10f },
  // Sixth pass: one unmistakable beat for each mood. The bunt leans its cheek into the viewer;
  // the protest keeps both paws grounded while stamping and lashing; curl-up is a third exact
  // standing-to-loaf bridge, quieter than the yawn and nod entries.
  /*CA_HEAD_BUNT*/     { 2800, CP_ONCE, 0.10f },
  /*CA_PROTEST*/       { 2200, CP_ONCE, CAT_HEAD_YAW_ART_MAX },
  /*CA_CURL_UP*/       { 2800, CP_ONCE, 0.12f },
  // Seventh pass, the play beat. The head stays near-frontal because the pounce's content is the
  // BODY silhouette -- the same reasoning that keeps stretching at 0.08 -- and a turned head
  // would fight the crouch. The body yaw that makes the haunches read lives in the keyframes.
  /*CA_POUNCE*/        { 3000, CP_ONCE, 0.10f },
};

struct CatRenderState {
  CatAnim cur;                    // no `prev` — blendFrom replaces it (spec §Transitions)
  float   phase;                  // 0..1 within cur, per CAT_POSE[cur].mode
  CatPose blendFrom;              // FROZEN evaluated pose; the blend source
  float   blend;                  // 0..1; at 1.0 blendFrom is ignored
  float   aBreathe, aTail;        // free-running accumulators, RADIANS
  float   blinkNextMs, blinkT;    // scheduled blink
  float   twitchNextMs, twitchT;  // scheduled whisker twitch — blink's rarer sibling
  float   headYaw, headYawTarget; // slew-limited, independent of body yaw
  bool    mirror;                 // scene-facing flip, decided once at scene entry (see CatRaster)
};

struct CatPreset {
  float bodyChub, headSize, earLen, earPoint, tailLen, tailFluff, eyeShape;
  uint8_t marking;                // 0 none, 1 stripes, 2 patch, 3 stripes+blaze (light chest)
  uint8_t flat, outline;          // sticker pass (spec): collapsed body ramp / silhouette rim
  float breatheRate, tailRate;    // radians per second
  uint16_t pal[16];               // RGB565; slot meanings = the CI_* enum
};

constexpr uint16_t catRGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Ramp construction: scale an RGB565 toward black (catDim) or toward white (catLit), per
// channel at its own bit depth. A preset authors ONE fur colour and ONE accent colour; the
// four bands of each ramp derive from them, so adding a variant is two colour decisions rather
// than eight, and no preset can ship a ramp that isn't monotonic.
constexpr uint16_t catDim(uint16_t c, float k) {
  return (uint16_t)(((int)(((c >> 11) & 0x1F) * k) << 11) |
                    ((int)(((c >>  5) & 0x3F) * k) <<  5) |
                     (int)(( c        & 0x1F) * k));
}
constexpr uint16_t catLit(uint16_t c, float k) {   // k of the remaining headroom to white
  return (uint16_t)(((int)(((c >> 11) & 0x1F) + (31 - ((c >> 11) & 0x1F)) * k) << 11) |
                    ((int)(((c >>  5) & 0x3F) + (63 - ((c >>  5) & 0x3F)) * k) <<  5) |
                     (int)(( c        & 0x1F) + (31 - ( c        & 0x1F)) * k));
}
// band 1 is the authored colour: the lit crescent goes ABOVE it, the two shadow steps below.
#define CAT_RAMP4(c) catLit(c, 0.34f), (c), catDim(c, 0.70f), catDim(c, 0.48f)

// Universal leg rig. These are skeleton proportions, not coat/preset choices. At yaw=0 the
// rear pair is outside the fore pair; at yaw=1 the anatomical fore/aft axis projects onto x,
// putting the rear pair under the tail/rump and the fore pair under the chest/head.
// CAT_TUNE makes the uncertain profile proportions live controls without bloating CatPreset.
static const float CAT_REAR_SPREAD = 0.67f;
static const float CAT_FORE_SPREAD = 0.19f;
// At true profile the side axis points into the screen, so siblings in the same anatomical plane
// must project onto one silhouette. A nonzero residue kept two full forelegs visible until yaw
// 0.95 and left detached rear-toe clusters. Zero merges them continuously as profile approaches.
static const float CAT_SIDE_PROFILE = 0.0f;
#ifdef CAT_TUNE
static float catLegRearX = -0.40f, catLegFrontX = 0.40f, catLegHock = 0.16f;
static float catLegRearSet = 3.0f, catLegPawScale = 0.86f, catLegGround = 6.0f;
static float catEarInner = 0.5f;    // inner-ear width as a fraction of the available base width
static float catEyeK = 0.592f;      // front-on eye x as a fraction of hr; was a baked 0.62
static float catEarYawGain = 0.0f;  // see the measurement beside the #else copy
#else
static const float catLegRearX = -0.40f, catLegFrontX = 0.40f, catLegHock = 0.16f;
static const float catLegRearSet = 3.0f, catLegPawScale = 0.86f, catLegGround = 6.0f;
static const float catEarInner = 0.5f;
static const float catEyeK = 0.592f;
// Ears do not yaw at all, and 0 is a measurement, not a stylistic shrug. The inner ear does not
// FORESHORTEN as the ear turns -- it SNAPS OFF, because catDrawInnerEar bails on `irad > avail`
// (no room for pink's own corner rounding) while the outer ear is still at ~65% of its front-on
// area. So any nonzero gain buys a little ear parallax and pays for it with a discontinuous
// one-pink-one-bare face somewhere in preset space. Swept earLen x earPoint x headSize (240
// combos, 194 of which have a leading inner ear front-on at all), counting how many LOSE it by
// CAT_HEAD_YAW_ART_MAX: gain 0.00 -> 0 broken, 0.05 -> 7, 0.10 -> 11, 0.15 -> 17, 0.20 -> 22,
// 0.30 -> 38. Monotone, and only 0 is clean. CAT_PRESET[0] happens to survive to yaw 0.84 at
// 0.20, which is why this shipped -- one preset is not the test, and six are planned.
// Pinned by test_leading_inner_ear_survives_the_art_ceiling. If a future artist wants ear
// parallax back, the thing to fix first is the snap, not this number.
static const float catEarYawGain = 0.0f;
#endif

// One shipped preset for bring-up; Task "six presets" replaces this table.
static const CatPreset CAT_PRESET[] = {
  // 0 orange tabby: stripes+blaze, chibi build
  // earLen/tailLen/tailFluff are cells -> doubled with the re-grid; chub/headSize/eyeShape
  // are ratios and breathe/tailRate are rad/s, so neither scales.
  // Retuned for the chibi brief once the lighting could carry it: rounder body, bigger head,
  // much bigger eyes, short broad ears so the skull dominates. earLen dropped 8.0 -> 6.0 with
  // the wider ear base (catEarTri), and earPoint 0.5 keeps that reach triangular instead of
  // reducing it to the round nubs the rig review caught at earLen 2.5. Previous values were
  // 0.82/1.24/9.6/10.0/4.2/1.22 (2026-07-29 review pass) — restore those for the soberer cat.
  { 1.18f, 1.38f, 6.0f, 0.5f, 10.0f, 4.8f, 1.46f, 3, 0, 0, 1.1f, 1.35f,
    // blitCatGrid is a bare pal[idx] lookup that skips only CI_TRANS, so any slot the renderer
    // can emit must be filled here — an unset one blits black rather than degrading gracefully.
    { 0, CAT_RAMP4(catRGB(232,151,63)),          // 1-4  fur
         CAT_RAMP4(catRGB(163,84,22)),           // 5-8  accent (stripes)
      catRGB(0,0,0),                             // 9    eye — flat BLACK. A near-black tint reads as
                                                 //      a murky hole against the screen background;
                                                 //      only true black holds the crisp chibi edge.
                                                 //      Safe as pal[9]=0: blitCatGrid skips on INDEX
                                                 //      == CI_TRANS, never on colour.
      catDim(catRGB(232,151,63), 0.62f),         // 10   outline — artist call: closer to the fill
                                                 //      than the 0.48 shadow band. Derives from the
                                                 //      authored fur base by hand; keep in sync if
                                                 //      the fur color changes.
      catRGB(229,143,162),                       // 11   nose
      catRGB(226,158,140),                       // 12   inner ear
      catRGB(0,0,0),                             // 13   mouth — true black survives the tiny panel;
                                                 //      the previous dark brown vanished in fur shade
      catRGB(255,255,255),                       // 14   catchlight
      catRGB(240,120,130) } },                   // 15   blush
};
constexpr unsigned CAT_PRESET_N = sizeof(CAT_PRESET) / sizeof(CAT_PRESET[0]);

// ---- pose keyframes: rig-tuned starting points -----------------------------
// Lengths are grid cells, so every one of these doubled with the 112x104 re-grid.
// Angles (tailBase, tailCurl, tilt) and the 0..1 normals (yaw, eyeOpen) did not.
constexpr CatPose P_IDLE = {
  /*bodyX*/60, /*bodyY*/75, /*bodyRx*/28, /*bodyRy*/22, /*breathe*/1.2f,
  /*headDx*/0, /*headDy*/-30, /*headR*/20, /*yaw*/0.3f, /*tilt*/0, /*headYaw*/0.3f,
  /*earL*/0, /*earR*/0,
  /*tailBase*/2.6f, /*tailCurl*/-0.20f, /*tailSwish*/0,   // -0.35 curled the tip into the cheek
  /*legLift*/{0, 0, 0, 0}, /*legFwd*/0,
  /*eyeOpen*/1, /*pupilDx*/0, /*pupilDy*/0, /*mouthOpen*/0,
};
constexpr CatKey K_IDLE[] = { {0, P_IDLE}, {1, P_IDLE} };   // idle motion comes from the accumulators

// A C++11-compatible constexpr builder (one return statement, no mutation) keeps meow as a
// P_IDLE delta without dynamically initializing a global pose on the device. The body and paws
// stay planted: this is the needy base animation, so the appeal comes from the face/head beat and
// not from a full-body reaction competing with the separate lick/stretch/itch poses.
constexpr CatPose catMeowPose(float headDy, float tilt, float earL, float earR,
                              float eyeOpen, float mouthOpen) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}

// One 1.6 s meow:
//   0.00-0.14  small chin tuck / anticipation
//   0.14-0.30  head lifts into the call
//   0.30-0.66  held vowel with a subtle mouth/ear change, not a frozen cel
//   0.66-1.00  long soft recovery to the exact idle pose for a seamless loop
// mouthOpen stays below the rig's 0.50 all-head-size art limit.
constexpr CatKey K_MEOW[] = {
  { 0.00f, P_IDLE },
  { 0.14f, catMeowPose(-29.5f,  0.04f, -0.04f,  0.03f, 0.92f, 0.00f) },
  { 0.30f, catMeowPose(-32.0f, -0.11f,  0.10f, -0.06f, 0.68f, 0.42f) },
  { 0.52f, catMeowPose(-31.5f, -0.08f,  0.06f, -0.03f, 0.74f, 0.34f) },
  { 0.66f, catMeowPose(-32.0f, -0.10f,  0.08f, -0.05f, 0.70f, 0.40f) },
  { 1.00f, P_IDLE },
};

// Low loaf: body settled, paws tucked under it, head forward on the +x end, tail curled around
// the rump. Four things here are constrained rather than chosen, and each one has a wrong-looking
// neighbour a small step away:
//   headDx +18 (NOT 0, and NOT negative). The renderer's front is +x -- front legs sit at
//     +catLegFrontX*brx and the tail root at bx-brx+4 -- so a head at -x puts the face on the
//     cat's own rump. At headDx 0 the head is CONCENTRIC with the body (headBodyOverlap 1106 of
//     a ~1000-cell head) and the whole cat rasterizes as one potato with a face on its rim.
//     +18 leaves an overlap of 400: enough to read as a head resting on a chest, not a snowman.
//   headDy -22 (was -10). The head has to clear the body's top edge or there is no silhouette;
//     -10 put the head's CENTRE six cells below it.
//   bodyX 56 / bodyRx 26. bodyX 60 clips the head off the right edge (H1) and a wider body walks
//     the tail root left into the fold. This pair is the widest loaf that keeps both ends on grid.
//   tailBase 3.2 / tailCurl 0.8. The root leaves flat along the ground, then five short joints
//     complete the turn and carry the tip back over the near side of the rump. 0.75 falls just
//     short of the returning-tip depth split; 0.9 closes into a tight striped ball that reads as
//     another paw. The root still has the idle/meow face backstop, but distal joints deliberately
//     do not -- that separation is what also gives lick/stretch/itch room for authored curls.
// eyeOpen 0, not the 0.1 "bar": at 0 catRender draws the caret, which is the content-cat closed
// eye. The 0.1 bar renders as two black slabs at this head size.
constexpr CatPose catSleepPose(float headDy, float tilt, float earL, float earR,
                               float legLift0 = 0.55f) {
  return {
    56.0f, 84.0f, 26.0f, 19.0f, 2.0f,
    18.0f, headDy, 18.0f, 0.90f, tilt, 0.12f,
    earL, earR,
    3.2f, 0.8f, 0.0f,                           // grounded root, returning tip; see note above
    { legLift0, 0.55f, 0.55f, 0.55f }, P_IDLE.legFwd, // paws tucked -- a loaf has no standing legs
    0.0f, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatPose P_SLEEP = catSleepPose(-22.0f, 0.18f, -0.10f,  0.06f);
// The dream flexes a tucked forepaw along with the ear flick: 0.47 stays in the tucked band
// (above the loaf's 0.4 floor, below CAT_LEG_NEAR_GATE) so the paw stirs without changing plane.
constexpr CatPose P_SLEEP_DREAM = catSleepPose(-21.5f, 0.16f, 0.10f, -0.02f, 0.47f);

// Breathing supplies the continuous slow motion. The phase curve deliberately holds almost
// still, then gives one quick asymmetric dream-ear flick every five seconds.
constexpr CatKey K_SLEEP[] = {
  { 0.00f, P_SLEEP },
  { 0.55f, P_SLEEP },
  { 0.62f, P_SLEEP_DREAM },
  { 0.70f, P_SLEEP },
  { 1.00f, P_SLEEP },
};

// LICKING, ITCH and STRETCHING are all authored as HEAD-AND-BODY poses, with no paw raised to
// the face, because the leg rig cannot put one there and no keyframe value changes that:
//   - rear paws are drawn BEHIND the body, and R.pawY bottoms out at ground-24 = 76 against a
//     torso spanning 53..97, so a raised hind paw is occluded at every legLift in [0,1].
//   - front paws draw over the chest, but F.pawY bottoms at ground-22 = 81 while the head's
//     underside sits at 65. The paw stops 16 cells short of the chin, and because only the paw
//     moves the shank collapses into a nub on the belly rather than reading as a raised arm.
//   - legFwd is the only horizontal channel and it moves BOTH forelegs together, so it cannot
//     reach one paw toward anything.
// A literal groom or scratch therefore needs new geometry (a jointed foreleg with its own reach),
// not tuning. What IS expressive here is the head: dip, cock, ears, eyes, mouth. So licking is a
// head dipped to the shoulder and itch is a head shaken against it, which read as the actions
// without claiming the paw. Both are noted for the owner as substitutions, not as the drawing.

// Head dips toward the near shoulder and works there. tilt does the turning: past ~0.8 the
// feature-space rotation swings the mouth and whisker fan far enough off the round skull that the
// face stops reading as a face, so the dip tops out below that.
constexpr CatPose catLickPose(float headDx, float headDy, float tilt,
                              float eyeOpen, float mouthOpen, float legLift0) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    -0.06f, 0.04f,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { legLift0, P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}

// Two grooming passes at the shoulder, not one: a single dip reads as a dropped head. eyeOpen
// travels 1 -> 0 through the "bar" band on the way down, which is what a blink already does, so
// it is the transition and not a held look.
// mouthOpen stays at/below 0.22 here, well under the 0.50 art limit meow uses. The open mouth is
// a dark oval, and on a head this size anything past ~0.25 stops reading as a mouth and starts
// reading as a hole in the muzzle -- especially at high tilt, where it sits off-centre.
// Keyframed eyeOpen is only ever 0 or >= 0.6 for the same reason sleeping is: the low positive
// band is the "bar", which is a transition to pass through and not a value to hold.
// The paw comes up FIRST and stays at the muzzle; the head then bobs into it twice. That order
// is the whole read -- a head that dips before the paw arrives is grooming a shoulder, which is
// what this pose was before the foreleg could reach. Every raised value clears CAT_LEG_NEAR_GATE
// so the arm never changes painter plane mid-pose; only the return to idle crosses it.
constexpr CatKey K_LICK[] = {
  { 0.00f, P_IDLE },
  { 0.18f, catLickPose(-4.0f, -26.0f, 0.40f, 0.70f, 0.08f, 0.90f) },
  { 0.36f, catLickPose(-6.0f, -24.0f, 0.62f, 0.00f, 0.20f, 0.94f) },
  { 0.50f, catLickPose(-5.0f, -26.0f, 0.50f, 0.00f, 0.05f, 0.92f) },
  { 0.66f, catLickPose(-6.0f, -24.0f, 0.64f, 0.00f, 0.20f, 0.95f) },
  { 0.84f, catLickPose(-2.0f, -28.0f, 0.22f, 0.75f, 0.02f, 0.80f) },
  { 1.00f, P_IDLE },
};

// legLift[2] is the -x hind leg, and every value here is above CAT_LEG_NEAR_GATE so the leg
// stays on the near plane for the whole scratch: crossing the gate mid-pose flips its painter
// depth, and at gate height the paw sits inside the torso, so the crossing is visible. Only the
// final return to idle crosses it, where a one-frame depth change is buried in the whole body
// moving at once.
// The head LEANS toward the paw and holds; it does not shake. An alternating cock was the right
// read while there was no leg to look at, but next to a working one it fights the beat -- the
// fast motion belongs to the limb doing the scratching. Ears still counter-rotate, since a
// symmetric pair reads as a shrug.
constexpr CatPose catItchPose(float headDy, float tilt, float earL, float earR,
                              float eyeOpen, float legLift2) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], legLift2, P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_ITCH[] = {
  { 0.00f, P_IDLE },
  { 0.14f, catItchPose(-28.0f, 0.42f, -0.50f, 0.28f, 0.00f, 0.86f) },
  { 0.28f, catItchPose(-27.0f, 0.30f, -0.34f, 0.18f, 0.00f, 0.70f) },
  { 0.42f, catItchPose(-28.0f, 0.46f, -0.55f, 0.32f, 0.00f, 0.94f) },
  { 0.56f, catItchPose(-27.0f, 0.30f, -0.34f, 0.18f, 0.00f, 0.70f) },
  { 0.70f, catItchPose(-28.0f, 0.44f, -0.52f, 0.30f, 0.00f, 0.92f) },
  { 0.86f, catItchPose(-29.0f, 0.12f, -0.12f, 0.08f, 0.70f, 0.78f) },
  { 1.00f, P_IDLE },
};

// The only pose that reshapes the BODY. The forelegs push out on legFwd while the torso goes long
// and shallow and settles toward the floor, which is the front-down half of a stretch; the rear
// half is unavailable for the same reason licking has no paw. The tail comes up off the rump --
// tailBase toward 1.8 is the near-vertical end of the root's own backstop band, so this stays
// inside the clamp rather than relying on the freed distal joints.
constexpr CatPose catStretchPose(float bodyX, float bodyY, float bodyRx, float bodyRy,
                                 float headDx, float headDy, float legFwd, float tailBase,
                                 float eyeOpen, float mouthOpen) {
  return {
    bodyX, bodyY, bodyRx, bodyRy, P_IDLE.breathe,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, 0.0f, P_IDLE.headYaw,
    0.05f, -0.05f,
    tailBase, -0.10f, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
// headDy tracks bodyY DOWN, it does not stay put. Dropping the torso to bodyY 90 without it puts
// the body's top edge at 74 and the head's centre at 76 -- the concentric potato the sleeping
// pose was rebuilt to escape. Holding roughly idle's clearance (head centre ~10 cells above the
// body's top edge) keeps a neck while the whole cat still sits 19 cells lower on screen, which is
// where the lowering actually reads from.
constexpr CatKey K_STRETCH[] = {
  { 0.00f, P_IDLE },
  { 0.26f, catStretchPose(58.0f, 86.0f, 32.0f, 17.0f,  4.0f, -24.0f,  8.0f, 2.20f, 0.75f, 0.00f) },
  // The hold is the whole point of the pose, so it gets 0.46-0.68 of a 2600 ms clip (~570 ms)
  // rather than the 0.46-0.62 of 2200 (~350) it shipped with. Lengthening the clip alongside the
  // span is what keeps the reach and the recovery at their original real-time pace; widening the
  // span inside 2200 would have paid for the hold by making both snappier.
  { 0.46f, catStretchPose(54.0f, 90.0f, 36.0f, 15.0f, 10.0f, -26.0f, 16.0f, 1.80f, 0.00f, 0.20f) },
  { 0.68f, catStretchPose(54.0f, 90.0f, 36.0f, 15.0f, 10.0f, -26.0f, 16.0f, 1.80f, 0.00f, 0.28f) },
  { 0.82f, catStretchPose(58.0f, 84.0f, 31.0f, 18.0f,  3.0f, -25.0f,  6.0f, 2.30f, 0.80f, 0.04f) },
  { 1.00f, P_IDLE },
};

// Content mood: a small, self-contained knead. Both forepaws stay below the near-plane gate,
// so they remain grounded limbs instead of popping into the face-reaching arm geometry. The
// alternating lift and one-cell weight shift carry the beat; the closed-eye caret makes it read
// as comfort rather than another demand for attention.
constexpr CatPose catKneadPose(float bodyX, float headDy, float tilt, float earL, float earR,
                               float legLift0, float legLift1, float eyeOpen) {
  return {
    bodyX, P_IDLE.bodyY + 1.0f, P_IDLE.bodyRx, P_IDLE.bodyRy, 1.8f,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    2.45f, -0.35f, P_IDLE.tailSwish,
    { legLift0, legLift1, P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_KNEAD[] = {
  { 0.00f, P_IDLE },
  { 0.16f, catKneadPose(60.0f, -29.5f,  0.03f,  0.08f, -0.05f, 0.08f, 0.08f, 0.70f) },
  { 0.30f, catKneadPose(59.0f, -29.0f, -0.05f,  0.12f, -0.08f, 0.34f, 0.04f, 0.00f) },
  { 0.44f, catKneadPose(60.0f, -29.5f,  0.02f,  0.08f, -0.05f, 0.10f, 0.10f, 0.00f) },
  { 0.58f, catKneadPose(61.0f, -29.0f,  0.06f,  0.04f, -0.12f, 0.04f, 0.34f, 0.00f) },
  { 0.72f, catKneadPose(60.0f, -29.5f, -0.02f,  0.08f, -0.05f, 0.10f, 0.10f, 0.00f) },
  { 0.84f, catKneadPose(59.0f, -29.0f, -0.05f,  0.12f, -0.08f, 0.30f, 0.04f, 0.65f) },
  { 1.00f, P_IDLE },
};

// Needy mood: one forepaw comes up twice while the face stays upright and looks out at the
// viewer. The same reach geometry used by licking lands the paw near the muzzle, but the open
// eyes, vocal mouth and lack of a shoulder dip turn it into an unmistakable "hey, you" gesture.
// Every nonzero raised key stays above CAT_LEG_NEAR_GATE so its painter plane cannot flip mid-beat.
constexpr CatPose catBegPose(float headDy, float tilt, float earL, float earR,
                             float tailBase, float legLift0, float eyeOpen, float mouthOpen) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    1.5f, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, -0.08f, P_IDLE.tailSwish,
    { legLift0, P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    2.0f,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_BEG[] = {
  { 0.00f, P_IDLE },
  { 0.14f, catBegPose(-29.0f,  0.04f, -0.08f,  0.06f, 2.35f, 0.64f, 0.82f, 0.00f) },
  { 0.30f, catBegPose(-32.0f, -0.15f,  0.14f, -0.10f, 2.15f, 0.86f, 0.72f, 0.42f) },
  { 0.44f, catBegPose(-30.0f, -0.08f,  0.04f, -0.03f, 2.25f, 0.64f, 1.00f, 0.08f) },
  { 0.58f, catBegPose(-31.5f, -0.14f,  0.12f, -0.08f, 2.10f, 0.92f, 0.70f, 0.38f) },
  { 0.74f, catBegPose(-31.0f, -0.10f,  0.08f, -0.05f, 2.20f, 0.84f, 0.76f, 0.14f) },
  { 0.86f, catBegPose(-29.5f, -0.02f,  0.02f, -0.02f, 2.40f, 0.64f, 0.90f, 0.00f) },
  { 1.00f, P_IDLE },
};

// Sleepy mood: the cat does not simply snap from standing into the profile loaf. It lifts its
// chin into one long closed-eye yawn, folds the paws, then finishes on the EXACT sleeping pose.
// That exact endpoint lets the reaction hand off to CA_SLEEPING without a second silhouette
// change. Mouth opening stays at the renderer's measured 0.50 art ceiling.
constexpr CatPose catYawnPose(float bodyX, float bodyY, float bodyRx, float bodyRy,
                              float headDx, float headDy, float headR, float yaw, float tilt,
                              float earL, float earR, float tailBase, float tailCurl,
                              float legTuck, float eyeOpen, float mouthOpen) {
  return {
    bodyX, bodyY, bodyRx, bodyRy, P_IDLE.breathe,
    headDx, headDy, headR, yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { legTuck, legTuck, legTuck, legTuck }, P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_YAWN[] = {
  { 0.00f, P_IDLE },
  { 0.16f, catYawnPose(60.0f, 76.0f, 28.0f, 22.0f,  0.0f, -29.5f, 20.0f, 0.32f, 0.04f,
                       -0.10f,  0.08f, 2.55f, -0.16f, 0.04f, 0.65f, 0.00f) },
  { 0.34f, catYawnPose(60.0f, 77.0f, 28.0f, 21.0f,  1.0f, -31.0f, 20.0f, 0.40f, -0.08f,
                       -0.22f,  0.16f, 2.45f, -0.08f, 0.10f, 0.00f, 0.50f) },
  { 0.56f, catYawnPose(59.0f, 79.0f, 28.0f, 21.0f,  4.0f, -29.0f, 20.0f, 0.52f, 0.10f,
                       -0.18f,  0.12f, 2.65f,  0.12f, 0.20f, 0.00f, 0.46f) },
  { 0.74f, catYawnPose(58.0f, 82.0f, 27.0f, 20.0f, 10.0f, -24.0f, 19.0f, 0.72f, 0.16f,
                       -0.12f,  0.08f, 2.95f,  0.48f, 0.38f, 0.00f, 0.10f) },
  { 1.00f, P_SLEEP },
};

// Needy mood: the single asking paw becomes two joined "please" paws. The important plane rule
// is the same one begging relies on: every authored nonzero lift is above CAT_LEG_NEAR_GATE, so
// neither arm changes from chest-underlay to face-overlay during a held beat. The first paw leads,
// the second joins it, and the pair gives one small squeeze before both return together.
constexpr CatPose catPleasePose(float bodyX, float headDy, float tilt, float earL, float earR,
                                float tailBase, float legLift0, float legLift1,
                                float eyeOpen, float mouthOpen) {
  return {
    bodyX, P_IDLE.bodyY + 0.5f, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    1.5f, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, -0.08f, P_IDLE.tailSwish,
    { legLift0, legLift1, P_IDLE.legLift[2], P_IDLE.legLift[3] },
    2.0f,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_PLEASE[] = {
  { 0.00f, P_IDLE },
  { 0.14f, catPleasePose(60.0f, -29.0f,  0.04f, -0.08f,  0.06f,
                         2.35f, 0.66f, 0.00f, 0.90f, 0.00f) },
  { 0.30f, catPleasePose(60.0f, -31.5f, -0.12f,  0.12f, -0.08f,
                         2.15f, 0.88f, 0.66f, 0.72f, 0.30f) },
  { 0.48f, catPleasePose(59.5f, -32.0f, -0.15f,  0.14f, -0.10f,
                         2.08f, 0.92f, 0.90f, 0.70f, 0.20f) },
  { 0.64f, catPleasePose(59.0f, -31.5f, -0.10f,  0.12f, -0.08f,
                         2.08f, 0.96f, 0.94f, 0.65f, 0.34f) },
  { 0.80f, catPleasePose(60.5f, -31.0f,  0.06f, -0.04f,  0.08f,
                         2.20f, 0.90f, 0.88f, 0.80f, 0.10f) },
  { 0.90f, catPleasePose(60.0f, -29.5f,  0.02f,  0.02f, -0.02f,
                         2.40f, 0.66f, 0.66f, 0.92f, 0.00f) },
  { 1.00f, P_IDLE },
};

// Content mood: the five-joint tail folds forward around the rump and finishes against the
// shoulder while the cat closes its eyes and gives two tiny purr-squishes. Unlike kneading this
// owns the outer silhouette, not the paws. The reduced accumulator gain in catEvalCur keeps the
// free-running idle swish from pulling the authored curl away from the cheek.
constexpr CatPose catTailHugPose(float bodyX, float bodyY, float bodyRx, float bodyRy,
                                 float headDx, float headDy, float tilt, float earL, float earR,
                                 float tailBase, float tailCurl,
                                 float legLift0, float legLift1, float eyeOpen) {
  return {
    bodyX, bodyY, bodyRx, bodyRy, 2.2f,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { legLift0, legLift1, P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_TAIL_HUG[] = {
  { 0.00f, P_IDLE },
  { 0.18f, catTailHugPose(60.0f, 76.0f, 28.5f, 22.5f, -0.5f, -29.5f,  0.03f,
                           0.08f, -0.05f, 2.38f, -0.38f, 0.08f, 0.08f, 0.70f) },
  { 0.36f, catTailHugPose(59.0f, 76.0f, 29.0f, 23.0f, -1.0f, -28.5f,  0.06f,
                           0.12f, -0.10f, 2.20f, -0.60f, 0.10f, 0.10f, 0.00f) },
  { 0.52f, catTailHugPose(60.0f, 77.0f, 29.0f, 24.0f, -1.0f, -29.0f, -0.04f,
                           0.06f, -0.12f, 2.18f, -0.62f, 0.12f, 0.12f, 0.00f) },
  { 0.66f, catTailHugPose(59.0f, 76.0f, 29.0f, 23.0f, -1.0f, -28.5f,  0.05f,
                           0.12f, -0.08f, 2.22f, -0.58f, 0.10f, 0.10f, 0.00f) },
  { 0.82f, catTailHugPose(60.0f, 76.0f, 28.5f, 22.5f, -0.5f, -29.5f,  0.00f,
                           0.05f, -0.04f, 2.40f, -0.38f, 0.05f, 0.05f, 0.72f) },
  { 1.00f, P_IDLE },
};

// Sleepy mood: an alternate standing-to-loaf bridge. Three head drops get progressively deeper
// while the torso settles, the paws tuck and the body turns toward the sleeping profile. The
// little rebounds keep this from reading as one slow squash. Like yawning, the exact P_SLEEP
// endpoint is load-bearing: the following CA_SLEEPING frame must not make a second silhouette.
constexpr CatPose catNodPose(float bodyX, float bodyY, float bodyRx, float bodyRy,
                             float headDx, float headDy, float headR, float yaw, float tilt,
                             float earL, float earR, float tailBase, float tailCurl,
                             float legTuck, float eyeOpen) {
  return {
    bodyX, bodyY, bodyRx, bodyRy, P_IDLE.breathe,
    headDx, headDy, headR, yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { legTuck, legTuck, legTuck, legTuck }, P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_NOD[] = {
  { 0.00f, P_IDLE },
  { 0.12f, catNodPose(60.0f, 76.0f, 28.0f, 22.0f,  0.0f, -28.0f, 20.0f, 0.32f,  0.08f,
                       -0.05f,  0.04f, 2.55f, -0.15f, 0.04f, 0.65f) },
  { 0.24f, catNodPose(60.0f, 77.0f, 28.0f, 21.5f,  1.0f, -23.0f, 20.0f, 0.38f,  0.22f,
                       -0.14f,  0.12f, 2.50f, -0.08f, 0.08f, 0.00f) },
  { 0.36f, catNodPose(60.0f, 77.0f, 28.0f, 21.5f,  2.0f, -28.5f, 20.0f, 0.42f, -0.04f,
                        0.08f, -0.06f, 2.55f,  0.00f, 0.12f, 0.72f) },
  { 0.50f, catNodPose(59.5f, 79.0f, 28.0f, 21.0f,  4.0f, -23.0f, 20.0f, 0.50f,  0.24f,
                       -0.18f,  0.14f, 2.70f,  0.14f, 0.18f, 0.00f) },
  { 0.62f, catNodPose(59.0f, 79.0f, 27.5f, 20.5f,  7.0f, -27.0f, 19.5f, 0.60f,  0.02f,
                        0.06f, -0.04f, 2.80f,  0.28f, 0.24f, 0.65f) },
  { 0.76f, catNodPose(58.0f, 82.0f, 27.0f, 20.0f, 11.0f, -22.5f, 19.0f, 0.72f,  0.24f,
                       -0.14f,  0.10f, 3.00f,  0.48f, 0.40f, 0.00f) },
  { 1.00f, P_SLEEP },
};

// Content mood, face only: the deliberate cat-affection slow blink. The body is pinned to
// P_IDLE in every key -- any drift would read as the start of a gesture that never arrives --
// and the closed caret is HELD across a keyed span, which is what separates it from the 120 ms
// reflex blink that passes through the same shape. The tiny tilt walk keeps the hold alive.
constexpr CatPose catSlowBlinkPose(float headDy, float tilt, float earL, float earR,
                                   float eyeOpen) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_SLOWBLINK[] = {
  { 0.00f, P_IDLE },
  { 0.22f, catSlowBlinkPose(-29.5f, 0.04f,  0.04f, -0.03f, 0.65f) },
  { 0.38f, catSlowBlinkPose(-29.0f, 0.08f,  0.06f, -0.05f, 0.00f) },
  { 0.62f, catSlowBlinkPose(-28.8f, 0.10f,  0.05f, -0.04f, 0.00f) },
  { 0.80f, catSlowBlinkPose(-29.6f, 0.04f,  0.02f, -0.02f, 0.90f) },
  { 1.00f, P_IDLE },
};

// Needy mood, the non-vocal ask: the nose leads. No paw comes up (that is begging/please
// territory) and the mouth stays shut; the read is the lean toward the viewer, several quick
// headDy bobs, forward ears, and pupils dropped toward the treat below the cat.
constexpr CatPose catSniffPose(float headDx, float headDy, float tilt, float earL, float earR,
                               float pupilDy, float eyeOpen) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_SNIFF[] = {
  { 0.00f, P_IDLE },
  { 0.16f, catSniffPose(2.5f, -28.5f, 0.05f, 0.10f, -0.06f, 0.5f, 0.85f) },
  { 0.32f, catSniffPose(3.0f, -27.5f, 0.02f, 0.14f, -0.08f, 1.5f, 0.70f) },
  { 0.46f, catSniffPose(3.0f, -29.0f, 0.06f, 0.10f, -0.05f, 1.0f, 0.80f) },
  { 0.60f, catSniffPose(3.5f, -27.0f, 0.00f, 0.16f, -0.10f, 2.0f, 0.70f) },
  { 0.78f, catSniffPose(1.5f, -29.5f, 0.03f, 0.06f, -0.03f, 0.0f, 1.00f) },
  { 1.00f, P_IDLE },
};

// Sleepy mood, the bridge OUT of the loaf: yawning's corridor run in reverse with its own
// content. It must start on the exact P_SLEEP so CA_SLEEPING hands off without a second
// silhouette, rise through a closed-eye waking yawn while still low, then pay one visible
// stretch beat (long shallow body, forelegs pushed out) before settling to the exact idle.
constexpr CatKey K_WAKE[] = {
  { 0.00f, P_SLEEP },
  { 0.16f, catYawnPose(58.0f, 82.0f, 27.0f, 20.0f, 10.0f, -24.0f, 19.0f, 0.72f,  0.16f,
                       -0.12f,  0.08f, 2.95f,  0.48f, 0.38f, 0.00f, 0.00f) },
  { 0.34f, catYawnPose(59.0f, 79.0f, 28.0f, 21.0f,  4.0f, -29.0f, 20.0f, 0.52f,  0.10f,
                       -0.20f,  0.14f, 2.65f,  0.12f, 0.20f, 0.00f, 0.42f) },
  { 0.50f, catYawnPose(60.0f, 77.0f, 28.0f, 21.0f,  1.0f, -31.0f, 20.0f, 0.40f, -0.06f,
                       -0.22f,  0.16f, 2.45f, -0.08f, 0.10f, 0.00f, 0.50f) },
  { 0.68f, catStretchPose(58.0f, 86.0f, 32.0f, 17.0f, 4.0f, -24.0f, 8.0f, 2.20f, 0.65f, 0.00f) },
  { 0.84f, catStretchPose(59.0f, 80.0f, 30.0f, 20.0f, 2.0f, -27.0f, 3.0f, 2.40f, 0.90f, 0.00f) },
  { 1.00f, P_IDLE },
};

// Grooming I: three complete cat-wash cycles, not one lick followed by an abstract paw wave.
// Each cycle visibly licks the near forepaw, drops the head and paw apart, sweeps that freshly
// licked paw over the cheek, then holds the contact for a short pause. The renderer steers a
// raised forepaw toward the muzzle; every non-idle lift stays above the near-plane gate so the
// arm cannot pop behind the head between the two halves of the movement.
constexpr CatPose catGroomFacePose(float headDx, float headDy, float tilt,
                                   float eyeOpen, float mouthOpen, float legLift0) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    -0.10f, 0.06f,
    2.45f, -0.32f, P_IDLE.tailSwish,
    { legLift0, P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_GROOM_FACE[] = {
  { 0.00f, P_IDLE },
  { 0.06f, catGroomFacePose(-3.0f, -25.5f, 0.30f, 0.65f, 0.02f, 0.84f) },
  { 0.12f, catGroomFacePose(-6.0f, -26.0f, 0.64f, 0.00f, 0.24f, 0.92f) }, // lick 1
  { 0.18f, catGroomFacePose(-2.0f, -23.0f, 0.12f, 0.00f, 0.00f, 0.68f) }, // head down
  { 0.24f, catGroomFacePose(-7.0f, -24.5f, 0.62f, 0.00f, 0.00f, 0.99f) }, // swipe 1
  { 0.30f, catGroomFacePose(-7.0f, -24.5f, 0.62f, 0.00f, 0.00f, 0.99f) }, // pause
  { 0.38f, catGroomFacePose(-5.5f, -26.0f, 0.62f, 0.00f, 0.24f, 0.93f) }, // lick 2
  { 0.44f, catGroomFacePose(-1.5f, -23.0f, 0.10f, 0.00f, 0.00f, 0.68f) }, // head down
  { 0.50f, catGroomFacePose(-7.0f, -24.5f, 0.64f, 0.00f, 0.00f, 0.99f) }, // swipe 2
  { 0.56f, catGroomFacePose(-7.0f, -24.5f, 0.64f, 0.00f, 0.00f, 0.99f) }, // pause
  { 0.64f, catGroomFacePose(-5.5f, -26.0f, 0.62f, 0.00f, 0.24f, 0.93f) }, // lick 3
  { 0.70f, catGroomFacePose(-1.5f, -23.0f, 0.10f, 0.00f, 0.00f, 0.68f) }, // head down
  { 0.76f, catGroomFacePose(-7.0f, -24.5f, 0.60f, 0.00f, 0.00f, 0.98f) }, // swipe 3
  { 0.82f, catGroomFacePose(-7.0f, -24.5f, 0.60f, 0.00f, 0.00f, 0.98f) }, // pause
  { 0.90f, catGroomFacePose(-2.0f, -27.0f, 0.20f, 0.70f, 0.00f, 0.78f) },
  { 1.00f, P_IDLE },
};

// Grooming II: the foreleg stays presented across the chest while the head makes four compact
// tongue dips along it. A lower lift than face grooming leaves the paw below the muzzle, so the
// repeated mouth-open beats read along the arm instead of as another face wash.
constexpr CatPose catGroomForelegPose(float headDx, float headDy, float tilt,
                                      float eyeOpen, float mouthOpen, float legLift0) {
  return {
    P_IDLE.bodyX - 1.0f, P_IDLE.bodyY + 1.0f, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    -0.08f, 0.04f,
    2.35f, -0.28f, P_IDLE.tailSwish,
    { legLift0, P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    1.0f,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_GROOM_FORELEG[] = {
  { 0.00f, P_IDLE },
  { 0.12f, catGroomForelegPose(-4.0f, -25.0f, 0.38f, 0.70f, 0.00f, 0.72f) },
  { 0.20f, catGroomForelegPose(-7.0f, -22.5f, 0.66f, 0.00f, 0.22f, 0.80f) },
  { 0.28f, catGroomForelegPose(-5.0f, -24.5f, 0.48f, 0.00f, 0.02f, 0.76f) },
  { 0.36f, catGroomForelegPose(-7.0f, -22.5f, 0.66f, 0.00f, 0.22f, 0.82f) },
  { 0.44f, catGroomForelegPose(-5.0f, -24.5f, 0.48f, 0.00f, 0.02f, 0.76f) },
  { 0.52f, catGroomForelegPose(-7.0f, -22.5f, 0.66f, 0.00f, 0.22f, 0.82f) },
  { 0.60f, catGroomForelegPose(-5.0f, -24.5f, 0.48f, 0.00f, 0.02f, 0.76f) },
  { 0.68f, catGroomForelegPose(-7.0f, -22.5f, 0.66f, 0.00f, 0.22f, 0.80f) },
  { 0.78f, catGroomForelegPose(-4.0f, -26.0f, 0.30f, 0.70f, 0.00f, 0.72f) },
  { 1.00f, P_IDLE },
};

// Grooming III: a temporary profile recline, distinct from the sleeping loaf. The body goes
// wider and shallower, one hind leg comes around the flank, and the head sits just above the
// torso with a strong downward roll. Three mouth pulses sell belly licking; closed eyes keep
// the contortion calm rather than distressed. The exact P_IDLE endpoints make this a content
// quirk, not a new mood base.
constexpr CatPose catGroomBellyPose(float headDx, float headDy, float tilt,
                                    float legLift2, float eyeOpen, float mouthOpen) {
  return {
    56.0f, 87.0f, 34.0f, 16.0f, 0.8f,
    headDx, headDy, 18.0f, 0.78f, tilt, 0.12f,
    -0.16f, 0.10f,
    3.15f, 0.72f, 0.0f,
    { 0.48f, 0.48f, legLift2, 0.48f }, 0.0f,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, mouthOpen,
  };
}
constexpr CatKey K_GROOM_BELLY[] = {
  { 0.00f, P_IDLE },
  { 0.18f, catGroomBellyPose( 4.0f, -20.0f, 0.34f, 0.72f, 0.70f, 0.00f) },
  { 0.30f, catGroomBellyPose(-1.0f, -18.5f, 0.68f, 0.94f, 0.00f, 0.20f) },
  { 0.40f, catGroomBellyPose( 1.0f, -19.5f, 0.52f, 0.90f, 0.00f, 0.02f) },
  { 0.50f, catGroomBellyPose(-1.0f, -18.5f, 0.70f, 0.96f, 0.00f, 0.22f) },
  { 0.60f, catGroomBellyPose( 1.0f, -19.5f, 0.52f, 0.90f, 0.00f, 0.02f) },
  { 0.70f, catGroomBellyPose(-1.0f, -18.5f, 0.68f, 0.94f, 0.00f, 0.20f) },
  { 0.82f, catGroomBellyPose( 4.0f, -20.0f, 0.32f, 0.72f, 0.70f, 0.00f) },
  { 1.00f, P_IDLE },
};

// The adoring gaze, straight from the reference art: the cat looks UP at its human -- the
// catchlight rides high in the solid chibi eye (negative pupilDy; sniff's droop is the same
// channel run the other way) -- head cocked into a tilt and lifted, held wide-eyed, then a
// soft half-blink that KEEPS the gaze instead of dropping it. Face-led like the slow blink:
// the body never moves, so the whole read is eyes + tilt, and it survives real screen size.
constexpr CatPose catAdorePose(float headDy, float tilt, float pupilDy, float eyeOpen) {
  return {
    P_IDLE.bodyX, P_IDLE.bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, P_IDLE.breathe,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    0.04f, -0.02f,
    P_IDLE.tailBase, P_IDLE.tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_ADORE[] = {
  { 0.00f, P_IDLE },
  { 0.15f, catAdorePose(-30.8f, 0.08f, -0.6f, 1.00f) },
  { 0.30f, catAdorePose(-31.5f, 0.16f, -1.7f, 1.00f) },   // gaze locks on
  { 0.55f, catAdorePose(-31.3f, 0.13f, -1.6f, 1.00f) },   // held
  { 0.68f, catAdorePose(-31.0f, 0.12f, -1.3f, 0.00f) },   // the melt: a happy-blink caret mid-gaze
                                                          // (0.5 would sit in the banned bar band)
  { 0.80f, catAdorePose(-30.8f, 0.10f, -1.0f, 0.95f) },   // eyes reopen STILL looking up
  { 0.92f, catAdorePose(-30.2f, 0.04f, -0.3f, 1.00f) },
  { 1.00f, P_IDLE },
};

// Content mood: the cat leans one cheek into an offered hand, closes its eyes for the contact,
// then slides upward through the rub before easing back. The motion is deliberately lateral:
// the 2-D rig has no z translation, while a plain head dip is already occupied by grooming.
// Scene mirroring makes the chosen cheek vary between visits without a second animation.
constexpr CatPose catHeadBuntPose(float bodyX, float bodyY, float headDx, float headDy,
                                  float tilt, float earL, float earR,
                                  float tailBase, float tailCurl, float eyeOpen) {
  return {
    bodyX, bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, 1.8f,
    headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { P_IDLE.legLift[0], P_IDLE.legLift[1], P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_HEAD_BUNT[] = {
  { 0.00f, P_IDLE },
  { 0.14f, catHeadBuntPose(60.0f, 75.0f,  1.5f, -31.0f, 0.08f,
                            0.10f, -0.06f, 2.50f, -0.16f, 1.00f) },
  { 0.30f, catHeadBuntPose(59.0f, 76.0f,  5.0f, -29.0f, 0.20f,
                           -0.08f,  0.14f, 2.28f,  0.06f, 0.65f) },
  { 0.46f, catHeadBuntPose(58.0f, 77.0f,  8.0f, -27.5f, 0.30f,
                           -0.16f,  0.18f, 2.16f,  0.20f, 0.00f) },
  { 0.62f, catHeadBuntPose(58.5f, 76.5f,  7.0f, -31.0f, 0.24f,
                           -0.10f,  0.14f, 2.20f,  0.14f, 0.00f) },
  { 0.76f, catHeadBuntPose(59.0f, 76.0f,  5.0f, -30.0f, 0.18f,
                           -0.04f,  0.10f, 2.30f,  0.04f, 0.00f) },
  { 0.88f, catHeadBuntPose(59.5f, 75.5f,  2.0f, -30.0f, 0.08f,
                            0.04f, -0.02f, 2.48f, -0.12f, 0.72f) },
  { 1.00f, P_IDLE },
};

// Needy mood: a tiny, readable protest rather than a fourth pleading pose. Both front paws stay
// BELOW the near-plane gate and stamp in alternation, the body commits its weight to each side,
// the ears pin, and the tail root gives a short lash. A small complaint at each plant keeps the
// gesture tied to hunger without duplicating the raised-paw BEGGING/PLEASE silhouettes.
constexpr CatPose catProtestPose(float bodyX, float bodyY, float headDy, float tilt,
                                 float earL, float earR, float tailBase, float tailCurl,
                                 float legLift0, float legLift1, float eyeOpen, float mouthOpen) {
  return {
    bodyX, bodyY, P_IDLE.bodyRx, P_IDLE.bodyRy, 0.8f,
    P_IDLE.headDx, headDy, P_IDLE.headR, P_IDLE.yaw, tilt, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { legLift0, legLift1, P_IDLE.legLift[2], P_IDLE.legLift[3] },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, 0.8f, mouthOpen,
  };
}
constexpr CatKey K_PROTEST[] = {
  { 0.00f, P_IDLE },
  { 0.12f, catProtestPose(60.0f, 76.0f, -29.0f,  0.04f, -0.16f,  0.14f,
                          2.34f, -0.04f, 0.06f, 0.06f, 0.85f, 0.00f) },
  { 0.24f, catProtestPose(61.5f, 75.0f, -30.0f, -0.08f, -0.24f,  0.20f,
                          2.08f,  0.18f, 0.38f, 0.04f, 0.80f, 0.00f) },
  { 0.36f, catProtestPose(60.5f, 77.0f, -31.0f, -0.14f, -0.28f,  0.24f,
                          2.02f,  0.30f, 0.02f, 0.04f, 0.70f, 0.34f) },
  { 0.50f, catProtestPose(58.5f, 75.0f, -29.5f,  0.08f,  0.20f, -0.24f,
                          2.30f, -0.28f, 0.04f, 0.38f, 0.82f, 0.00f) },
  { 0.62f, catProtestPose(59.5f, 77.0f, -31.0f,  0.14f,  0.24f, -0.28f,
                          2.38f, -0.34f, 0.04f, 0.02f, 0.70f, 0.36f) },
  { 0.76f, catProtestPose(60.0f, 76.0f, -30.0f,  0.00f, -0.18f,  0.16f,
                          2.12f,  0.20f, 0.08f, 0.08f, 0.78f, 0.10f) },
  { 0.88f, catProtestPose(60.0f, 75.5f, -29.5f, -0.03f, -0.08f,  0.06f,
                          2.42f, -0.10f, 0.04f, 0.04f, 0.90f, 0.00f) },
  { 1.00f, P_IDLE },
};

// Sleepy mood: the quiet entry. The cat checks its bed, lowers, turns and wraps the tail before
// tucking the paws. The last authored key is the exact loaf object, just as it is for YAWNING and
// NODDING, so the sleepy base cannot pop by even a float bit at the hand-off.
constexpr CatKey K_CURL_UP[] = {
  { 0.00f, P_IDLE },
  { 0.16f, catYawnPose(60.0f, 77.0f, 28.0f, 21.5f,  2.0f, -27.0f, 20.0f, 0.38f, 0.10f,
                       0.10f, -0.08f, 2.62f, -0.05f, 0.08f, 0.72f, 0.00f) },
  { 0.34f, catYawnPose(59.0f, 80.0f, 28.0f, 20.5f,  7.0f, -24.0f, 20.0f, 0.52f, 0.18f,
                      -0.08f,  0.10f, 2.82f,  0.20f, 0.18f, 0.65f, 0.00f) },
  { 0.52f, catYawnPose(58.0f, 82.0f, 27.0f, 19.5f, 12.0f, -21.0f, 19.0f, 0.68f, 0.24f,
                      -0.12f,  0.08f, 3.02f,  0.46f, 0.34f, 0.00f, 0.00f) },
  { 0.68f, catYawnPose(56.5f, 83.5f, 26.5f, 19.0f, 17.0f, -20.5f, 18.5f, 0.84f, 0.20f,
                      -0.10f,  0.06f, 3.16f,  0.70f, 0.48f, 0.00f, 0.00f) },
  { 0.84f, P_SLEEP },
  { 1.00f, P_SLEEP },
};

// Content mood, the play beat: sink, gather, wiggle, leap. Two things make this pose work that
// are not obvious from the beat list.
//
// The BODY YAW is 0.55, not idle's 0.30. Haunches-up is the whole silhouette, and near-frontal
// cannot show it: the torso is one ellipse with no front-to-rear tilt, so a frontal crouch reads
// only as a cat getting shorter. A three-quarter turn gives the rear something to sit above, and
// stops well short of the profile that would cost the chibi face.
//
// The JUMP'S HEIGHT IS bodyY ALONE. `ground` is derived as bodyY + bodyRy + catLegGround, so the
// contact line rises with the torso and the legs come along without stretching. The lifts here
// only TUCK the paws, and every one of them stays under CAT_LEG_NEAR_GATE -- past the gate a
// foreleg switches to the reach-for-the-muzzle geometry and the airborne frames would render as
// a cat covering its face.
constexpr CatPose catPouncePose(float bodyX, float bodyY, float bodyRx, float bodyRy,
                                float headDy, float earL, float earR,
                                float tailBase, float tailCurl,
                                float foreLift, float hindL, float hindR, float eyeOpen) {
  return {
    bodyX, bodyY, bodyRx, bodyRy, 0.8f,          // shallow breath: the stalk is a held one
    P_IDLE.headDx, headDy, P_IDLE.headR, 0.55f, 0.0f, P_IDLE.headYaw,
    earL, earR,
    tailBase, tailCurl, P_IDLE.tailSwish,
    { foreLift, foreLift, hindL, hindR },
    P_IDLE.legFwd,
    eyeOpen, P_IDLE.pupilDx, P_IDLE.pupilDy, P_IDLE.mouthOpen,
  };
}
constexpr CatKey K_POUNCE[] = {
  { 0.00f, P_IDLE },
  // Sink. headDy tracks the torso DOWN for the same reason the stretch does: leaving the head put
  // while the body drops closes the neck into a concentric potato.
  { 0.18f, catPouncePose(60.0f, 80.0f, 30.0f, 19.0f, -26.0f,  0.10f, -0.10f,
                         2.80f, -0.10f, 0.00f, 0.14f, 0.14f, 0.72f) },
  // Full crouch. tailBase 3.05 is a hair under pi -- the tail leaves the rump straight back and
  // level -- and tailCurl 0 is what keeps it a rigid line instead of the idle's coiled tip.
  // eyeOpen 0.60 is the NARROWEST the art supports, not a taste call: ery = 5.2 * eyeOpen * fs, so
  // a fully open eye is only about five cells tall and anything under 0.60 collapses it to a
  // two-cell bar that reads as a rendering fault. The stalk squints as far as it can and gets its
  // contrast from the snap to 1.00 at the leap instead.
  { 0.32f, catPouncePose(60.0f, 84.0f, 31.0f, 16.0f, -22.0f,  0.14f, -0.14f,
                         3.05f,  0.00f, 0.00f, 0.34f, 0.34f, 0.60f) },
  // Butt wiggle: two cells of sway, three beats, haunches trading the weight. Small on purpose --
  // it is a tell, and a tell that travels reads as the cat losing its balance.
  { 0.40f, catPouncePose(58.0f, 84.0f, 31.0f, 16.0f, -22.0f,  0.14f, -0.14f,
                         3.05f,  0.00f, 0.00f, 0.42f, 0.26f, 0.60f) },
  { 0.48f, catPouncePose(62.0f, 84.0f, 31.0f, 16.0f, -22.0f,  0.14f, -0.14f,
                         3.05f,  0.00f, 0.00f, 0.26f, 0.42f, 0.60f) },
  { 0.56f, catPouncePose(58.0f, 84.0f, 31.0f, 16.0f, -22.0f,  0.14f, -0.14f,
                         3.05f,  0.00f, 0.00f, 0.42f, 0.26f, 0.60f) },
  // Coil: deepest and briefest, the stillness before it goes.
  { 0.66f, catPouncePose(60.0f, 85.0f, 32.0f, 15.0f, -21.0f,  0.16f, -0.16f,
                         3.10f,  0.00f, 0.00f, 0.46f, 0.46f, 0.60f) },
  // Jump. Body rises 20 and elongates, paws tuck, eyes snap wide.
  { 0.74f, catPouncePose(60.0f, 65.0f, 26.0f, 24.0f, -33.0f, -0.06f,  0.06f,
                         2.45f,  0.05f, 0.50f, 0.50f, 0.50f, 1.00f) },
  // Land on a squashed torso, then recover.
  { 0.84f, catPouncePose(60.0f, 79.0f, 30.0f, 19.0f, -28.0f,  0.06f, -0.06f,
                         2.52f, -0.14f, 0.10f, 0.10f, 0.10f, 0.85f) },
  { 0.92f, catPouncePose(60.0f, 76.0f, 28.0f, 21.0f, -29.0f,  0.02f, -0.02f,
                         2.56f, -0.18f, 0.02f, 0.02f, 0.02f, 0.95f) },
  { 1.00f, P_IDLE },
};

static void catKeysGet(CatAnim a, const CatKey*& K, int& n) {
  switch (a) {
    case CA_MEOW:    K = K_MEOW; n = (int)(sizeof K_MEOW / sizeof K_MEOW[0]); break;
    case CA_SLEEPING: K = K_SLEEP; n = (int)(sizeof K_SLEEP / sizeof K_SLEEP[0]); break;
    case CA_LICKING: K = K_LICK; n = (int)(sizeof K_LICK / sizeof K_LICK[0]); break;
    case CA_STRETCHING: K = K_STRETCH; n = (int)(sizeof K_STRETCH / sizeof K_STRETCH[0]); break;
    case CA_ITCH:    K = K_ITCH; n = (int)(sizeof K_ITCH / sizeof K_ITCH[0]); break;
    case CA_KNEADING: K = K_KNEAD; n = (int)(sizeof K_KNEAD / sizeof K_KNEAD[0]); break;
    case CA_BEGGING: K = K_BEG; n = (int)(sizeof K_BEG / sizeof K_BEG[0]); break;
    case CA_YAWNING: K = K_YAWN; n = (int)(sizeof K_YAWN / sizeof K_YAWN[0]); break;
    case CA_PLEASE: K = K_PLEASE; n = (int)(sizeof K_PLEASE / sizeof K_PLEASE[0]); break;
    case CA_TAIL_HUG: K = K_TAIL_HUG; n = (int)(sizeof K_TAIL_HUG / sizeof K_TAIL_HUG[0]); break;
    case CA_NODDING: K = K_NOD; n = (int)(sizeof K_NOD / sizeof K_NOD[0]); break;
    case CA_SLOWBLINK: K = K_SLOWBLINK; n = (int)(sizeof K_SLOWBLINK / sizeof K_SLOWBLINK[0]); break;
    case CA_SNIFF:   K = K_SNIFF; n = (int)(sizeof K_SNIFF / sizeof K_SNIFF[0]); break;
    case CA_WAKING:  K = K_WAKE; n = (int)(sizeof K_WAKE / sizeof K_WAKE[0]); break;
    case CA_GROOM_FACE: K = K_GROOM_FACE; n = (int)(sizeof K_GROOM_FACE / sizeof K_GROOM_FACE[0]); break;
    case CA_GROOM_FORELEG: K = K_GROOM_FORELEG; n = (int)(sizeof K_GROOM_FORELEG / sizeof K_GROOM_FORELEG[0]); break;
    case CA_GROOM_BELLY: K = K_GROOM_BELLY; n = (int)(sizeof K_GROOM_BELLY / sizeof K_GROOM_BELLY[0]); break;
    case CA_ADORE:   K = K_ADORE; n = (int)(sizeof K_ADORE / sizeof K_ADORE[0]); break;
    case CA_HEAD_BUNT: K = K_HEAD_BUNT; n = (int)(sizeof K_HEAD_BUNT / sizeof K_HEAD_BUNT[0]); break;
    case CA_PROTEST: K = K_PROTEST; n = (int)(sizeof K_PROTEST / sizeof K_PROTEST[0]); break;
    case CA_CURL_UP: K = K_CURL_UP; n = (int)(sizeof K_CURL_UP / sizeof K_CURL_UP[0]); break;
    case CA_POUNCE:  K = K_POUNCE; n = (int)(sizeof K_POUNCE / sizeof K_POUNCE[0]); break;
    default:         K = K_IDLE;  n = (int)(sizeof K_IDLE  / sizeof K_IDLE[0]);  break;
  }
}

static CatPose catLerp(const CatPose& a, const CatPose& b, float f) {
  CatPose o;
  const float* A = (const float*)&a; const float* B = (const float*)&b; float* O = (float*)&o;
  for (unsigned i = 0; i < sizeof(CatPose) / sizeof(float); i++) O[i] = A[i] + (B[i] - A[i]) * f;
  return o;
}

static CatPose catKeyEval(CatAnim a, float ph) {
  const CatKey* K; int n; catKeysGet(a, K, n);
  if (ph <= K[0].t) return K[0].p;
  // Return the authored endpoint verbatim. Letting ph==1 pass through the final lerp can leave a
  // one-bit float rounding difference (visible to the exact hand-off invariant) even though its
  // smoothstep factor is mathematically one.
  if (ph >= K[n - 1].t) return K[n - 1].p;
  for (int i = 0; i + 1 < n; i++)
    if (ph <= K[i + 1].t) {
      float span = K[i + 1].t - K[i].t;
      float f = span > 0 ? (ph - K[i].t) / span : 1.0f;
      f = f * f * (3.0f - 2.0f * f);               // smoothstep ease between keys
      return catLerp(K[i].p, K[i + 1].p, f);
    }
  return K[n - 1].p;
}

#ifdef CAT_TUNE
// Tuning-rig-only override of the EVALUATED pose, one slot per CatPose float, NaN = "use the
// keyframe value". This started as a single catTuneBodyYaw knob, which was the wrong shape: the
// five unauthored poses need mouthOpen (meow), eyeOpen (sleeping), tilt, legLift/legFwd
// (stretching, itch) and the ear twitches, and NONE of those could be scrubbed — you cannot
// author a pose against parameters nobody can reach, or find their limits before authoring.
// CatPose is all-float by static_assert, so one indexed array covers every field including any
// added later, instead of a hand-maintained knob per field. NaN rather than a negative sentinel
// because half of these (tilt, pupilDx, tailCurl) are legitimately negative.
static float catTunePose[CAT_POSE_FLOATS];
static bool  catTunePoseInit = false;
static void catTuneReset() {                        // all slots back to "keyframe decides"
  for (int i = 0; i < CAT_POSE_FLOATS; i++) catTunePose[i] = NAN;
  catTunePoseInit = true;
}
#endif

static const float CAT_TWO_PI  = 6.2831853f;
static const float CAT_BLEND_MS = 180.0f;   // pose-transition crossfade
static const float CAT_BLINK_MS = 120.0f;   // full blink close+open
static const float CAT_TWITCH_MS = 260.0f;  // one out-and-back whisker flick
static const float CAT_YAW_SLEW_PER_S = 2.5f;

// the ONLY trig in the pipeline runs here and in catRender's joint block
static CatPose catEvalCur(const CatPreset& p, const CatRenderState& s) {
  (void)p;
  CatPose o = catKeyEval(s.cur, s.phase);
  o.bodyRy   += o.breathe * sinf(s.aBreathe);
  // A sleeping cat's tucked tail should not wag like the needy/idle poses. Keep just enough
  // accumulator motion to prevent the hidden chain from becoming a special frozen code path.
  // The pounce damps too, and for a different reason than the sleepers: "tail straight" is the
  // pose, and at full gain the idle wag keeps playing through the stalk and dissolves the line
  // the crouch is built on. The gain is per-animation, so the leap inherits it -- a tail that
  // stays committed through the jump is not wrong, and a per-phase gain would cost a CatPose
  // field for one beat.
  float tailGain = s.cur == CA_SLEEPING ? 0.12f :
                   s.cur == CA_TAIL_HUG ? 0.20f :
                   s.cur == CA_POUNCE   ? 0.25f :
                   (s.cur == CA_YAWNING || s.cur == CA_NODDING || s.cur == CA_CURL_UP ||
                    s.cur == CA_WAKING) ? 0.35f : 1.0f;
  o.tailSwish = sinf(s.aTail) * tailGain;
  if (s.blinkT > 0) {
    float b = s.blinkT / CAT_BLINK_MS;              // 1 -> 0 over the blink
    o.eyeOpen *= fabsf(2.0f * b - 1.0f);            // fully closed mid-blink
  }
  o.headYaw = s.headYaw;                            // head facing slews independently of body yaw
#ifdef CAT_TUNE
  // LAST, so an override wins over the accumulators too. That ordering is the point: eyeOpen is
  // modulated by the blink and tailSwish is written outright above, so an override applied where
  // the old bodyYaw one sat would have been silently discarded for exactly the fields the
  // unauthored poses need to scrub.
  if (catTunePoseInit) {
    float* O = (float*)&o;
    for (int i = 0; i < CAT_POSE_FLOATS; i++)
      if (!isnan(catTunePose[i])) O[i] = catTunePose[i];
  }
#endif
  return o;
}

// public eval: the pose ON SCREEN, i.e. blended. catSetPose snapshots exactly this.
static CatPose catEval(const CatPreset& p, const CatRenderState& s) {
  CatPose q = catEvalCur(p, s);
  if (s.blend < 1.0f) q = catLerp(s.blendFrom, q, s.blend);
  return q;
}

static void catInit(CatRenderState& s) {
  s.cur = CA_IDLE; s.phase = 0; s.blend = 1;        // blend=1: blendFrom is never read
  s.blendFrom = CatPose{};
  s.aBreathe = 0; s.aTail = 0;
  s.blinkT = 0; s.blinkNextMs = 1500;
  s.twitchT = 0; s.twitchNextMs = 3000;
  s.headYaw = s.headYawTarget = CAT_POSE[CA_IDLE].yaw;
  s.mirror = false;
}

// The only transition op (spec §Transitions): snapshot the BLENDED on-screen pose,
// so interrupting a blend mid-flight is well-defined and pop-free.
static void catSetPose(CatRenderState& s, const CatPreset& p, CatAnim a) {
  s.blendFrom = catEval(p, s);
  s.cur = a; s.phase = 0; s.blend = 0;
  s.headYawTarget = CAT_POSE[a].yaw;
}

static bool catPoseDone(const CatRenderState& s) {
  return CAT_POSE[s.cur].mode == CP_ONCE && s.phase >= 1.0f;   // LOOP/HOLD are never done
}

// ALL time evolution lives here (spec §Runtime state). Takes the preset because
// breathe/tail rates are per-variant.
static void catAdvance(CatRenderState& s, const CatPreset& p, float dtMs) {
  const CatPoseDef& d = CAT_POSE[s.cur];
  float dphase = dtMs / (float)d.durMs;
  if      (d.mode == CP_LOOP) { s.phase += dphase; s.phase -= floorf(s.phase); }
  else if (d.mode == CP_ONCE) { s.phase += dphase; if (s.phase > 1.0f) s.phase = 1.0f; }
  // CP_HOLD pins phase
  s.aBreathe = fmodf(s.aBreathe + dtMs * 0.001f * p.breatheRate, CAT_TWO_PI);
  s.aTail    = fmodf(s.aTail    + dtMs * 0.001f * p.tailRate,    CAT_TWO_PI);
  if (s.blend < 1.0f) { s.blend += dtMs / CAT_BLEND_MS; if (s.blend > 1.0f) s.blend = 1.0f; }
  if (s.blinkT > 0) s.blinkT -= dtMs;
  s.blinkNextMs -= dtMs;
  if (s.blinkNextMs <= 0) {
    s.blinkT = CAT_BLINK_MS;
    // deterministic jitter off the accumulators — no RNG state, never re-aligns cleanly
    s.blinkNextMs = 2200.0f + 1400.0f * sinf(s.aTail * 0.7f + s.aBreathe);
  }
  if (s.twitchT > 0) s.twitchT -= dtMs;
  s.twitchNextMs -= dtMs;
  if (s.twitchNextMs <= 0) {
    s.twitchT = CAT_TWITCH_MS;
    // same no-RNG jitter idea as blink, phased differently so the two never lock step;
    // 6800 +/- 3200 versus blink's 2200 +/- 1400 — deliberately rarer (owner's call)
    s.twitchNextMs = 6800.0f + 3200.0f * sinf(s.aTail * 0.53f + s.aBreathe * 1.3f);
  }
  float dy = s.headYawTarget - s.headYaw;
  float cap = CAT_YAW_SLEW_PER_S * dtMs * 0.001f;
  if (dy >  cap) dy =  cap;
  if (dy < -cap) dy = -cap;
  s.headYaw += dy;
}

// rotate a head-local feature anchor by tilt around the head center -> grid ints.
// This is the spec's "tilt is a feature-space rotation": the head circle never rotates,
// its features do. Ears are tris, so their vertices rotate for free.
struct CatHead { float cx, cy, ct, st; };
// ---- facing model (spec docs/superpowers/specs/2026-07-29-cat-facing-pass-design.md) ---------
// A head feature is a POINT ON THE SKULL: azimuth a from the nose, elevation e from its own y.
// phi = headYaw * 90deg, so CAT_POSE[].yaw is now a real angle. Projection:
//   x = hr*cos(e)*sin(phi+a)   fore = cos(phi+a)/cos(a)   visible = cos(phi+a) > 0
// The cos(e) term is load-bearing: scaling x while leaving y alone projects onto a CYLINDER, and
// features then leave the head (the ear root reached 0.975*hr at headYaw 0.4 that way). With it,
// |(x,y)| = hr*sqrt(cos^2 e*sin^2 t + sin^2 e) <= hr always -- ear bases are buried by arithmetic,
// not by a clamp, and nothing needs a yaw-aware cull.
struct CatTurn { float sp, cp; };                   // sinf(phi), cosf(phi): two facing-model calls
static inline CatTurn catTurn(float fyaw) {
  float phi = fyaw * 1.5707963f;                    // 0..1 -> 0..pi/2, 1.0 IS full profile
  CatTurn t; t.sp = sinf(phi); t.cp = cosf(phi); return t;
}
// Ears are an iconic front-biased chibi feature, not face paint. Running their top-back anchors
// through the full face turn pushed both behind the skull: the leading ear became a tiny knob by
// headYaw 0.30 and full profile left only a rear ear. Nlerp the already-computed face turn toward
// identity, then normalize so the sphere-projection/burial proof still receives a unit sin/cos
// pair. One sqrt replaces two additional trig calls.
// The SHIPPED gain is 0 (see catEarYawGain), which makes this the identity turn and costs the
// device nothing: every input folds at compile time off the `static const`. It stays a function
// because the rig scrubs the knob live under CAT_TUNE, and because a future ear-parallax attempt
// wants this seam rather than a second facing model.
// `gain` is a parameter, not a read of catEarYawGain, so a test can exercise the seam at gains
// the shipped build has compiled away (the device's gain is a `static const 0`).
static inline CatTurn catEarTurn(const CatTurn& face, float gain) {
  // Not just a fast path: `face.sp * 0.0f` is NOT foldable to 0 (a NaN/Inf face would survive it),
  // so without this the shipped gain still pays the multiply/sqrt AND could propagate a poisoned
  // facing. An explicit compare folds the whole body away at the `static const 0` the device
  // builds with, and doubles as the NaN guard the other feats get from their own clamps.
  if (gain == 0.0f) { CatTurn i; i.sp = 0.0f; i.cp = 1.0f; return i; }
  CatTurn e;
  e.sp = face.sp * gain;
  e.cp = 1.0f + (face.cp - 1.0f) * gain;
  float n2 = e.sp * e.sp + e.cp * e.cp;
  if (n2 > 1e-6f) {
    float inv = 1.0f / sqrtf(n2);
    e.sp *= inv; e.cp *= inv;
  } else {
    e.sp = 0.0f; e.cp = 1.0f;
  }
  return e;
}
// ce = cos(elevation), sa/ca = sin/cos(azimuth). Azimuth is asin(k / ce) where k is the feature's
// old x fraction, which is exactly what makes phi=0 reproduce the expression being replaced.
// BAKED, not computed: the whole point is two trig calls a frame.
// test_feat_table_matches_its_derivation recomputes these from (k, yf) and fails on a hand edit.
struct CatFeat { float ce, sa, ca; };
static const CatFeat CF_EYE   = { 0.994987f, 0.623123f, 0.782124f };  // k 0.62, yf +0.10
static const CatFeat CF_BLUSH = { 0.877268f, 0.820709f, 0.571346f };  // k 0.72, yf +0.48
static const CatFeat CF_WHISK = { 0.887919f, 0.653213f, 0.757175f };  // k 0.58, yf +0.46
static const CatFeat CF_NOSE  = { 0.940425f, 0.000000f, 1.000000f };  // k 0.00, yf +0.34
static const CatFeat CF_TICK  = { 0.693974f, 0.504341f, 0.863504f };  // k 0.35, yf -0.72 (tick mid)
static const CatFeat CF_TICK0 = { 0.693974f, 0.000000f, 1.000000f };  // k 0.00, yf -0.72 (centre)
// CF_EARB's cosine is NEGATIVE, unlike every other feat here -- deliberately, not a typo. asin(k/ce)
// returns the acute solution, which is right for the eyes/nose/blush/whiskers (all on the FRONT of
// the face) but wrong for the ears: they sit on the top-BACK of the skull, behind the axis of
// rotation, so their projected x must move OPPOSITE to the nose as the head turns. Because
// sin(180-a) == sin(a), the supplementary (obtuse) angle reproduces the exact same front-on x at
// yaw 0 while reversing which way it travels off-axis -- negating only the cosine selects that
// obtuse root. Without this the ears chase the nose toward the facing side and converge into one
// bunny-eared blob instead of the far ear sliding behind the near one (owner catch, 2026-07-29).
static const CatFeat CF_EARB  = { 0.940425f, 0.531677f, -0.846947f };  // k 0.50, yf -0.34
// The muzzle blaze: a rounded triangle up the bridge of the nose. Two stations rather than one
// primitive placed by hand, because a marking that did not travel on the same projection as the
// nose it sits under would slide off the muzzle the moment the head turned.
static const CatFeat CF_BLAZE_TIP = { 0.996795f, 0.000000f, 1.000000f };  // k 0.00, yf -0.08
static const CatFeat CF_BLAZE     = { 0.693974f, 0.720488f, 0.693467f };  // k 0.50, yf +0.72
struct CatAz { float x, fore; bool vis; };
// side = +1 / -1 mirrors the azimuth. side=+1 is the LEADING feature (Task 1: positive headYaw
// faces screen +x), so it is the one that reaches the limb and disappears.
static inline CatAz catAz(float hr, const CatTurn& t, const CatFeat& f, float side) {
  float sa = f.sa * side;                           // sin(-a) = -sin(a), cos(-a) = cos(a)
  float s = t.sp * f.ca + t.cp * sa;                // sinf(phi + a), by angle addition
  float c = t.cp * f.ca - t.sp * sa;                // cosf(phi + a)
  float fore = c / f.ca;                            // normalized so phi=0 gives exactly 1
  // Clamp BOTH ends: >1 would fatten a feature crossing azimuth 0 (the trailing eye, mid-turn,
  // reaches 1/cos a = 1.27); <0 would hand a primitive a negative radius or reach.
  if (fore > 1.0f) fore = 1.0f; else if (fore < 0.0f) fore = 0.0f;
  CatAz r; r.x = hr * f.ce * s; r.fore = fore; r.vis = c > 0.0f; return r;
}

// A forehead stripe is a short meridian on the skull, not one flat screen-space x. Keep its
// azimuth fixed, then sample that longitude at crown / midpoint / brow elevations. cos(e) grows
// toward the equator, so the side stripes naturally bend inward at the crown and fan outward
// toward the brow; the centre stripe acquires the same spherical lean as the head turns.
struct CatTickArc { float x[3]; bool vis; };
static inline CatTickArc catTickArc(float hr, const CatTurn& t, const CatFeat& f, float side) {
  float sa = f.sa * side;
  float s = t.sp * f.ca + t.cp * sa;                // one longitude: sinf(phi + a)
  float c = t.cp * f.ca - t.sp * sa;
  float hs = hr * s;
  CatTickArc r;
  r.x[0] = hs * 0.474974f;                          // yf -0.88: crown
  r.x[1] = hs * 0.693974f;                          // yf -0.72: authored midpoint
  r.x[2] = hs * 0.828493f;                          // yf -0.56: brow
  r.vis = c > 0.0f;
  return r;
}

// The ear's shape history is worth keeping, because both of the failures were STRUCTURAL:
//  - As a TRIANGLE, its tip was a single vertex (no proportion tuning could blunt it) and its base
//    was a fixed-cell chord against a scaling skull, buried only while 0.60*hr + 10 <= 0.954*hr --
//    hr >= 28, which no shipped headSize reached, so the corners hung out as wings.
//  - As TWO COAXIAL CAPSULES, wide root then narrow tip, the radii met at a STEP. At high earPoint
//    that step plus a 1-cell cap read as a nipple, and no value of earPoint could remove it.
// A ROUNDED TRIANGLE (catRTri: the hull of three discs) fixes both at once. Sides are straight
// tangents, so no step exists anywhere. The tip is round by construction. And the two base corners
// are SPHERE FEATURES (catAz, CF_EARB), so |(x,y)| <= hr buries them at every yaw by arithmetic --
// no clamp, no yaw-aware cull. One primitive means one shading frame, so the seam that forced the
// capsule design cannot occur either. Ears draw BEFORE the head, so painter order is the entire
// visibility rule: the far ear's root hides behind the skull while its tip still clears it.
struct CatEar { float blx, brx, by, tx, ty; };     // base corners share an elevation, hence one by
static inline int catEarRad(int hr) { int r = (int)((float)hr * 0.26f + 0.5f); return r < 2 ? 2 : r; }

// Tip radius as pointiness rises: lerp from the stock 5/8-of-root tip down to a 1-cell tip.
static inline int catEarTipRad(int hr, float point) {
  int base = (catEarRad(hr) * 5) / 8; if (base < 2) base = 2;
  int rt = (int)((float)base + (1.0f - (float)base) * point + 0.5f);
  return rt < 1 ? 1 : rt;
}

// Base half-width. At earPoint 0 this is exactly R - rad, so the hull's base is 2R wide -- today's
// root width, and only the step changes. It then widens by 0.35*R: a pointier ear is a wider-based
// one, which is what reads as a cat rather than a horn.
static inline float catEarHalfBase(int R, int rad, float point) {
  return (float)(R - rad) + 0.35f * (float)R * point;
}

// One ear: two projected base corners plus an off-sphere tip.
// The corners sit at azimuth CF_EARB +/- delta, and delta needs no asinf: the rotated feats follow
// from the addition identities, which want only cos(delta) = sqrt(1 - sin^2 delta). One sqrtf per
// ear. Because both corners are azimuths converging on the limb, the base FORESHORTENS as the head
// turns -- correct for a flap, and free.
static CatEar catEarShape(int hr, float earLen, float side, float wob,
                          const CatTurn& tn, float halfBase) {
  float h = (float)hr;
  // sin(delta): the base half-angle on the skull. Clamped at 0.60 -- the shipped range peaks near
  // 0.33 (hr 28, earPoint 1), so the clamp only guards a tiny head with a wide base, where an
  // unclamped delta would push cos(azimuth) toward zero and blow up fore's normalization.
  float sd = halfBase / (h * CF_EARB.ce);
  if (sd > 0.60f) sd = 0.60f;
  float cd = sqrtf(1.0f - sd * sd);
  CatFeat fl = { CF_EARB.ce, CF_EARB.sa * cd - CF_EARB.ca * sd, CF_EARB.ca * cd + CF_EARB.sa * sd };
  CatFeat fr = { CF_EARB.ce, CF_EARB.sa * cd + CF_EARB.ca * sd, CF_EARB.ca * cd - CF_EARB.sa * sd };
  CatAz L = catAz(h, tn, fl, side), Rr = catAz(h, tn, fr, side);
  CatAz mid = catAz(h, tn, CF_EARB, side);
  CatEar E;
  E.blx = L.x; E.brx = Rr.x; E.by = -h * 0.34f;        // one elevation ring, so one y
  // The tip is OUTSIDE the skull (y = -hr - earLen), so it has no azimuth. Same rule as the
  // whiskers: project the root, then add the outward lean as a length scaled by fore. The twitch
  // rides on the lean, where it has always been.
  E.tx = mid.x + (side * h * 0.12f + side * wob * 6.0f) * mid.fore;
  E.ty = -h - earLen;
  return E;
}

// Largest reach w >= 0 keeping base + k*w inside [lo, hi]. Used to fit the whisker fan to the
// grid: catHrot is linear in the reach, so this is exact and needs no search.
// MEASURED DEAD IN PRACTICE (final review, 2026-07-29): after both `catFitLen` clamps were deleted
// entirely, a 6048-configuration sweep (4 headSize x 3 chub x 2 eyeShape x outline x 6 poses x
// 21 yaws) recorded ZERO clipped flags -- at high yaw only the backward-pointing side of the fan
// survives, and that side never reaches the grid edge. This costs ~12 float divisions/frame on the
// FPU-less C3 for a guard nothing measured has tripped. NOT deleted here: 6048 samples is not a
// proof for every future preset/tuning combination the rig can still reach. Candidate deletion
// recorded in NEXT_SESSION.md with this evidence -- revisit there, don't re-derive from scratch.
static inline float catFitLen(float base, float k, float lo, float hi) {
  if (k > -1e-4f && k < 1e-4f) return (base >= lo && base <= hi) ? 1e9f : 0.0f;
  float a = (lo - base) / k, b = (hi - base) / k;
  float m = a > b ? a : b;                       // the bound reached as w grows
  return m > 0.0f ? m : 0.0f;
}
static inline void catHrot(const CatHead& h, float fx, float fy, int& ox, int& oy) {
  ox = (int)(h.cx + fx * h.ct - fy * h.st + 0.5f);
  oy = (int)(h.cy + fx * h.st + fy * h.ct + 0.5f);
}

// Screen-space leg joints after projecting the body's anatomical fore/aft and side axes.
// The side index only locates siblings within a plane; it does not decide whether a limb goes
// behind or in front of the body. Rear/front anatomy decides painter depth.
struct CatRearLeg {
  int haunchX, haunchY, hockX, hockY, pawX, pawY;
};
struct CatForeLeg {
  int rootX, rootY, pawX, pawY;
};
struct CatLegLayout {
  CatRearLeg rear[2];
  CatForeLeg fore[2];
  int haunchRx, haunchRy, pawRx, pawRy, legRad;
  bool rearNear[2];                       // hind leg lifted to scratch: draws AFTER the body
  bool foreNear[2];                       // foreleg reaching the muzzle: draws AFTER the face
};

// A leg lifted past this stops being a tucked paw and becomes a raised one. Below the gate
// nothing changes at all -- the legs keep their original reach and their original painter depth,
// which is what every pose but the itch and the lick wants (sleeping's four-paw tuck sits at 0.55
// and deliberately stays under it). Above it the leg gains reach toward what it is reaching FOR,
// and moves to a nearer plane so it is not swallowed by the part it has come round the front of.
// Shared by both pairs, but they move to different planes: a hind leg comes round the torso and
// draws after the body, a foreleg comes up to the mouth and draws after the face.
static const float CAT_LEG_NEAR_GATE = 0.62f;

// Five shorter segments preserve the shipped tail's total length while giving a strong curl
// enough joints to turn back toward the cat. Four long segments could either make the idle tail
// smooth OR make a sleeping coil, but not both: the latter ran into the grid before its heading
// came back around.
static const int CAT_TAIL_SEGMENTS = 5;
struct CatTailSeg {
  int x0, y0, x1, y1, rad;
};
struct CatTailLayout {
  CatTailSeg seg[CAT_TAIL_SEGMENTS];
  int frontStart;                         // first returning segment drawn over the torso
  int rootX, rootY;
};

static inline int catMixI(float a, float b, float f) {
  return (int)(a + (b - a) * f + 0.5f);
}

static CatTailLayout catProjectTail(const CatPose& q, const CatPreset& p,
                                    int bx, int by, int brx, int bry) {
  CatTailLayout L;
  L.frontStart = CAT_TAIL_SEGMENTS;
  float base = q.tailBase;
  // Only the ROOT needs the face backstop. The old code applied it after every joint, so a curl
  // could never finish its turn: every distal heading was dragged back into the same up/back-left
  // band. Keeping the root there preserves idle/meow safety while freeing the rest of the chain.
  if (base < 1.55f) base = 1.55f + (base - 1.55f) * 0.25f;
  if (base > 3.45f) base = 3.45f + (base - 3.45f) * 0.25f;

  // In profile the visible rump surface is farther inside the screen-space ellipse than it is
  // head-on. Move the root inward with body yaw so a sleeping coil joins the flank instead of
  // hanging from the extreme left silhouette; idle's modest yaw shifts it by only two cells.
  float txf = (float)(bx - brx + 4) + q.yaw * 6.0f;
  float tyf = (float)(by + bry - 12);
  L.rootX = (int)(txf + 0.5f); L.rootY = (int)(tyf + 0.5f);
  // tailLen was authored as one quarter of the whole chain. Keep that total reach after adding
  // the fifth joint; e.g. the shipped 10-cell value becomes five 8-cell segments, still 40 cells.
  int segLen = (int)(p.tailLen * 4.0f / (float)CAT_TAIL_SEGMENTS + 0.5f);
  if (segLen < 2) segLen = 2;
  int rad = (int)(p.tailFluff + 0.5f);
  float sw = q.tailSwish * 0.10f;

  for (int i = 0; i < CAT_TAIL_SEGMENTS; i++) {
    // Segment zero leaves on tailBase itself. From segment one onward these headings reproduce
    // the old four-joint sequence, with one important difference: there is no distal backstop.
    // The triangular swish term is the closed form of the old per-joint accumulation.
    float fi = (float)i;
    float a = base + q.tailCurl * fi + sw * fi * (fi + 1.0f) * 0.25f;
    int srad = rad - i / 2; if (srad < 1) srad = 1;
    float nx = txf + cosf(a) * (float)segLen;
    float ny = tyf - sinf(a) * (float)segLen;

    // A strong curl becomes anatomically "near" once it has turned back toward +x (the head).
    // Split there so the root remains behind the rump but a returning tip can wrap over the
    // torso. Small idle/meow curves never satisfy this and keep their original painter depth.
    if (L.frontStart == CAT_TAIL_SEGMENTS && i >= 2 &&
        (q.tailCurl > 0.42f || q.tailCurl < -0.42f) && nx > txf + 0.5f)
      L.frontStart = i;

    // Fold endpoints before rasterization. Four cells budget the outline and the transverse
    // tabby band as well as the capsule itself; the old +2 budget still let a curled striped tail
    // raise T1 even though its centreline had been clamped.
    float margin = (float)srad + 4.0f;
    if (nx < margin) nx = margin;
    else if (nx > (float)CAT_GW - 1.0f - margin) nx = (float)CAT_GW - 1.0f - margin;
    if (ny < margin) ny = margin;
    else if (ny > (float)CAT_GH - 1.0f - margin) ny = (float)CAT_GH - 1.0f - margin;

    L.seg[i].x0 = (int)(txf + 0.5f); L.seg[i].y0 = (int)(tyf + 0.5f);
    L.seg[i].x1 = (int)(nx + 0.5f);  L.seg[i].y1 = (int)(ny + 0.5f);
    L.seg[i].rad = srad;
    txf = nx; tyf = ny;
  }
  return L;
}

// hxi/hyi/hr describe the head, used ONLY to stop a scratching hind paw from climbing into it.
static CatLegLayout catProjectLegs(const CatPose& q, int bx, int by, int brx, int bry, int ground,
                                   int hxi, int hyi, int hr) {
  CatLegLayout L;
  // Squaring yaw holds the useful front-facing layout through the near-frontal range, then
  // decisively separates rump and chest as the body reaches profile. Linear interpolation
  // collapsed the right haunch into the forelegs at idle's modest 0.30 yaw.
  float turn = q.yaw * q.yaw;
  int basePawRx = brx / 6; if (basePawRx < 2) basePawRx = 2;
  L.pawRx = (int)((float)basePawRx * catLegPawScale + 0.5f); if (L.pawRx < 2) L.pawRx = 2;
  L.pawRy = L.pawRx * 2 / 3; if (L.pawRy < 2) L.pawRy = 2;
  L.legRad = L.pawRx - 1; if (L.legRad < 2) L.legRad = 2;
  L.haunchRx = (int)((float)brx * 0.32f + 0.5f);
  L.haunchRy = (int)((float)bry * 0.38f + 0.5f);

  // The rim under-draw grows every shape +1, so the deepest thing below a paw line is the
  // shank cap at pawY + legRad + 1. Clamp per-paw against the grid: the flat CAT_GH-6 ground
  // clamp upstream doesn't know the derived radius, and extreme chub pushed the ring off-grid
  // (clipped=8, the front-legs bit, on the extremes test — shipped presets fit CAT_GH-1 by luck).
  int pawYMax = CAT_GH - 2 - L.legRad;

  // Rear-vs-front depth is visible as a vertical setback only while facing the camera. In
  // profile that anatomical axis is horizontal, so the setback fades away. Do NOT replace
  // it with a far-side lift: that raises one front and one rear paw as an inexplicable pair.
  int rearLift = (int)((1.0f - turn) * catLegRearSet + 0.5f);
  for (int e = 0; e < 2; e++) {
    float side = e ? 1.0f : -1.0f;

    // Rear: wide haunches in the frontal view; a compact group at negative x (tail side) in
    // profile. The hock stays behind the paw so the foot points toward the cat's front.
    float rearPawFront = (float)bx + side * (float)brx * CAT_REAR_SPREAD;
    float rearPawProfile = (float)bx + (float)brx *
      (catLegRearX + side * CAT_SIDE_PROFILE);
    float haunchFront = (float)bx + side * (float)brx * 0.42f;
    float haunchProfile = (float)bx + (float)brx *
      (catLegRearX - catLegHock + side * CAT_SIDE_PROFILE * 0.55f);
    float hockFront = rearPawFront - side * 2.0f;
    float hockProfile = rearPawProfile - (float)brx * catLegHock;
    CatRearLeg& R = L.rear[e];
    float lift = q.legLift[e ? 3 : 2];
    R.pawX = catMixI(rearPawFront, rearPawProfile, turn);
    R.pawY = ground - rearLift - (int)(lift * 24.0f);
    R.haunchX = catMixI(haunchFront, haunchProfile, turn);
    R.haunchY = by + (int)((float)bry * 0.25f) + 5;

    // The scratch. 24 cells of rise only reaches ground-24 = the middle of the torso, so the
    // whole of it was hidden behind the body -- which is why no keyframe could show a hind leg
    // doing anything. The EXCESS above the gate is what buys the reach, rather than a bigger
    // global scale: rescaling the base would have lifted sleeping's tucked paws too.
    L.rearNear[e] = lift > CAT_LEG_NEAR_GATE;
    if (L.rearNear[e]) {
      float over = lift - CAT_LEG_NEAR_GATE;
      R.pawY -= (int)(over * 44.0f + 0.5f);
      R.pawX += (int)(side * (float)brx * over * 0.30f + 0.5f);   // outward, clear of the muzzle

      // Then stop the paw at the head rather than at a tuned constant. The first attempt put it
      // at y=52 on THIS preset, inside the head disc -- and since the head draws last the whole
      // scratch vanished behind the face. A constant that happens to clear preset 0's head is not
      // a fix: headSize reaches 2.32, so a big-headed preset would swallow it again. Push the paw
      // straight down until it is outside the skull (plus its own radius and the outline ring).
      int clear = hr + L.pawRy + 2;
      for (int guard = 0; guard < 64; guard++) {
        int dx = R.pawX - hxi, dy = R.pawY - hyi;
        if (dx * dx + dy * dy >= clear * clear) break;
        R.pawY++;
      }
    }
    if (R.pawY > pawYMax) R.pawY = pawYMax;
    R.hockX = catMixI(hockFront, hockProfile, turn);
    R.hockY = R.pawY - L.pawRy - 1;

    // Fore: central at the camera, then gathered under the chest/head in profile.
    float foreFront = (float)bx + side * (float)brx * CAT_FORE_SPREAD;
    float foreProfile = (float)bx + (float)brx *
      (catLegFrontX + side * CAT_SIDE_PROFILE);
    float rootProfile = (float)bx + (float)brx *
      (catLegFrontX - 0.10f + side * CAT_SIDE_PROFILE * 0.55f);
    CatForeLeg& F = L.fore[e];
    float fLift = q.legLift[e ? 0 : 1];
    F.pawX = catMixI(foreFront, foreProfile, turn) + (int)(q.legFwd + 0.5f);
    F.pawY = ground - (int)(fLift * 22.0f);
    if (F.pawY > pawYMax) F.pawY = pawYMax;
    F.rootX = catMixI(foreFront, rootProfile, turn);
    F.rootY = by + (int)((float)bry * 0.43f);

    // Reaching a paw to the mouth. 22 cells of rise stops at ground-22, ~16 short of the chin,
    // and raising the paw alone collapses the shank into a nub on the belly -- so instead of a
    // bigger rise, steer the paw AT the muzzle and let the root-to-paw capsule become the arm.
    // Straight up is the one direction that cannot work: the forelegs sit at only +-0.19*brx, so
    // directly overhead is the middle of the skull.
    L.foreNear[e] = fLift > CAT_LEG_NEAR_GATE;
    if (L.foreNear[e]) {
      float t = (fLift - CAT_LEG_NEAR_GATE) / (1.0f - CAT_LEG_NEAR_GATE);   // 0 at the gate, 1 wide open
      float mx = (float)hxi, my = (float)hyi + (float)hr * 0.45f;           // the muzzle
      F.pawX += (int)((mx - (float)F.pawX) * t * 0.90f + 0.5f);
      F.pawY += (int)((my - (float)F.pawY) * t * 0.90f + 0.5f);
      // Never above the head's centre: past that the paw is over the eyes rather than the mouth,
      // and a pose about grooming reads as a cat covering its face.
      if (F.pawY < hyi) F.pawY = hyi;
    }
  }
  return L;
}

// skipHaunch: for a raised leg drawn OVER the body. The haunch sits inside the torso, so painting
// it in the near pass just repaints body-coloured fur in a body-sized blob and the limb reads as a
// ridge on the belly rather than as a leg. Dropping it lets the shank emerge from under the flank.
static void catDrawRearLeg(CatRaster& r, const CatLegLayout& L, int e, const uint8_t* ramp,
                           bool skipHaunch = false) {
  const CatRearLeg& R = L.rear[e];
  if (!skipHaunch) shEllipse(r, R.haunchX, R.haunchY, L.haunchRx, L.haunchRy, ramp);
  shCapsule(r, R.haunchX, R.haunchY + L.haunchRy / 2,
           R.hockX, R.hockY, L.legRad, ramp);
  // A short second segment makes the hock visibly bent instead of another straight table leg.
  int footRad = L.legRad - 1; if (footRad < 2) footRad = 2;
  shCapsule(r, R.hockX, R.hockY, R.pawX, R.pawY - 1, footRad, ramp);
  shEllipse(r, R.pawX, R.pawY, L.pawRx, L.pawRy, ramp);
}

static void catDrawForeLeg(CatRaster& r, const CatLegLayout& L, int e, const uint8_t* ramp,
                           bool contact = false, bool raised = false) {
  const CatForeLeg& F = L.fore[e];
  bool oldContact = r.contact;
  // A raised arm needs the paw to READ as a paw. Standing, legRad is pawRx-1 and the one-cell
  // step is enough because the paw sits on the ground with the shank straight above it. Held up
  // against the muzzle it isn't: the shaft and the paw are the same object at a glance and the
  // limb renders as a uniform post. Thin the shaft and grow the paw so the end has a shape.
  int shaftRad = raised ? (L.legRad > 3 ? L.legRad - 1 : 2) : L.legRad;
  int prx = raised ? L.pawRx + 1 : L.pawRx, pry = raised ? L.pawRy + 1 : L.pawRy;
  r.contact = contact;                                // seam where foreleg crosses body/rear leg
  shCapsule(r, F.rootX, F.rootY, F.pawX, F.pawY, shaftRad, ramp);
  r.contact = false;                                  // keep paw joined to its own shank
  shEllipse(r, F.pawX, F.pawY, prx, pry, ramp);
  r.contact = oldContact;
}

// One outer ear: a single rounded triangle. Extracted from catRender's ears loop so the outline
// under-draw pass can call the exact same geometry in rim mode before the fill pass draws it.
static void catDrawOuterEar(CatRaster& r, const CatHead& hd, int hr, const CatPreset& p,
                            const CatTurn& tn, int e, float wob) {
  int R = catEarRad(hr), rad = catEarTipRad(hr, p.earPoint);
  float halfBase = catEarHalfBase(R, rad, p.earPoint);
  CatEar E = catEarShape(hr, p.earLen, e ? 1.0f : -1.0f, wob, tn, halfBase);
  int blx, bly, brx, bry, tx, ty;
  catHrot(hd, E.blx, E.by, blx, bly);
  catHrot(hd, E.brx, E.by, brx, bry);
  catHrot(hd, E.tx,  E.ty, tx,  ty);
  // Flat ramp: an ear is a thin flap, and the full terminator made one of the pair black.
  shRTri(r, blx, bly, brx, bry, tx, ty, rad, CAT_FUR_FLAT);
}

static void catRender(uint8_t grid[CAT_GH][CAT_GW], const CatPreset& p,
                      const CatRenderState& s, CatDiagnostics* diag) {
  for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) grid[y][x] = CI_TRANS;
  if (diag) {
    diag->clipped = 0; diag->cellsFilled = 0; diag->headBodyOverlap = 0;
    diag->bboxX0 = CAT_GW; diag->bboxY0 = CAT_GH; diag->bboxX1 = 0; diag->bboxY1 = 0;
    for (int i = 0; i < CAT_PART_COUNT; i++) diag->occluded[i] = 0;
    for (int y = 0; y < CAT_GH; y++) for (int x = 0; x < CAT_GW; x++) diag->owner[y][x] = 0;
  }
  CatPose q = catEval(p, s);
  CatRaster r; r.g = grid; r.d = diag; r.part = CATP_TAIL; r.clipTo = 0; r.clipR = 0; r.remap = false; r.remapLight = false; r.far = false; r.contact = false; r.darken = false; r.furOnly = false; r.rim = false; r.mirror = s.mirror; r.ramp = nullptr;

  // Sticker pass: flat collapses the body/leg/tail ramp — one step flatter than the face's own
  // collapse (CAT_FUR_STICKER vs CAT_FUR_FLAT), since the body is a much bigger mass than the
  // face and the face's ramp alone still left the darkest band as its own visible color.
  const uint8_t* bodyRamp = p.flat ? CAT_FUR_STICKER : CAT_FUR;

  // ---- joints -> integers. ALL trig and float math ends in this block. ----
  int bx  = (int)(q.bodyX + 0.5f), by = (int)(q.bodyY + 0.5f);
  int brx = (int)(q.bodyRx * p.bodyChub + 0.5f);
  // chub grows the body in BOTH axes, ry at 45% of the rate. Scaling rx alone turned a chubby
  // cat into a pancake — 84 cells wide by 44 tall, four legs strung across it like a table.
  int bry = (int)(q.bodyRy * (0.55f + 0.45f * p.bodyChub) + 0.5f);   // breathe folded in by catEval
  float hx = q.bodyX + q.headDx + q.yaw * 12.0f;    // yaw slides the head toward profile
  // The head rides up by whatever chub added to the body's half-height. Without this the
  // torso grew into a fixed headDy and swallowed the neck — chub 1.5 sank the skull 5 cells
  // deeper than chub 1.0 and the cat read as a capybara.
  float hy = q.bodyY + q.headDy - q.bodyRy * 0.45f * (p.bodyChub - 1.0f);
  int hr  = (int)(q.headR * p.headSize + 0.5f);
  // The lift must not push ear tips off-grid (big head + long ear + high chub did). No longer
  // budgets a shading offset: with per-cell lighting the triangle IS the extent it declares.
  // +2 not +1: the under-drawn outline grows the ear silhouette by one more cell (spec §Sticker
  // pass, under-draw) — that grown ring needs its own headroom too.
  float earTop = (float)hr + p.earLen + (float)catEarRad(hr) + 2.0f;   // widest radius bounds it
  if (hy < earTop) hy = earTop;
  int hxi = (int)(hx + 0.5f), hyi = (int)(hy + 0.5f);
  CatHead hd; hd.cx = hx; hd.cy = hy; hd.ct = cosf(q.tilt); hd.st = sinf(q.tilt);
  CatTurn tn = catTurn(q.headYaw);   // face turn: two trig calls for the whole facing model
  CatTurn en = catEarTurn(tn, catEarYawGain);   // front-biased ear turn, normalized w/o more trig
  int ground = by + bry + (int)floorf(catLegGround + 0.5f);
  if (ground > CAT_GH - 6) ground = CAT_GH - 6;
  CatLegLayout legs = catProjectLegs(q, bx, by, brx, bry, ground, hxi, hyi, hr);

  // Whisker twitch: one sine lobe over the flick, lifting the tip rows. Negative = up.
  float twitchDy = 0.0f;
  if (s.twitchT > 0)
    twitchDy = -2.5f * sinf((1.0f - s.twitchT / CAT_TWITCH_MS) * 3.14159265f);

  // ---- tail geometry: chained capsules, swished by the accumulator sine. Split from the
  // fill (below) so the outline under-draw pass can rim these exact segments before any fill
  // covers them (spec §Sticker pass, under-draw). ----
  CatTailLayout tail = catProjectTail(q, p, bx, by, brx, bry);

  // ---- sticker outline: under-draw the whole cat grown +1 (spec §Sticker pass). Drawn FIRST,
  // so every fill covers its interior — outline survives only at the true silhouette. Per-part
  // rim would put dark lines at part boundaries; refs keep head+ear one continuous blob. ----
  if (p.outline) {
    r.rim = true;
    r.part = CATP_TAIL;
    for (int i = 0; i < CAT_TAIL_SEGMENTS; i++)
      shCapsule(r, tail.seg[i].x0, tail.seg[i].y0,
                tail.seg[i].x1, tail.seg[i].y1, tail.seg[i].rad);
    r.part = CATP_REAR_LEGS;
    catDrawRearLeg(r, legs, 0, bodyRamp); catDrawRearLeg(r, legs, 1, bodyRamp);
    r.part = CATP_BODY;
    shEllipse(r, bx, by, brx, bry);
    r.part = CATP_FRONT_LEGS;
    catDrawForeLeg(r, legs, 0, bodyRamp); catDrawForeLeg(r, legs, 1, bodyRamp);
    r.part = CATP_EARS;
    for (int e = 0; e < 2; e++) catDrawOuterEar(r, hd, hr, p, en, e, e ? q.earR : q.earL);
    r.part = CATP_HEAD;
    shEllipse(r, hxi, hyi, hr, hr);
    r.rim = false;
  }

  // ---- tail root: first fill in painter order, behind everything else. Strong curls leave
  // their returning tip for the near-tail pass after the body. ----
  r.part = CATP_TAIL;
  for (int i = 0; i < tail.frontStart; i++)
    shCapsule(r, tail.seg[i].x0, tail.seg[i].y0,
              tail.seg[i].x1, tail.seg[i].y1, tail.seg[i].rad, bodyRamp);

  if (p.marking == 1 || p.marking == 3) {
    // tabby tail rings: three thin bands ACROSS the chain, not alternating whole segments —
    // a recolored segment is a quarter of the tail lengthwise and reads as patchwork, not
    // rings (owner catch, 2026-07-29). Perpendicular to the local segment axis, reaching one
    // cell past the fill so the band meets the outline ring on both sides; remap can't touch
    // CI_OUTLINE, so the band ends cleanly at the rim.
    r.remap = true;
    static const float RING_U[3] = { 0.325f, 0.525f, 0.725f }; // normalized chain positions
    for (int k = 0; k < 3; k++) {
      float at = RING_U[k] * (float)CAT_TAIL_SEGMENTS;
      int i = (int)at; if (i >= tail.frontStart) continue;
      float t = at - (float)i;
      const CatTailSeg& sg = tail.seg[i];
      float px = (float)sg.x0 + (float)(sg.x1 - sg.x0) * t;
      float py = (float)sg.y0 + (float)(sg.y1 - sg.y0) * t;
      float dx = (float)(sg.x1 - sg.x0), dy = (float)(sg.y1 - sg.y0);
      float len = sqrtf(dx * dx + dy * dy); if (len < 1.0f) len = 1.0f;
      float ex = -dy / len * ((float)sg.rad + 1.5f), ey = dx / len * ((float)sg.rad + 1.5f);
      catCapsule(r, (int)(px - ex + 0.5f), (int)(py - ey + 0.5f),
                 (int)(px + ex + 0.5f), (int)(py + ey + 0.5f), 1, CI_ACC_1);
    }
    r.remap = false;
  }

  // ---- rear-leg plane: both hind legs belong behind the torso head-on. ----
  // This is not a left/right "far side" pair. The following body pass overwrites both rear
  // haunches and upper joints; the darker ramp and raised frontal paws reinforce the same
  // anatomical depth cue.
  r.part = CATP_REAR_LEGS; r.far = true;
  if (!legs.rearNear[0]) catDrawRearLeg(r, legs, 0, bodyRamp);
  if (!legs.rearNear[1]) catDrawRearLeg(r, legs, 1, bodyRamp);
  r.far = false;

  // ---- body + markings ----
  r.part = CATP_BODY;
  shEllipse(r, bx, by, brx, bry, bodyRamp);
  if (p.marking == 2) {                              // patch: one blob on the flank
    r.remap = true;                                  // masked fill: recolors fur, can't bleed
    catEllipse(r, bx - brx / 2, by - 4, 12, 10, CI_ACC_1);
    r.remap = false;
  } else if (p.marking == 3) {                       // blaze: the light chest every chibi
    r.remap = true; r.remapLight = true;             // reference wears. NOT at the anatomical
    catEllipse(r, bx, by + bry / 6,                  // chest -- the head paints over that whole
               brx / 2, bry / 2 + 3, CI_FUR_0);      // band (headBodyOverlap ~760) -- but the
    r.remap = false; r.remapLight = false;           // visible bib below the chin, between the
  }                                                  // forelegs (idx ignored: remap decides)
                                                     // marking 1 (tabby) is drawn on the head —
                                                     // head, haunch and legs cover most of the body

  // ---- scratching hind leg: a leg raised past CAT_LEG_NEAR_GATE has come round the outside of
  // the torso, so it draws over the body instead of under it. Its rim goes here too rather than in
  // the outline block, which runs before the body and would have had the body paint straight over
  // it. Not r.far: this leg is the nearest thing on the cat, and the fore/aft depth tint would
  // read as a sock on the one limb the pose is about. ----
  for (int e = 0; e < 2; e++) {
    if (!legs.rearNear[e]) continue;
    r.part = CATP_REAR_LEGS;
    if (p.outline) { r.rim = true; catDrawRearLeg(r, legs, e, bodyRamp, true); r.rim = false; }
    r.contact = true;                                  // one seam where the leg crosses the flank
    catDrawRearLeg(r, legs, e, bodyRamp, true);
    r.contact = false;
  }

  // ---- returning tail tip: a real curl crosses from the far side of the rump to the near side
  // when its heading comes back toward +x. The first near segment casts one contact halo over the
  // torso; the rest join it without per-segment seams. Idle/meow never enter this pass. ----
  if (tail.frontStart < CAT_TAIL_SEGMENTS) {
    r.part = CATP_TAIL;
    for (int i = tail.frontStart; i < CAT_TAIL_SEGMENTS; i++) {
      r.contact = i == tail.frontStart;
      shCapsule(r, tail.seg[i].x0, tail.seg[i].y0,
                tail.seg[i].x1, tail.seg[i].y1, tail.seg[i].rad, bodyRamp);
    }
    r.contact = false;
    if (p.marking == 1 || p.marking == 3) {
      r.remap = true;
      static const float RING_U[3] = { 0.325f, 0.525f, 0.725f };
      for (int k = 0; k < 3; k++) {
        float at = RING_U[k] * (float)CAT_TAIL_SEGMENTS;
        int i = (int)at; if (i < tail.frontStart || i >= CAT_TAIL_SEGMENTS) continue;
        float t = at - (float)i;
        const CatTailSeg& sg = tail.seg[i];
        float px = (float)sg.x0 + (float)(sg.x1 - sg.x0) * t;
        float py = (float)sg.y0 + (float)(sg.y1 - sg.y0) * t;
        float dx = (float)(sg.x1 - sg.x0), dy = (float)(sg.y1 - sg.y0);
        float len = sqrtf(dx * dx + dy * dy); if (len < 1.0f) len = 1.0f;
        // Stop at the capsule boundary on the near pass: extending into adjacent fur would stripe
        // the torso wherever a wrapped tail crosses it.
        float reach = (float)(sg.rad > 1 ? sg.rad - 1 : 1);
        float ex = -dy / len * reach, ey = dx / len * reach;
        catCapsule(r, (int)(px - ex + 0.5f), (int)(py - ey + 0.5f),
                   (int)(px + ex + 0.5f), (int)(py + ey + 0.5f), 1, CI_ACC_1);
      }
      r.remap = false;
    }
  }

  // ---- front-leg plane: both forelegs sit between the body and head. ----
  // Their upper shafts deliberately overpaint the chest. Both forelegs carry contact seams —
  // with the paw line tucked to the body (catLegGround ≈ 0) there is no background between
  // the shafts, and the seams are what keep the pair reading as two legs (spec §Sticker pass);
  // neither is a rear leg.
  r.part = CATP_FRONT_LEGS;
  if (!legs.foreNear[0]) catDrawForeLeg(r, legs, 0, bodyRamp, true);
  if (!legs.foreNear[1]) catDrawForeLeg(r, legs, 1, bodyRamp, true);

  // ---- ears: rounded triangles whose base corners are buried in the skull, drawn BEFORE the
  // head so the head arc becomes the junction. Rotated by tilt, twitched per-ear. ----
  r.part = CATP_EARS;
  for (int e = 0; e < 2; e++) catDrawOuterEar(r, hd, hr, p, en, e, e ? q.earR : q.earL);

  // ---- head (a circle: rotation-invariant, so tilt lives in the features) ----
  // The halo is what separates skull from chest and from the ear bases buried under it.
  r.part = CATP_HEAD;
  r.contact = true;
  shEllipse(r, hxi, hyi, hr, hr, CAT_FUR_FLAT);
  r.contact = false;

  // ---- inner ears: after the head, or the skull buries them along with the outer base.
  // Built on the ear's own axis at explicit stations, NOT as a scaled copy of the outer triangle:
  // scaling by 0.5 about the centroid would put the inner base at 1/6 of the axis (the centroid is
  // at 1/3), far below the skull rim, and paint pink onto the already-drawn head.
  // Width is a fraction of the AVAILABLE width at the base station, so the fur border is
  // proportional rather than constant-width -- which is what "40-60% width" means to the eye.
  // That fraction is a LOOK, though, not the safety property: staying inside the outer ear is
  // enforced by the r.furOnly wrap on the fill (see the comment beside it), because the width
  // model is a linear taper and the true rounded boundary is a perpendicular offset.
  // What the t0 solve below actually proves: that the two BASE CORNERS (the paths P_s(t), s=+-1)
  // clear the skull rim. It does not test the straight chord between them -- when that chord
  // straddles local x=0 (near where an ear's axis crosses the head's own vertical, ~0.7% of
  // ear-cases in a broad sweep), the hull's true nearest point to the head centre is the chord
  // INTERIOR, not either tested corner. Untested, but not unguarded: the Rs margin below is a
  // clearance budget on the CORNERS, and it happens to be generous enough to absorb this case too
  // (measured worst case 1.1548 cells of clearance across a real-render sweep, still safe) --
  // covered empirically, not proven.
  r.part = CATP_EARS;
  for (int e = 0; e < 2; e++) {
    int R = catEarRad(hr), rad = catEarTipRad(hr, p.earPoint);
    float halfBase = catEarHalfBase(R, rad, p.earPoint);
    CatEar E = catEarShape(hr, p.earLen, e ? 1.0f : -1.0f, e ? q.earR : q.earL, en, halfBase);
    int irad = rad - 2; if (irad < 1) irad = 1;
    float bmx = 0.5f * (E.blx + E.brx);
    float hbs = 0.5f * (E.brx - E.blx); if (hbs < 0.0f) hbs = -hbs;   // projected half-width
    float ax = E.tx - bmx, ay = E.ty - E.by;
    // t0 must clear the skull rim BY CONSTRUCTION, not by a tuned literal: a fixed 0.62 (this
    // axis's old single-root-point tuning) turns out to land BEFORE the true crossing once the
    // base moved onto the sphere (Task 5's catEarShape) -- verified numerically, not just at the
    // shipped preset's yaw but even head-on (yaw 0), so it was never really "today's stations", it
    // was an inherited constant that happened not to have a test watching it. Solve instead: ihalf
    // is affine in t (both W0 and the offset are), so each base corner's own path P_s(t) is a
    // straight line, and |P_s(t)| = Rs is the one-quadratic-per-side trick catFitLen already uses
    // for the whiskers, just squared. This keeps the guarantee true for every hr/earLen/earPoint/
    // yaw the rig can produce, not only the combinations the tests happen to sample.
    float K1 = catEarInner * hbs;
    float K0 = catEarInner * (hbs + (float)rad - 1.0f - (float)irad);
    // +2: catHrot's `(int)(v + 0.5f)` truncates toward zero, not round-to-nearest, so each of the
    // rotated x and y can be pulled toward the head centre by just under one cell -- bounding the
    // distance error at sqrt(2) =~ 1.42, which 2.0 covers with room to spare (irad separately
    // covers catRTri's own corner rounding). Tilt is NOT budgeted here on purpose: catHrot's
    // rotation is exactly about the head centre, and the invariant being protected (|(x,y)| <= hr,
    // i.e. distance FROM that same centre) is preserved by rotation for free, at any tilt -- adding
    // a cushion for it would be spending pink height (this margin runs up the ear axis) on
    // something that provably cannot move the answer. Don't restore it.
    float Rs = (float)hr + (float)irad + 2.0f;
    float t0 = 0.0f;
    for (float sgn = -1.0f; sgn <= 1.0f; sgn += 2.0f) {
      float px = bmx + sgn * K0, py = E.by;
      float dx = ax - sgn * K1, dy = ay;
      float A2 = px * px + py * py, AD = px * dx + py * dy, D2 = dx * dx + dy * dy;
      float disc = AD * AD - D2 * (A2 - Rs * Rs);
      float ts;
      if (D2 <= 1e-3f) {
        // Degenerate: this corner's local axis has ~zero length, so its distance from the head
        // centre never changes with t. A2 >= Rs^2 means it's already clear (draw from the base);
        // otherwise it's stuck inside Rs regardless of t -- push to the tip as a best effort
        // (unreachable for this rig's proportions, but a real answer either way, not a skip).
        ts = (A2 >= Rs * Rs) ? 0.0f : 1.0f;
      } else if (disc < 0.0f) {
        // No real root: the quadratic (opens upward, D2 > 0) never touches zero, so it is positive
        // for every t -- this corner's path is outside Rs for its ENTIRE run, including t=0. Safe
        // from the base; the old `: 1.0f` fallback here read this as the opposite (skip the ear),
        // which is backwards -- "never enters the danger radius" is the *safe* case, not the unsafe
        // one, and it fails toward destroying the shape instead of drawing it.
        ts = 0.0f;
      } else {
        ts = (-AD + sqrtf(disc)) / D2;   // first t (from 0) where the path clears Rs; disc==0
                                          // (exact tangency) lands here too and still draws
      }
      if (ts < 0.0f) ts = 0.0f; else if (ts > 1.0f) ts = 1.0f;
      if (ts > t0) t0 = ts;
    }
    float t1 = t0 + 0.6f * (1.0f - t0);       // most of what's left of the axis, fur border near the tip
    if (t1 <= t0 + 0.02f) continue;           // no room for a band on this ear
    float W0 = hbs * (1.0f - t0) + (float)rad;                        // outer boundary at t0
    // ihalf is the HALF-WIDTH OF THE OFFSET, not the drawn half-width -- catRTri then rounds every
    // point by irad, so what actually paints is (ihalf + irad). Guarding on `ihalf` alone (an
    // earlier version did) undercounts the drawn width by exactly irad, which at the shipped
    // earPoint 0 made the guard unsatisfiable by construction and the pink never rendered at all.
    // `avail` is the width at this station with one cell of fur already reserved, and the
    // `irad > avail` skip is the LOWER guard the algebra needs and did not have: "ihalf <= avail -
    // irad, therefore ihalf + irad <= avail for any catEarInner <= 1" only bounds anything while
    // avail - irad >= 0, and that fails at small tip radii -- irad floors at 1 while rad keeps
    // shrinking with pointiness, so from rad <= 2 the rounding radius ALONE can exceed the room
    // there is. Flooring a negative ihalf at 0 then drew irad anyway, wider than the station
    // allowed. Skipping is the honest answer: an ear with no room for even pink's own corner
    // rounding has no inner ear. No minimum-width floor beyond that (fix round 3): a "too thin to
    // read as pink" cutoff killed the whole earPoint 0.75-1.0 range, where rad legitimately shrinks
    // -- exactly the ears some owners ask for. A 3-4 cell sliver inside a narrow spike IS what
    // a pointy cat ear's inner ear looks like. ihalf == 0 is a real shape too, not nothing: two
    // coincident base corners collapse catRTri's triangle test to its degenerate A==0 path, which
    // the three rounded segments alone still render as a capsule (test_rtri_degenerate_is_a_disc).
    float avail = W0 - 1.0f;                      // one cell of fur reserved, always
    if ((float)irad > avail) continue;            // no room even for the rounding radius
    float ihalf = catEarInner * (avail - (float)irad);
    if (ihalf < 0.0f) ihalf = 0.0f;               // unreachable given the guard above; cheap anyway
    int i0x, i0y, i1x, i1y, i2x, i2y;
    catHrot(hd, bmx + ax * t0 - ihalf, E.by + ay * t0, i0x, i0y);
    catHrot(hd, bmx + ax * t0 + ihalf, E.by + ay * t0, i1x, i1y);
    catHrot(hd, bmx + ax * t1,         E.by + ay * t1, i2x, i2y);
    // furOnly, same rule already used for blush and the eyes: pink is a feature ON the ear, so the
    // ear's OWN FUR is what bounds it. That makes "pink never escapes onto the background or the
    // outline ring" a MECHANISM (a cell that isn't fur or accent simply doesn't take the paint)
    // instead of an arithmetic result that has to be re-proved for every hr/earPoint/yaw the rig
    // can reach. It replaces a perpendicular-offset solve with a tuned cushion that still measured
    // real escapes at 130 of 1740 swept cases; the cost is that pink can sit flush against the fur
    // edge with no border where the width model over-reaches, instead of poking through it.
    // NOTE: furOnly does NOTHING for the skull invariant -- the skull is fur too, so pink landing
    // on the head is a perfectly legal furOnly write. The t0 solve above is the ONLY thing keeping
    // pink off the already-drawn head, and it stays load-bearing for exactly that reason.
    r.furOnly = true;
    catRTri(r, i0x, i0y, i1x, i1y, i2x, i2y, irad, CI_EAR);
    r.furOnly = false;
  }

  // ---- face: driven by headYaw (independent slew), anchors rotated by tilt ----
  // ORDERING REQUIREMENT this pass introduced: the eye, muzzle (below) and inner-ear (above) passes
  // are all furOnly-gated -- a furOnly write can only land on a cell that is ALREADY fur or accent
  // colour, which is what keeps them off the outline ring and the background without an arithmetic
  // bound that would need re-deriving for every hr/eyeShape/yaw. That means each one needs its own
  // substrate (skull fur for the eye/muzzle, ear fur for the inner ear) painted FIRST. Get the
  // ordering wrong and the failure mode is SILENT DELETION -- the feature paints nothing and raises
  // no clip flag, no diagnostic, nothing -- not misplaced pixels that would be obvious on the rig.
  r.part = CATP_FACE;
  {
    // Feature SIZES scale with the skull, not just their anchors. They were absolute, so a
    // headSize-1.4 cat got the same 5-cell eyes and 4-cell muzzle on a 40% bigger head and
    // read as a sparse balloon. hr/20 because 20 is P_IDLE's design headR.
    float fs = (float)hr / 20.0f;
    // Facing is one projection now (spec 2026-07-29): the old esp/eshift pair CANCELLED for the
    // leading eye (0.620*hr -> 0.629*hr across the whole turn) and culled the trailing one at
    // x = -0.205*hr, mid-face. Both artefacts are gone by construction.
    // The pure projection alone still widened separation versus the old baked expression (owner
    // catch, 2026-07-29: "eyes are further apart than before") -- the old code ADDITIONALLY shrank
    // separation with yaw on top of the shift, an artistic compression this model doesn't reproduce
    // and that we are not restoring (it's what caused the static-eye bug this whole pass replaced).
    // Instead the front-on azimuth itself is a rig knob, `catEyeK`, computed once to reproduce the
    // OLD separation at idle yaw (0.12): lowering the azimuth keeps the vanish-at-the-rim property
    // exactly (the vanish is always at azimuth 90, independent of a), which scaling x post-hoc would
    // not. CF_EYE itself stays the baked 0.62 constant -- the feat-table test still checks it, and
    // it documents where this knob started.
    float esa = catEyeK / CF_EYE.ce;
    // Clamp like the ear's own sd > 0.60f guard: catEyeK is a CAT_TUNE rig knob a human will scrub
    // live, and catEyeK >= CF_EYE.ce (0.994987) drives esa >= 1, taking sqrtf negative -> NaN, which
    // then poisons the whole face's x and fore through fe. 0.98 leaves comfortable slider headroom
    // above the shipped 0.592 while never reaching the singularity. Symmetric on the low side too --
    // a hand-typed negative rig value (e.g. eyeK=-1.0) drives esa <= -1 exactly the same way, and
    // sqrtf doesn't care which sign pushed it negative (final review, 2026-07-29).
    if (esa > 0.98f) esa = 0.98f; else if (esa < -0.98f) esa = -0.98f;
    CatFeat fe = { CF_EYE.ce, esa, sqrtf(1.0f - esa * esa) };
    CatAz eye[2] = { catAz((float)hr, tn, fe, -1.0f), catAz((float)hr, tn, fe, 1.0f) };
    CatAz nose = catAz((float)hr, tn, CF_NOSE, 1.0f);
    int erx0 = (int)(4.0f * p.eyeShape * fs + 0.5f);
    int ery = (int)(5.2f * q.eyeOpen * fs + 0.5f);
    // Anti-fuse: a MAXIMUM width, and only meaningful while both eyes are on the near hemisphere.
    // Past the limb the two projections converge (gap 0 at full profile), and capping against that
    // gap crushed the one surviving eye to a couple of cells.
    if (eye[0].vis && eye[1].vis) {
      float gap = eye[1].x - eye[0].x; if (gap < 0.0f) gap = -gap;
      int cap = (int)(gap * 0.5f) - 1; if (cap < 1) cap = 1;
      if (erx0 > cap) erx0 = cap;
    }
    // Blush FIRST. With the eye pass now furOnly-wrapped too (below), CI_BLUSH reads as neither
    // fur nor accent, so once a cell is blush the eye pass can no longer paint over it at all --
    // ordering plus furOnly means blush wins any overlap outright, not merely "lands clear of the
    // eye" the way arithmetic alone would only promise. Eye bottom is hr*(0.10 + 0.26*eyeOpen),
    // blush top is 0.36*hr: exactly tangent at eyeOpen == 1, so nothing overlaps today -- but a
    // future keyframe with eyeOpen > 1 would have the eye's own furOnly wrap block it from covering
    // blush, biting a blush-colored hole in the eye rather than drawing over it.
    {                                    // unconditional: gating it on eyeOpen made the cheeks
      r.furOnly = true;                  // blink out with the eyes, several times a minute
      for (int e = 0; e < 2; e++) {
        CatAz b = catAz((float)hr, tn, CF_BLUSH, e ? 1.0f : -1.0f);
        if (!b.vis) continue;            // round the far side of the skull: nothing hides it now
        int bx2, by2; catHrot(hd, b.x, (float)hr * 0.48f, bx2, by2);
        // Same floor as the eye's own erx guard below: at yaw 0.36-0.38 this rounds to 0 and
        // catEllipse's degenerate rx2==0 branch fills a full 1-wide COLUMN instead of shrinking
        // away (measured 3 cells, 1 wide x 4 tall) -- a floor of 1 keeps it a proper sliver ellipse.
        int brx2 = (int)(3.6f * fs * b.fore + 0.5f); if (brx2 < 1) brx2 = 1;
        catEllipse(r, bx2, by2, brx2, (int)(2.4f * fs + 0.5f), CI_BLUSH);
      }
      r.furOnly = false;
    }
    // furOnly: an eye is a FACE feature, so the head's own silhouette clips it. The burial
    // invariant (catAz) only guarantees the eye's ANCHOR point stays inside the skull circle, never
    // the anchor plus its drawn radius -- a wide eyeShape (or any future preset/pose) can still push
    // the rendered blob a cell past the rim. Clipping to fur is correct occlusion, not a workaround,
    // and unlike capping the radius or retuning a pose yaw it holds for every eyeShape/hr/yaw, not
    // just the ones checked today.
    r.furOnly = true;
    for (int e = 0; e < 2; e++) {
      if (!eye[e].vis) continue;                             // vanishes AT the rim, not mid-face
      int ex, ey; catHrot(hd, eye[e].x, (float)hr * 0.10f, ex, ey);
      int erx = (int)((float)erx0 * eye[e].fore + 0.5f); if (erx < 1) erx = 1;
      if (ery <= 0) {
        // Happy blink: a caret, not a bar. A closed eye drawn as a straight line reads asleep
        // or dead; the upward curve is the whole "content cat" cue.
        int bw = erx, bh = 1 + erx / 4;
        catCapsule(r, ex - bw, ey + bh, ex, ey - bh, 0, CI_EYE);
        catCapsule(r, ex, ey - bh, ex + bw, ey + bh, 0, CI_EYE);
      } else {
        // Chibi eye: a SOLID dark oval with one catchlight. The old sclera+small-pupil pair drew a
        // bright iris ring around a dark centre, which is the wide-eyed-alarm read no amount of
        // repositioning fixes. Every chibi reference is a plain dark blob.
        // Axis-aware: catCapsule's radius is isotropic, so a single call capped by the LONGER axis
        // (the old `ecap = erx>ery ? erx-ery : 0` line, pre-existing and carried verbatim by the
        // facing-pass brief) always drew a disc of radius max(erx,ery) -- foreshortening erx below
        // ery never reached the raster, harmless before `fore` fed erx, silently wrong after. Split
        // by whichever axis is currently the long one instead of always capping along x.
        if (erx >= ery) catCapsule(r, ex - (erx - ery), ey, ex + (erx - ery), ey, ery, CI_EYE);
        else            catCapsule(r, ex, ey - (ery - erx), ex, ey + (ery - erx), erx, CI_EYE);
        int px = ex + (int)(q.pupilDx + 0.5f), py = ey + (int)(q.pupilDy + 0.5f);
        if (ery >= 3) {                     // catchlight, up-left toward the lamp
          // Stadium, not a disc: catEllipse at radius 2 rasterizes as a plus with corner nubs,
          // which reads as a sparkle. A capsule is solid at every size.
          // ery/3 was picked on the rig's x1 pane, not zoomed: at ery/2 the highlight is half the
          // eye and the pair read as WHITE dots at real screen size. ery/4 collapses clr to 1,
          // cw to 0, and the capsule degenerates back to the plus.
          int clr = ery / 3; if (clr < 1) clr = 1;
          // Mirroring is an exact image flip, so the catchlight travels with the authored eye
          // just like every shading band instead of being re-lit as a separate scene.
          int cx2 = px - erx / 4, cy2 = py - ery / 3, cw = clr / 2;
          // furOnly is also live here (this whole loop is furOnly-wrapped, above); clipTo takes
          // priority in catPlot so the catchlight still paints on the eye it just drew.
          r.clipTo = CI_EYE;
          catCapsule(r, cx2 - cw, cy2, cx2 + cw, cy2, clr, CI_HILITE);
          r.clipTo = 0;
        }
      }
    }
    r.furOnly = false;
    // Whiskers: 3 per side, fanned, deliberately overhanging the silhouette — so they are NOT
    // sphere features and cannot be projected like one. Project the ROOT (which is on the skull)
    // and treat the outward part as a LENGTH, foreshortened by the cheek's own fore. The old code
    // passed `wlen` as an absolute head-local x despite its name; multiplying that by fore can
    // land a tip inside its own root.
    // The fan is SHRUNK to fit the grid rather than letting tips clip: at headYaw 0.9 on a turned
    // body it ran off the right edge, and all three unwritten poses declare yaw 0.90. Shrinking
    // beats clamping each tip, which would converge all three onto the same edge column. Now PER
    // SIDE: at high yaw the two fans have genuinely different lengths, and one shared wlen made the
    // near fan shrink to fit the far one. catFitLen is closed-form because catHrot is linear in the
    // reach.
    CatAz wroot[2] = { catAz((float)hr, tn, CF_WHISK, -1.0f), catAz((float)hr, tn, CF_WHISK, 1.0f) };
    float wlen[2];
    for (int e = 0; e < 2; e++) {
      if (!wroot[e].vis) { wlen[e] = 0.0f; continue; }       // culled below anyway -- skip the fit
      float sgn = wroot[e].x >= nose.x ? 1.0f : -1.0f;       // outward = away from the muzzle
      float want = 0.84f * (float)hr * wroot[e].fore;        // 1.42 - 0.58: today's reach, as a length
      for (int w = -1; w <= 1; w++) {
        float fy = (float)hr * 0.42f + (float)w * 6.0f * fs + twitchDy * fs;
        float lx = catFitLen(hd.cx + wroot[e].x * hd.ct - fy * hd.st, sgn * hd.ct, 1.0f, (float)CAT_GW - 2.0f);
        float ly = catFitLen(hd.cy + wroot[e].x * hd.st + fy * hd.ct, sgn * hd.st, 1.0f, (float)CAT_GH - 2.0f);
        if (lx < want) want = lx;
        if (ly < want) want = ly;
      }
      wlen[e] = want > 0.0f ? want : 0.0f;
    }
    for (int e = 0; e < 2; e++) {
      if (!wroot[e].vis) continue;
      float sgn = wroot[e].x >= nose.x ? 1.0f : -1.0f;
      for (int w = -1; w <= 1; w++) {
        int w0x, w0y, w1x, w1y;
        catHrot(hd, wroot[e].x, (float)hr * 0.46f + (float)w * 3.2f * fs, w0x, w0y);
        catHrot(hd, wroot[e].x + sgn * wlen[e],
                (float)hr * 0.46f + (float)w * 5.0f * fs + twitchDy * fs, w1x, w1y);
        catCapsule(r, w0x, w0y, w1x, w1y, 0, CI_FUR_0);
      }
    }
    // Muzzle LAST: in a head-on view the whisker roots pass behind the projecting nose and
    // mouth, not over them. This completes the visible far-to-near face stack:
    // head -> ear detail -> eyes -> whiskers -> nose/mouth.
    // x-radii only: nw/mw foreshorten by nose.fore because a horizontal turn narrows the muzzle;
    // nh/mh are vertical extents and a horizontal turn doesn't touch them.
    int nw = (int)(2.6f * fs * nose.fore + 0.5f), nh = (int)(2.4f * fs + 0.5f);
    int mw = (int)(3.2f * fs * nose.fore + 0.5f), mh = (int)(2.2f * fs + 0.5f);
    int nx, ny; catHrot(hd, nose.x, (float)hr * 0.34f, nx, ny);
    // furOnly: a muzzle is a face feature too, same rule as the eye above -- the silhouette clips
    // it, not an arithmetic bound. nose.x is projected onto the rim at profile, but the mouth draws
    // several rows below that anchor, well outside the head circle; foreshortening nw/mw alone
    // measured short (12 -> 9 bad edge cells at yaw 0.90). The pair -- foreshorten AND clip -- is
    // what reaches 0.
    r.furOnly = true;
    catTri(r, nx - nw, ny, nx + nw, ny, nx, ny + nh, CI_NOSE);
    // One muzzle construction for every expression, taken from the reference strip:
    //   tiny nose -> short philtrum -> compact omega smile
    //                                \-> dark lower bowl + pink tongue as it opens
    // Keeping the upper mouth present makes opening a deformation of the same face instead of
    // the old hard swap from a "w" to an unrelated black hole.
    int joinY = ny + nh + 2;                               // a little daylight below the nose
    int smileW = (int)(mw * 0.78f + 0.5f); if (smileW < 2) smileW = 2;
    int smileMid = (int)(smileW * 0.55f + 0.5f); if (smileMid < 1) smileMid = 1;
    int smileDrop = (int)(mh * 0.70f + 0.5f); if (smileDrop < 1) smileDrop = 1;
    // catCapsule radius 1 is a THREE-cell stroke. At the shipped fs ~= 1.4 that was heavier than
    // the one-cell silhouette rim and turned the little omega into a moustache. Save it for heads
    // substantially larger than the authored preset band.
    int mouthStroke = fs > 1.75f ? 1 : 0;

    if (q.mouthOpen > 0.05f) {
      float u = q.mouthOpen / 0.50f; if (u > 1.0f) u = 1.0f;
      // `mouthH` is a HEIGHT, not a radius. The old code used its value as ry while moving the
      // centre by only mouthH/2, so one third of every "downward" opening actually grew UP around
      // the nose. Fix the top at joinY+1 and derive centre/radius from the total height.
      int mouthH = (int)(q.mouthOpen * 14.0f * fs + 0.5f); if (mouthH < 2) mouthH = 2;
      int openRy = mouthH / 2; if (openRy < 1) openRy = 1;
      // The +1 is the hardware readability allowance: at x1 the authored meow otherwise leaves
      // only a tiny dark rim once the tongue is present. Closed-mouth geometry stays delicate.
      int openW = (int)((float)mw * (0.45f + 0.55f * u) + 0.5f) + 1;
      if (openW < 1) openW = 1;
      int openTop = joinY + 1, openCy = openTop + openRy;
      // Tiny heads have only a few integer rows between muzzle and chin. Clamp the radius rather
      // than moving the top: the expression keeps its anchor, but never paints the silhouette's
      // last row and merges the mouth into the outline.
      int chinY = (int)floorf(hd.cy + (float)hr - 1.0f);
      int maxOpenRy = (chinY - openTop) / 2;
      if (maxOpenRy < 1) maxOpenRy = 1;
      if (openRy > maxOpenRy) { openRy = maxOpenRy; openCy = openTop + openRy; }
      catEllipse(r, nx, openCy, openW, openRy, CI_MOUTH);

      // A tongue turns the opening from a void into the pink, soft "meow/yawn" bowl used by the
      // references. On the physical 128px panel a one-row crescent disappears, while a centred
      // ellipse reads as a pink button. Use a little tapered wedge instead: 2 rows at the onset,
      // 3 once the bowl has room, widest at the top and with one dark mouth row left underneath.
      // Reuse blush pink (no new palette slot), and clip to the bowl so it cannot escape onto fur.
      if (mouthH >= 5) {
        int tongueRows = openRy >= 3 ? 3 : 2;
        int tongueHalf = openW - 1; if (tongueHalf < 1) tongueHalf = 1;
        int tongueBottom = openCy + openRy - 1;
        r.clipTo = CI_MOUTH;
        for (int row = 0; row < tongueRows; row++) {
          int half = tongueHalf - row; if (half < 1) half = 1;
          int y = tongueBottom - tongueRows + 1 + row;
          catCapsule(r, nx - half, y, nx + half, y, 0, CI_BLUSH);
        }
        r.clipTo = 0;
      }
    }

    // The philtrum and two rounded smile lobes are shared by closed and open expressions.
    // Four short one-cell segments read smoother at x1 than two steep diagonals, while the outer
    // endpoints returning toward joinY keep the expression friendly instead of frowning.
    catCapsule(r, nx, ny + nh, nx, joinY, mouthStroke, CI_MOUTH);
    int outerY = joinY + smileDrop / 2;
    for (int s = -1; s <= 1; s += 2) {
      catCapsule(r, nx, joinY, nx + s * smileMid, joinY + smileDrop,
                 mouthStroke, CI_MOUTH);
      catCapsule(r, nx + s * smileMid, joinY + smileDrop, nx + s * smileW, outerY,
                 mouthStroke, CI_MOUTH);
    }
    r.furOnly = false;
    // ---- muzzle blaze (marking 3): the light mask over the nose and mouth, the head's own half
    // of the coat. Drawn LAST, after every feature, for the same reason the forehead ticks are:
    // remapLight lifts band 0-1 fur to CI_HILITE, which is NOT on the fur ramp, so a blaze painted
    // BEFORE the face turns the whole muzzle into a surface the furOnly-gated passes refuse to
    // draw on -- the nose disappeared outright (caught by test_open_mouth_limits). After the face,
    // remap's fur-only rule instead steps around the finished nose, mouth and blush by
    // construction, and lightens only the fur between them.
  //
    // It is a TRIANGLE apex-up, not an ellipse: the blaze narrows to a point running up the
    // bridge and widens across the muzzle, and a rounded blob at this size read as a moustache.
    //
    // The disc clip is load-bearing, not tidiness. Past the chin the only thing under the brush
    // is BODY fur, and remapLight would lighten it straight into the chest bib -- welding the two
    // halves of the coat into one blob, which is the one thing this marking must not do. Index
    // clipping cannot help: head and body are the same four fur bands, so the cut has to be
    // geometric.
    if (p.marking == 3) {
      CatAz btip = catAz((float)hr, tn, CF_BLAZE_TIP, 1.0f);
      CatAz bl   = catAz((float)hr, tn, CF_BLAZE, -1.0f);
      CatAz brr  = catAz((float)hr, tn, CF_BLAZE,  1.0f);
      int tx, ty, lx, ly, rx2, ry2;
      catHrot(hd, btip.x, (float)hr * -0.08f, tx, ty);
      // The corners take their X from the 0.72 station -- a real point on the skull, so it
      // foreshortens with the turn like every other feature -- but their Y from the chin. A base
      // that wide is not ON the sphere that low (at yf 1.00 the skull has no width left, and
      // asin(k/ce) would not even be defined), so the shape is deliberately not a spherical patch
      // down there; the disc clip below is what gives it its true bottom edge.
      catHrot(hd, bl.x,   (float)hr *  1.00f, lx, ly);
      catHrot(hd, brr.x,  (float)hr *  1.00f, rx2, ry2);
      int brad = hr > 24 ? 3 : 2;                    // corner rounding scales with the skull
      r.clipCx = hxi; r.clipCy = hyi; r.clipR = hr;  // cut at the base of the head
      r.remap = true; r.remapLight = true;
      catRTri(r, tx, ty, lx, ly, rx2, ry2, brad, CI_FUR_0);   // idx ignored: remap decides
      r.remap = false; r.remapLight = false; r.clipR = 0;
    }

    if (p.marking == 1 || p.marking == 3) {                  // tabby forehead ticks. Last pass:
      r.remap = true;                                        // remap recolors fur only, so the
      for (int i = -1; i <= 1; i++) {                        // eyes/nose already drawn are safe.
        CatTickArc tk = i ? catTickArc((float)hr, tn, CF_TICK, (float)i)
                          : catTickArc((float)hr, tn, CF_TICK0, 1.0f);
        if (!tk.vis) continue;
        int tx[3], ty[3];
        static const float TY[3] = { -0.88f, -0.72f, -0.56f };
        for (int k = 0; k < 3; k++) catHrot(hd, tk.x[k], (float)hr * TY[k], tx[k], ty[k]);
        int tr = fs > 1.15f ? 2 : 1;
        catCapsule(r, tx[0], ty[0], tx[1], ty[1], tr, CI_ACC_1);
        catCapsule(r, tx[1], ty[1], tx[2], ty[2], tr, CI_ACC_1);
      }
      r.remap = false;
    }
  }

  // ---- raised foreleg: LAST, after the face. A paw held at the mouth is the nearest thing on
  // the cat, and drawn in the normal front-leg plane the head paints straight over it -- the
  // licking pose played as a cat with one leg missing. Its rim goes here for the same reason.
  // The paw is clamped out of the upper half of the head in catProjectLegs, so this can only
  // ever cover muzzle, never the eyes. ----
  for (int e = 0; e < 2; e++) {
    if (!legs.foreNear[e]) continue;
    r.part = CATP_FRONT_LEGS;
    if (p.outline) { r.rim = true; catDrawForeLeg(r, legs, e, bodyRamp, false, true); r.rim = false; }
    catDrawForeLeg(r, legs, e, bodyRamp, true, true);
  }
}
