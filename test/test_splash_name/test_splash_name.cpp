#include <unity.h>
#include <string>
#include "../../splash_name.h"

// Plain ASCII must survive byte-identical -- the common case must not be disturbed.
void test_ascii_passes_through_unchanged() {
  TEST_ASSERT_EQUAL_STRING("kitsune", splashAsciiName("kitsune").c_str());
  TEST_ASSERT_EQUAL_STRING("DJ Pigeon 3", splashAsciiName("DJ Pigeon 3").c_str());
}

// The point of the feature: a device named Jose shows Jose, not Jos or Jos<blank>.
void test_transliterates_latin1_supplement() {
  TEST_ASSERT_EQUAL_STRING("Jose",  splashAsciiName("Jos\xC3\xA9").c_str());        // José
  TEST_ASSERT_EQUAL_STRING("Bjorn", splashAsciiName("Bj\xC3\xB6rn").c_str());       // Björn
  TEST_ASSERT_EQUAL_STRING("Nino",  splashAsciiName("Ni\xC3\xB1o").c_str());        // Niño
  TEST_ASSERT_EQUAL_STRING("AEon",  splashAsciiName("\xC3\x86on").c_str());         // Æon
}

// ss is the reason transliteration must run BEFORE the MAX_LETTERS check: it GROWS the string.
void test_sharp_s_expands_and_grows_the_length() {
  std::string in = "Stra\xC3\x9F""e";                 // Straße -- 6 glyphs, 7 bytes
  std::string out = splashAsciiName(in);
  TEST_ASSERT_EQUAL_STRING("Strasse", out.c_str());
  TEST_ASSERT_TRUE(out.size() > 6);                   // 7 > 6 glyphs in
}

// Anything with no ASCII equivalent is dropped rather than drawn as a blank cell.
void test_drops_unrepresentable_bytes() {
  TEST_ASSERT_EQUAL_STRING("ok", splashAsciiName("o\xE4\xBD\xA0k").c_str());        // o<CJK>k
  TEST_ASSERT_EQUAL_STRING("hi", splashAsciiName("h\xF0\x9F\x98\x80i").c_str());    // h<emoji>i
  TEST_ASSERT_EQUAL_STRING("c",  splashAsciiName("\xC2\xA9""c").c_str());           // ©c
  TEST_ASSERT_EQUAL_STRING("",   splashAsciiName("\xC3").c_str());                  // truncated lead byte
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ascii_passes_through_unchanged);
  RUN_TEST(test_transliterates_latin1_supplement);
  RUN_TEST(test_sharp_s_expands_and_grows_the_length);
  RUN_TEST(test_drops_unrepresentable_bytes);
  return UNITY_END();
}
