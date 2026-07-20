#include "slide_store.h"
#include <LittleFS.h>
#include <cstdio>

static void rawPath(char* b, size_t n, int i) { snprintf(b, n, "/slide%d.raw", i); }
static void tmpPath(char* b, size_t n, int i) { snprintf(b, n, "/slide%d.tmp", i); }

bool LittleFsSlideStore::beginTmp(int i) {
  char p[24]; tmpPath(p, sizeof p, i);
  LittleFS.remove(p);
  _f = LittleFS.open(p, "w");
  return (bool)_f;
}
bool LittleFsSlideStore::write(const uint8_t* d, size_t n) {
  return _f && _f.write(d, n) == n;
}
bool LittleFsSlideStore::commit(int i) {
  if (!_f) return false;
  _f.flush(); _f.close();
  char t[24], r[24]; tmpPath(t, sizeof t, i); rawPath(r, sizeof r, i);
  LittleFS.remove(r);
  return LittleFS.rename(t, r);
}
void LittleFsSlideStore::abortTmp(int i) {
  if (_f) _f.close();
  char t[24]; tmpPath(t, sizeof t, i); LittleFS.remove(t);
}
int LittleFsSlideStore::list(SlideMeta* out, int max) {
  int n = 0;
  for (int i = 0; i < max; i++) {
    char r[24]; rawPath(r, sizeof r, i);
    File f = LittleFS.open(r, "r");
    if (!f) break;                 // dense: first gap ends the list
    if (out) { out[n].index = i; out[n].bytes = (uint32_t)f.size(); }
    n++; f.close();
  }
  return n;
}
bool LittleFsSlideStore::del(int i) {
  char r[24]; rawPath(r, sizeof r, i);
  if (!LittleFS.remove(r)) return false;
  for (int k = i + 1; k < SLIDE_MAX; k++) {   // compact higher indices down
    char a[24], b[24]; rawPath(a, sizeof a, k); rawPath(b, sizeof b, k - 1);
    File f = LittleFS.open(a, "r"); bool exists = (bool)f; if (f) f.close();
    if (!exists) break;
    LittleFS.rename(a, b);
  }
  return true;
}
void LittleFsSlideStore::clear() {
  for (int i = 0; i < SLIDE_MAX; i++) {
    char r[24], t[24]; rawPath(r, sizeof r, i); tmpPath(t, sizeof t, i);
    LittleFS.remove(r); LittleFS.remove(t);
  }
}
