#include "gif_store.h"
#include <LittleFS.h>
#include <cstdio>
#include <cstring>

static const char* TMP = "/_gif.tmp";   // leading underscore: never a valid uploaded name, so it
                                        // can't collide with a clip and list() skips it for free

void gifPath(char* b, size_t n, const char* name) { snprintf(b, n, "/%s.gif", name); }

bool LittleFsGifStore::beginTmp() {
  LittleFS.remove(TMP);
  _f = LittleFS.open(TMP, "w");
  return (bool)_f;
}
bool LittleFsGifStore::write(const uint8_t* d, size_t n) {
  return _f && _f.write(d, n) == n;
}
bool LittleFsGifStore::commit(const char* name) {
  if (!_f) return false;
  _f.flush(); _f.close();
  char p[GIF_NAME_MAX + 8]; gifPath(p, sizeof p, name);
  LittleFS.remove(p);
  return LittleFS.rename(TMP, p);
}
void LittleFsGifStore::abortTmp() {
  if (_f) _f.close();
  LittleFS.remove(TMP);
}

// Enumerate /*.gif. Sorted by name via insertion sort so playback order is stable and matches what
// the config page lists -- LittleFS returns directory entries in whatever order it stored them.
int LittleFsGifStore::list(GifMeta* out, int max) {
  File root = LittleFS.open("/");
  if (!root) return 0;
  int n = 0;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    const char* fn = f.name();
    if (!fn) { f.close(); continue; }
    if (*fn == '/') fn++;                       // some cores prefix the path, some don't
    size_t len = strlen(fn);
    if (len <= 4 || strcmp(fn + len - 4, ".gif") != 0) { f.close(); continue; }
    size_t stem = len - 4;
    if (stem > GIF_NAME_MAX) { f.close(); continue; }   // not ours; can't be addressed by name
    uint32_t bytes = (uint32_t)f.size();
    f.close();
    if (!out) { if (n < max) n++; continue; }
    if (n >= max) continue;
    int at = n;                                  // insertion sort: n is tiny (<= GIF_MAX)
    while (at > 0 && strncmp(fn, out[at - 1].name, GIF_NAME_MAX) < 0) { out[at] = out[at - 1]; at--; }
    memcpy(out[at].name, fn, stem); out[at].name[stem] = '\0';
    out[at].bytes = bytes;
    n++;
  }
  root.close();
  return n;
}

bool LittleFsGifStore::del(const char* name) {
  char p[GIF_NAME_MAX + 8]; gifPath(p, sizeof p, name);
  return LittleFS.remove(p);
}

void LittleFsGifStore::clear() {
  GifMeta m[GIF_MAX];
  int n = list(m, GIF_MAX);
  for (int i = 0; i < n; i++) del(m[i].name);
  LittleFS.remove(TMP);
}
