#include <unity.h>
#include <cmath>
#include <cstring>
#include <string>
#include "../../tap_replay.h"

// A synthetic tap line: bin0=0x0a3c, bin8=0xbeef, rest zero.
static std::string mkLine(uint32_t ms) {
  char hex[NUM_FREQS * 4 + 1];
  memset(hex, '0', sizeof hex - 1); hex[sizeof hex - 1] = 0;
  memcpy(hex + 0 * 4, "0a3c", 4);
  memcpy(hex + 8 * 4, "beef", 4);
  return "tap " + std::to_string(ms) + " " + hex;
}

void test_parse_good_line() {
  TapPacket p;
  TEST_ASSERT_TRUE(parseTapLine(mkLine(10432).c_str(), p));
  TEST_ASSERT_EQUAL_UINT32(10432, p.ms);
  TEST_ASSERT_EQUAL_UINT16(0x0a3c, p.mags.spectrogram[0]);   // big-endian hex order (spec §1)
  TEST_ASSERT_EQUAL_UINT16(0xbeef, p.mags.spectrogram[8]);
  TEST_ASSERT_EQUAL_UINT16(0, p.mags.spectrogram[63]);
}

void test_parse_rejects_malformed() {
  TapPacket p;
  TEST_ASSERT_FALSE(parseTapLine("prof 123 abc", p));        // wrong prefix
  TEST_ASSERT_FALSE(parseTapLine("tap 123 abcd", p));        // short hex
  std::string bad = mkLine(1); bad[10] = 'g';                // non-hex char mid-payload
  TEST_ASSERT_FALSE(parseTapLine(bad.c_str(), p));
  std::string longer = mkLine(1) + "00";                     // trailing garbage
  TEST_ASSERT_FALSE(parseTapLine(longer.c_str(), p));
}

void test_parse_text_and_header() {
  std::string text =
    "# {\"secs\":10.0,\"expect\":{\"kicks_per_s\":2.0,\"snare_ioi_ms\":null},\"pattern\":\"kick\",\"role\":\"constraint\"}\n"
    + mkLine(100) + "\n" + mkLine(105) + "\n";
  TapCapture cap;
  TEST_ASSERT_TRUE(parseTapText(text, cap));
  TEST_ASSERT_EQUAL_INT(2, (int)cap.pkts.size());
  // plain == on purpose: parsed literals compare exactly, and Unity's *_DOUBLE asserts need a
  // UNITY_INCLUDE_DOUBLE config PlatformIO doesn't guarantee
  TEST_ASSERT_TRUE(tapHeaderNum(cap.header, "secs") == 10.0);
  TEST_ASSERT_TRUE(tapHeaderNum(cap.header, "kicks_per_s") == 2.0);
  TEST_ASSERT_TRUE(std::isnan(tapHeaderNum(cap.header, "snare_ioi_ms")));  // JSON null -> NaN
  TEST_ASSERT_TRUE(std::isnan(tapHeaderNum(cap.header, "nope")));          // missing -> NaN
  TEST_ASSERT_EQUAL_STRING("kick", tapHeaderStr(cap.header, "pattern").c_str());
  TEST_ASSERT_EQUAL_STRING("constraint", tapHeaderStr(cap.header, "role").c_str());
  // A capture file contains ONLY the header + tap lines (the driver filters [prof]/JSON);
  // anything else means a corrupt file and must be rejected, not skipped.
  TapCapture junk;
  TEST_ASSERT_FALSE(parseTapText(std::string("[prof] id=32 fps=58\n") + mkLine(1) + "\n", junk));
}

// The driver writes headers with python json.dumps, which spaces its separators: `"role": "x"`.
// A compact-only string matcher returns "" here while the NUMBERS still parse (strtod skips
// whitespace) -- the corpus then looks fine to the golden tests and is rejected by the sweep.
void test_header_tolerates_spaced_json() {
  std::string spaced =
    "{\"pattern\": \"kick\", \"secs\": 10.0, \"expect\": {\"snares_per_s\": 0.0}, \"role\": \"constraint\"}";
  TEST_ASSERT_EQUAL_STRING("kick", tapHeaderStr(spaced, "pattern").c_str());
  TEST_ASSERT_EQUAL_STRING("constraint", tapHeaderStr(spaced, "role").c_str());
  TEST_ASSERT_TRUE(tapHeaderNum(spaced, "secs") == 10.0);
  TEST_ASSERT_TRUE(tapHeaderNum(spaced, "snares_per_s") == 0.0);
  TEST_ASSERT_EQUAL_STRING("", tapHeaderStr(spaced, "nope").c_str());   // missing key stays empty
}

// Synthetic beats at ~175 pkt/s: `hits` transients, `periodMs` apart, ~60ms wide, in the bass band
// (bins 0-7) or the mid band. The mid case lights the ACTUAL MID_BIN_LO..HI, not a hardcoded range:
// this test asserts "a transient in the mid band fires a snare", and hardcoding bins 8-23 made it
// break the moment the band moved to 18-31 (the tuning sweep) even though the property still held.
static TapCapture mkBeats(bool bassBand, uint32_t periodMs, int hits) {
  TapCapture cap;
  uint32_t t = 0;
  for (int h = 0; h < hits; h++) {
    for (int f = 0; f < (int)(periodMs / 6); f++) {
      TapPacket p; p.ms = t; memset(&p.mags, 0, sizeof p.mags);
      if (f < 10) {
        int lo = bassBand ? 0 : MID_BIN_LO, hi = bassBand ? 8 : MID_BIN_HI;
        for (int i = lo; i < hi; i++) p.mags.spectrogram[i] = 60000;
      }
      cap.pkts.push_back(p);
      t += 6;
    }
  }
  return cap;
}

void test_replay_kick_fires_no_snare() {
  ReplayResult r = replayCapture(mkBeats(true, 500, 6));     // 3s of kicks at 2/s
  TEST_ASSERT_TRUE(r.kicks >= 5);
  TEST_ASSERT_EQUAL_INT(0, r.snares);
}

void test_replay_snare_fires() {
  ReplayResult r = replayCapture(mkBeats(false, 1000, 4));   // 4s of snares at 1/s, bass silent
  TEST_ASSERT_TRUE(r.snares >= 3);
  TEST_ASSERT_EQUAL_INT(r.snares, (int)r.snareMs.size());
  TEST_ASSERT_EQUAL_INT(0, r.kicks);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_good_line);
  RUN_TEST(test_parse_rejects_malformed);
  RUN_TEST(test_parse_text_and_header);
  RUN_TEST(test_header_tolerates_spaced_json);
  RUN_TEST(test_replay_kick_fires_no_snare);
  RUN_TEST(test_replay_snare_fires);
  return UNITY_END();
}
