#pragma once
// Host-side loader + replay for .tap captures -- the snare tuning rig
// (docs/superpowers/specs/2026-07-14-snare-tuning-rig-design.md). One parser shared by
// tools/snare_sweep.cpp and test/test_audio_capture so the two can never disagree about the
// format. NEVER compiled into firmware (build_src_filter excludes tools/ and test/; a root
// header costs nothing). Arduino-free, like audio.*.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "audio.h"

struct TapPacket { uint32_t ms; SbStreamMags mags; };

struct TapCapture {
  std::string header;               // the "# {...}" line, from the '{' onward
  std::vector<TapPacket> pkts;
};

// "tap <millis> <256 hex chars>" -> packet. Strict: exactly 64 big-endian uint16s, nothing
// trailing but the line ending. A malformed line means a corrupt capture, never "skip it".
inline bool parseTapLine(const char* line, TapPacket& out) {
  if (strncmp(line, "tap ", 4) != 0) return false;
  char* end = nullptr;
  unsigned long ms = strtoul(line + 4, &end, 10);
  if (end == line + 4 || *end != ' ') return false;
  const char* hex = end + 1;
  memset(&out.mags, 0, sizeof out.mags);
  for (int i = 0; i < NUM_FREQS; i++) {
    uint16_t v = 0;
    for (int n = 0; n < 4; n++) {
      char c = hex[i * 4 + n];
      int d = (c >= '0' && c <= '9') ? c - '0'
            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if (d < 0) return false;                      // includes hitting NUL early: short line
      v = (uint16_t)((v << 4) | d);
    }
    out.mags.spectrogram[i] = v;
  }
  char tail = hex[NUM_FREQS * 4];
  if (tail != '\0' && tail != '\n' && tail != '\r') return false;
  out.ms = (uint32_t)ms;
  return true;
}

// Whole capture: optional "# {json}" header line(s), then tap lines. Empty lines tolerated.
inline bool parseTapText(const std::string& text, TapCapture& out) {
  out.header.clear(); out.pkts.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line[0] == '#') {
      size_t b = line.find('{');
      if (b != std::string::npos) out.header = line.substr(b);
      continue;
    }
    TapPacket p;
    if (!parseTapLine(line.c_str(), p)) return false;
    out.pkts.push_back(p);
  }
  return !out.pkts.empty();
}

inline bool loadTapCapture(const char* path, TapCapture& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  std::string text;
  char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  return parseTapText(text, out);
}

// Numeric header field. Keys are globally unique in the flat header ("secs", "kicks_per_s",
// the nested "expect" members). ponytail: substring extractor, not a JSON parser --
// tools/snare_capture.py machine-writes the format and owns it. NaN = missing or JSON null.
inline double tapHeaderNum(const std::string& header, const char* key) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = header.find(k);
  if (p == std::string::npos) return nan("");
  const char* s = header.c_str() + p + k.size();
  char* end = nullptr;
  double v = strtod(s, &end);
  return end == s ? nan("") : v;   // strtod("null") converts nothing -> NaN, exactly right
}

// String header field ("pattern", "role"). Empty string when missing. Tolerates whitespace after
// the colon: python's json.dumps defaults to `"role": "constraint"` (spaced), and a parser that
// only matched the compact form returned "" for every string field while the numbers -- strtod
// skips leading space -- kept working. That asymmetry is exactly how a corpus looks fine to the
// golden tests and is rejected by the sweep, so match both forms here.
inline std::string tapHeaderStr(const std::string& header, const char* key) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = header.find(k);
  if (p == std::string::npos) return "";
  size_t b = p + k.size();
  while (b < header.size() && (header[b] == ' ' || header[b] == '\t')) b++;
  if (b >= header.size() || header[b] != '"') return "";
  b++;
  size_t e = header.find('"', b);
  return e == std::string::npos ? "" : header.substr(b, e - b);
}

// The snare-side constants under sweep. Defaults mirror audio.h exactly, so a
// default-constructed ReplayConfig IS the shipped detector.
struct ReplayConfig {
  uint8_t  midLo = MID_BIN_LO, midHi = MID_BIN_HI;
  uint8_t  floorV = SNARE_MID_FLOOR, rise = SNARE_MID_RISE, marginDiv = SNARE_MARGIN_DIV;
  uint16_t refractoryMs = SNARE_REFRACTORY_MS;
  int      num = SNARE_VS_KICK_NUM, den = SNARE_VS_KICK_DEN;
};

struct ReplayResult {
  int kicks = 0, snares = 0, vetoes = 0, refr = 0;
  std::vector<uint32_t> snareMs;   // snare fire times, for the jitter objective
};

// Mirrors onEspNowRecv's detector block (main.cpp) LINE FOR LINE -- score both bands before
// either update() folds the packet in, kick fires unconditionally, snare must out-jump it
// (weighted). If that block changes shape, this must change with it; the golden tests over the
// committed fixtures are what make any drift visible.
inline ReplayResult replayCapture(const TapCapture& cap, const ReplayConfig& c = ReplayConfig()) {
  BeatDetector kickDet;                                            // stock bass detector (BEAT_*)
  BeatDetector midDet(c.floorV, c.rise, c.refractoryMs, c.marginDiv);
  ReplayResult r;
  for (const TapPacket& p : cap.pkts) {
    BloomParams b = bloomParamsFromMags(p.mags, c.midLo, c.midHi);
    int bassS = kickDet.score(b.bass), midS = midDet.score(b.mid);
    bool snareWins = midS * c.num > bassS * c.den;
    bool kick = kickDet.update(b.bass, p.ms);
    bool mid  = midDet.update(b.mid, p.ms);
    if (kick)             r.kicks++;
    if (mid && snareWins) { r.snares++; r.snareMs.push_back(p.ms); }
    else if (mid)         r.vetoes++;
    else if (midS > 0 && snareWins) r.refr++;
  }
  return r;
}
