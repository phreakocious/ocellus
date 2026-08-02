#pragma once
#include "slide_proto.h"
#include <FS.h>

// Device-only LittleFS backing for GIF clips (/<name>.gif, staged via /_gif.tmp).
// NOT added to env:native (uses Arduino FS); keep it out of the native build_src_filter.
//
// Name-keyed rather than indexed, so a clip uploaded one at a time over Web Serial and a whole set
// written by `flash.py --gifs` (uploadfs) land in the same namespace and list identically. That
// also means list() must enumerate the directory rather than probe dense indices the way the slide
// store does -- there is no index to probe.
struct LittleFsGifStore : GifStore {
  File _f;
  bool beginTmp() override;
  bool write(const uint8_t* d, size_t n) override;
  bool commit(const char* name) override;
  void abortTmp() override;
  int  list(GifMeta* out, int max) override;
  bool del(const char* name) override;
  void clear() override;
};

// Full path for a clip, e.g. "shark" -> "/shark.gif". Caller owns the buffer.
void gifPath(char* b, size_t n, const char* name);
