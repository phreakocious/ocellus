#pragma once
#include <string>

// Transliterate a UTF-8 device name down to plain ASCII (32..126) so every byte in the result
// is both drawable and correct -- VGA_FONT can draw the whole CP437 codepage now (see below),
// but CP437 codes above 126 are box-drawing/line/symbol glyphs, not accented Latin letters, so
// staying in pure ASCII is what keeps a transliterated name from landing on the wrong glyph.
//
// Two problems, one fix (spec 2026-08-02):
//   1. VGA_FONT bakes the CP437 codepage (32..255), not Unicode -- see vga_font.h. A raw UTF-8
//      byte fed straight to it lands on whatever glyph sits at that CP437 code point, almost
//      never the accented letter it came from (e-acute's UTF-8 continuation byte 0xA9 is CP437's
//      "not" sign, not an accented e). Without this step an accented name renders as mojibake,
//      not as the letter it was meant to be.
//   2. std::string::size() counts BYTES, so "José" (5 bytes) would pick a 5-letter scale while
//      drawing 4 glyphs, and MAX_LETTERS would be a byte cap rather than a letter cap.
// Running this FIRST makes the measured length equal the drawn length, so both go away.
//
// Only the Latin-1 supplement is mapped -- UTF-8 two-byte sequences 0xC3 0x80..0xBF, i.e.
// U+00C0..U+00FF. That covers the accented Latin names a user plausibly has. Everything else
// outside 32..126 is dropped: it would draw as the wrong CP437 glyph rather than the intended
// letter, and a silent mojibake reads as a worse bug than a dropped byte.
//
// Pure std::string, Arduino-free, so the native suite exercises it on the host.
inline std::string splashAsciiName(const std::string& in) {
  // Indexed by (second byte - 0x80), so entry i is U+00C0 + i.
  static const char* const LATIN1[64] = {
    "A", "A", "A", "A", "A", "A", "AE", "C",    // C0 À  .. C7 Ç
    "E", "E", "E", "E", "I", "I", "I",  "I",    // C8 È  .. CF Ï
    "D", "N", "O", "O", "O", "O", "O",  "x",    // D0 Ð  .. D7 ×
    "O", "U", "U", "U", "U", "Y", "Th", "ss",   // D8 Ø  .. DF ß
    "a", "a", "a", "a", "a", "a", "ae", "c",    // E0 à  .. E7 ç
    "e", "e", "e", "e", "i", "i", "i",  "i",    // E8 è  .. EF ï
    "d", "n", "o", "o", "o", "o", "o",  "/",    // F0 ð  .. F7 ÷
    "o", "u", "u", "u", "u", "y", "th", "y",    // F8 ø  .. FF ÿ
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    unsigned char c = (unsigned char)in[i];
    if (c >= 32 && c <= 126) { out += (char)c; continue; }   // already drawable
    if (c == 0xC3 && i + 1 < in.size()) {                    // Latin-1 supplement
      unsigned char d = (unsigned char)in[i + 1];
      if (d >= 0x80 && d <= 0xBF) { out += LATIN1[d - 0x80]; i++; continue; }
    }
    // Everything else -- 0xC2 punctuation, CJK/emoji lead and continuation bytes, control
    // codes, a truncated trailing lead byte -- has no CORRECT glyph (VGA_FONT would draw
    // some CP437 character for it, just not the one intended). Drop the byte and move on.
  }
  return out;
}
