// Offline grid sweep of the snare detector constants over .tap captures
// Links the REAL
// audio.cpp -- a reimplementation would be the thing under test drifting from the thing shipped.
//
//   c++ -std=c++17 -I. tools/snare_sweep.cpp audio.cpp -o /tmp/snare_sweep
//   /tmp/snare_sweep test/fixtures/*.tap
//
// Objective is LEXICOGRAPHIC, never a weighted sum -- recall must not buy false positives:
//   1. zero snares on every role:"constraint" capture (kick/hats/rumble), else eliminated
//   2. maximize recall on role:"recall" captures (fired/expected, capped at 1.0 -- overfire
//      earns nothing here; it surfaces in tier 3 as jitter instead)
//   3. minimize jitter: median absolute deviation of snare inter-onset intervals vs the
//      header's snare_ioi_ms (catches double-fire+miss patterns that count correctly)
// role:"report" captures (coincident) are printed per config, never scored -- the 1:1 default
// fails that case by design; it exists to MEASURE the scale bias, not to constrain it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../tap_replay.h"

struct Cap { std::string name, role; TapCapture cap; double secs, kicksPerS, snaresPerS, ioiMs; };

struct Score {
  bool ok = true;       // tier 1
  double recall = 0;    // tier 2, mean over recall captures
  double jitter = 0;    // tier 3, mean MAD in ms
  bool betterThan(const Score& o) const {
    if (ok != o.ok) return ok;
    if (recall != o.recall) return recall > o.recall;
    return jitter < o.jitter;
  }
};

static double madIoi(const std::vector<uint32_t>& ms, double period) {
  if (ms.size() < 2 || !(period > 0)) return 0;
  std::vector<double> dev;
  for (size_t i = 1; i < ms.size(); i++)
    dev.push_back(std::fabs((double)(ms[i] - ms[i - 1]) - period));
  std::sort(dev.begin(), dev.end());
  return dev[dev.size() / 2];
}

static Score scoreConfig(const std::vector<Cap>& caps, const ReplayConfig& c) {
  Score s;
  int nRecall = 0;
  for (const Cap& k : caps) {
    ReplayResult r = replayCapture(k.cap, c);
    if (k.role == "constraint" && r.snares > 0) { s.ok = false; return s; }
    if (k.role == "recall" && k.snaresPerS > 0) {
      double expect = k.snaresPerS * k.secs;
      s.recall += std::min((double)r.snares, expect) / expect;
      s.jitter += madIoi(r.snareMs, k.ioiMs);
      nRecall++;
    }
  }
  if (nRecall) { s.recall /= nRecall; s.jitter /= nRecall; }
  return s;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: snare_sweep <capture.tap> ...\n"); return 2; }
  std::vector<Cap> caps;
  for (int i = 1; i < argc; i++) {
    Cap k;
    if (!loadTapCapture(argv[i], k.cap)) { fprintf(stderr, "bad capture: %s\n", argv[i]); return 2; }
    k.name       = argv[i];
    k.role       = tapHeaderStr(k.cap.header, "role");
    k.secs       = tapHeaderNum(k.cap.header, "secs");
    k.kicksPerS  = tapHeaderNum(k.cap.header, "kicks_per_s");
    k.snaresPerS = tapHeaderNum(k.cap.header, "snares_per_s");
    k.ioiMs      = tapHeaderNum(k.cap.header, "snare_ioi_ms");   // NaN for null: madIoi returns 0
    if (k.role.empty() || !(k.secs > 0)) { fprintf(stderr, "bad header: %s\n", argv[i]); return 2; }
    caps.push_back(std::move(k));
  }

  // The grid. Edit freely -- this file is the bench, not the firmware. midHi caps at 32: the
  // console auto-ranges in two zones split at bin 32 (audio.h), a band straddling it drifts.
  //
  // Ranges widened past the spec's first draft after its own result pinned three axes to their
  // grid MAXIMUM (midLo 16, floor 50, rise 20) -- an optimum on the boundary is an optimum you
  // have not bracketed. These ranges bracket it: the viable region for each now has grid on both
  // sides of it, so the chosen point is interior, not an artifact of where the grid stopped.
  const uint8_t  LOs[]    = {8, 12, 16, 18, 20, 22};
  const uint8_t  HIs[]    = {24, 28, 32};
  const uint8_t  FLOORs[] = {40, 50, 60, 70, 80};
  const uint8_t  RISEs[]  = {15, 20, 25, 30, 35};
  const uint8_t  DIVs[]   = {4, 6, 8, 10};
  const uint16_t REFRs[]  = {80, 100, 120};
  const int      NUMs[]   = {1, 2, 3};
  const int      DENs[]   = {1, 2, 3, 4};

  auto gcd = [](int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; };

  struct Row { ReplayConfig c; Score s; };
  std::vector<Row> rows;
  for (uint8_t lo : LOs) for (uint8_t hi : HIs) { if (hi <= lo) continue;
    for (uint8_t fl : FLOORs) for (uint8_t ri : RISEs) for (uint8_t dv : DIVs)
    for (uint16_t re : REFRs) for (int nu : NUMs) for (int de : DENs) {
      if (gcd(nu, de) != 1) continue;                      // 2:2, 2:4 == 1:2 -- same ratio, don't rank it twice
      ReplayConfig c;
      c.midLo = lo; c.midHi = hi; c.floorV = fl; c.rise = ri;
      c.marginDiv = dv; c.refractoryMs = re; c.num = nu; c.den = de;
      rows.push_back({c, scoreConfig(caps, c)});
    }
  }
  std::sort(rows.begin(), rows.end(),
            [](const Row& a, const Row& b) { return a.s.betterThan(b.s); });

  ReplayConfig def;                                        // audio.h as shipped
  Score defS = scoreConfig(caps, def);

  auto print = [&](const char* tag, const ReplayConfig& c, const Score& s) {
    printf("%-8s bins %2d-%-2d floor %3d rise %2d div %2d refr %3d w %d:%d | %s recall %.2f jitter %6.1fms",
           tag, c.midLo, c.midHi, c.floorV, c.rise, c.marginDiv, c.refractoryMs, c.num, c.den,
           s.ok ? "ok  " : "DEAD", s.recall, s.jitter);
    for (const Cap& k : caps)
      if (k.role == "report") {
        ReplayResult r = replayCapture(k.cap, c);
        printf(" | %s %d/%.0f", k.name.c_str(), r.snares, k.snaresPerS * k.secs);
      }
    printf("\n");
  };

  print("DEFAULT", def, defS);
  for (int i = 0; i < (int)rows.size() && i < 10; i++)
    print(i ? "" : "BEST", rows[i].c, rows[i].s);

  // Which BANDS are viable at all, and how forgiving is each? The top-10 above can all sit on one
  // knife edge; a band where most floor/rise/div/refr/weight combos survive is a band whose
  // constants are not load-bearing. Pick from the wide part of the space, not the first row.
  printf("\nviable configs per mid band (of %d per band):\n", (int)(5*5*4*3*9));
  for (uint8_t lo : LOs) for (uint8_t hi : HIs) {
    if (hi <= lo) continue;
    int ok = 0, tot = 0;
    for (const Row& r : rows)
      if (r.c.midLo == lo && r.c.midHi == hi) { tot++; if (r.s.ok) ok++; }
    if (!tot) continue;
    printf("  bins %2d-%-2d  %4d/%-4d ok  %s\n", lo, hi, ok, tot,
           ok ? std::string(ok * 40 / tot, '#').c_str() : "");
  }

  printf("\nper-capture at DEFAULT:\n");
  for (const Cap& k : caps) {
    ReplayResult r = replayCapture(k.cap, def);
    printf("  %-30s role %-10s kicks %3d (expect %3.0f)  snares %3d (expect %3.0f)  veto %d refr %d\n",
           k.name.c_str(), k.role.c_str(), r.kicks, k.kicksPerS * k.secs,
           r.snares, k.snaresPerS * k.secs, r.vetoes, r.refr);
  }
  return 0;
}
