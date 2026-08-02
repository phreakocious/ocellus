#pragma once
#include <cstdint>

// No-repeat-until-exhausted shuffle bag, one per string pool. Arduino-free: the RNG is injected
// as rng(k) -> uniform int in [0,k). Byte-for-byte the same shuffle as the lab _pick / the
// reference treatcat_gc9a01.ino pickIdx.
struct ShuffleBag { uint8_t q[96]; int n; int last; };   // pools are <=96 (static_assert in treatcat.cpp)

inline int shufflebagPick(ShuffleBag& s, int len, uint32_t (*rng)(uint32_t n)) {
  if (s.n == 0) {
    for (int i = 0; i < len; i++) s.q[i] = (uint8_t)i;
    for (int i = len - 1; i > 0; i--) { int j = (int)rng((uint32_t)(i + 1));
      uint8_t t = s.q[i]; s.q[i] = s.q[j]; s.q[j] = t; }
    if (len > 1 && s.q[len - 1] == s.last) { uint8_t t = s.q[0]; s.q[0] = s.q[len - 1]; s.q[len - 1] = t; }
    s.n = len;
  }
  int idx = s.q[--s.n]; s.last = idx; return idx;
}
