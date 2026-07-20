#pragma once
#include <string>

// Glyph for tail position t of a "name column" in the Matrix effect. t=0 is the bright leading
// edge (bottom of the falling streak); t increases upward into the dimming tail. The name occupies
// the leading run, reverse-anchored (leading = last letter) so the column reads top-to-bottom as
// the name as it falls. Positions past the name return 0 ('\0') -> the caller draws a random glyph
// there, so the name stays woven into an otherwise-normal rain column. Pure/host-testable.
inline char matrixNameGlyph(const std::string& name, int t) {
  int L = (int)name.size();
  if (t < 0 || t >= L) return 0;
  return name[L - 1 - t];
}
