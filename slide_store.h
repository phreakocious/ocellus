#pragma once
#include "slide_proto.h"
#include <FS.h>

// Device-only LittleFS backing for slide files (/slideN.raw, staged via /slideN.tmp).
// NOT added to env:native (uses Arduino FS); keep it out of the native build_src_filter.
struct LittleFsSlideStore : SlideStore {
  File _f;
  bool beginTmp(int index) override;
  bool write(const uint8_t* d, size_t n) override;
  bool commit(int index) override;
  void abortTmp(int index) override;
  int  list(SlideMeta* out, int max) override;
  bool del(int index) override;
  void clear() override;
};
