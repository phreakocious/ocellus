#include <unity.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "../../tap_replay.h"

// Committed captures -> the REAL detector chain -> tolerance bands from each header's ground
// truth (spec section 5). Zero expected means zero fired; nonzero gets expected +/- max(1, 20%) --
// the tap starts after play, so window truncation can legitimately clip one edge event.
// coincident.tap is committed for the sweep but carries NO count assertion: its pass/fail flips
// with SNARE_VS_KICK_NUM, and a golden test must not depend on a tuning outcome.

static std::string fixture(const char* name) {
  std::string f(__FILE__);
  return f.substr(0, f.find_last_of("/\\")) + "/../fixtures/" + name;
}

static void checkFixture(const char* name) {
  TapCapture cap;
  std::string path = fixture(name);
  TEST_ASSERT_TRUE_MESSAGE(loadTapCapture(path.c_str(), cap), path.c_str());
  double secs = tapHeaderNum(cap.header, "secs");
  TEST_ASSERT_TRUE_MESSAGE(secs > 0, "header missing secs");
  ReplayResult r = replayCapture(cap);              // default ReplayConfig == the shipped constants
  struct { const char* key; int fired; } bands[] = {
    {"kicks_per_s", r.kicks}, {"snares_per_s", r.snares},
  };
  for (auto& b : bands) {
    double perS = tapHeaderNum(cap.header, b.key);
    TEST_ASSERT_FALSE_MESSAGE(std::isnan(perS), b.key);
    char msg[128];
    snprintf(msg, sizeof msg, "%s %s: fired %d, expect %.1f/s over %.0fs (veto %d refr %d)",
             name, b.key, b.fired, perS, secs, r.vetoes, r.refr);
    if (perS == 0.0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, b.fired, msg);
    } else {
      double expect = perS * secs;
      double tol = expect * 0.20 > 1.0 ? expect * 0.20 : 1.0;
      TEST_ASSERT_TRUE_MESSAGE(b.fired >= expect - tol && b.fired <= expect + tol, msg);
    }
  }
}

void test_kick()     { checkFixture("kick.tap"); }
void test_snare()    { checkFixture("snare.tap"); }
void test_backbeat() { checkFixture("backbeat.tap"); }
void test_hats()     { checkFixture("hats.tap"); }
void test_rumble()   { checkFixture("rumble.tap"); }

void test_coincident_integrity() {                  // fixture parses and is full-length; nothing more
  TapCapture cap;
  TEST_ASSERT_TRUE(loadTapCapture(fixture("coincident.tap").c_str(), cap));
  TEST_ASSERT_TRUE((int)cap.pkts.size() > 1000);    // ~1750 at 10s -- guards a truncated commit
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_kick);
  RUN_TEST(test_snare);
  RUN_TEST(test_backbeat);
  RUN_TEST(test_hats);
  RUN_TEST(test_rumble);
  RUN_TEST(test_coincident_integrity);
  return UNITY_END();
}
