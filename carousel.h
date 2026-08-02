#pragma once
#include <stdint.h>
#include <math.h>

// Pure scroll model for the animation carousel (spec 2026-08-01). No Arduino, no gfx, no
// millis() -- main.cpp owns the drawing and the clock, this owns the physics, and that split
// is what makes the only non-trivial part of the feature testable on the Mac.
//
// Position is in ITEMS, fractional, wrapping. Visual spacing equals ITEM_W so a drag tracks
// the finger 1:1.
struct Carousel {
  static constexpr float ITEM_W = 120.0f;   // px of finger travel per item
  static constexpr float DECAY  = 0.3f;     // velocity multiplier per SECOND (see tick)
  static constexpr float V_MAX  = 40.0f;    // items/s ceiling -- ~3x a firm flick
  static constexpr float V_SNAP = 0.5f;     // below this, stop coasting and snap
  static constexpr float SNAP_K = 12.0f;    // snap rate, fraction of remaining error per second
  static constexpr int   MAX_N  = 64;       // favourites are a 64-bit mask; nothing can exceed this
  static constexpr float STALL_S = 0.1f;    // finger down but motionless this long -> no flick to inherit

  uint8_t ids[MAX_N];
  int   n        = 0;
  float p        = 0.0f;    // fractional item position, always in [0, n)
  float v        = 0.0f;    // items/sec
  bool  dragging = false;
  int   lastX    = 0;
  float accum    = 0.0f;    // items dragged since the last velocity recompute
  float accumDt  = 0.0f;    // seconds those items accumulated over

  void open(const uint8_t* list, int count, uint8_t curId) {
    n = count < 0 ? 0 : (count > MAX_N ? MAX_N : count);
    for (int i = 0; i < n; i++) ids[i] = list[i];
    p = 0.0f; v = 0.0f; dragging = false; lastX = 0; accum = 0.0f; accumDt = 0.0f;
    for (int i = 0; i < n; i++) if (ids[i] == curId) { p = (float)i; break; }
  }

  // Finger sample. The first call after a finger-down only latches the origin -- there is no
  // delta yet, and treating the touch-down point as a delta is exactly the jump that a
  // non-atomic cross-task read used to produce.
  void drag(int x) {
    if (!dragging) { dragging = true; lastX = x; accum = 0.0f; return; }
    // Drag LEFT (x decreasing) moves the list forward, like every physical carousel.
    float d = (float)(lastX - x) / ITEM_W;
    lastX = x;
    p = wrap(p + d);
    accum += d;
  }

  // Finger up. v already holds the last tick's drag velocity, so a finger held still before
  // release coasts nowhere -- which is the correct behaviour, not an edge case.
  void release() { dragging = false; accum = 0.0f; accumDt = 0.0f; }

  void tick(float dt) {
    if (dt <= 0.0f || n <= 1) return;
    if (dragging) {
      // Cadence-independent on purpose. loop() calls carouselUpdate() several times per rendered
      // frame, but touchPoll() publishes a new sample only every 10ms, so most ticks carry no new
      // motion. Dividing by THIS tick's dt on such a tick yields v=0 and destroys the latched
      // flick -- measured as 0 coast for roughly half of all finger-up timings. Accumulate the
      // elapsed time alongside the motion and only recompute when motion actually arrived.
      accumDt += dt;
      if (accum != 0.0f) {
        v = accum / accumDt;
        accum = 0.0f;
        accumDt = 0.0f;
        if (v >  V_MAX) v =  V_MAX;
        if (v < -V_MAX) v = -V_MAX;
      } else if (accumDt >= STALL_S) {
        v = 0.0f;              // a held-still finger must not coast on a stale velocity
        accumDt = 0.0f;
      }
      return;
    }
    if (fabsf(v) >= V_SNAP) {             // coasting
      p = wrap(p + v * dt);
      // Time-based, NOT per-frame: fps swings from ~30 to 58 (uncapped audio modes), and a
      // per-frame multiplier would make the same flick travel twice as far in Bloom.
      v *= powf(DECAY, dt);
      if (fabsf(v) < V_SNAP) v = 0.0f;
      return;
    }
    v = 0.0f;
    float target = nearest();             // snapping
    float e = target - p;
    float k = SNAP_K * dt;
    if (k > 1.0f) k = 1.0f;
    if (fabsf(e) < 0.001f) { p = wrap(target); return; }
    p = wrap(p + e * k);
  }

  float pos() const { return p; }
  float velocity() const { return v; }

  bool moving() const {
    if (n <= 1) return false;
    if (dragging || v != 0.0f) return true;
    return fabsf(p - nearest()) > 0.001f;
  }

  uint8_t settledId() const {
    if (n <= 0 || moving()) return 0xFF;
    int i = (int)lroundf(p);
    i %= n; if (i < 0) i += n;
    return ids[i];
  }

private:
  // Nearest item centre in UNWRAPPED space, so a p of 4.9 with n=5 snaps toward 5 (== 0 after
  // wrap) rather than being dragged all the way back to 4.
  float nearest() const { return roundf(p); }

  float wrap(float x) const {
    if (n <= 1) return 0.0f;             // a one-item list cannot scroll
    x = fmodf(x, (float)n);
    if (x < 0.0f) x += (float)n;
    return x;
  }
};
