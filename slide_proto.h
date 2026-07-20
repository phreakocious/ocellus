#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

constexpr size_t SLIDE_BYTES = 240 * 240 * 2;   // 115200
constexpr int    SLIDE_MAX   = 128;             // hard cap on slide index

struct SlideMeta { int index; uint32_t bytes; };

// Storage backend, injected so this module stays Arduino-free (native-testable).
struct SlideStore {
  virtual ~SlideStore() {}
  virtual bool beginTmp(int index)             = 0;  // open/truncate slideN.tmp
  virtual bool write(const uint8_t* d, size_t n) = 0;
  virtual bool commit(int index)               = 0;  // fsync + atomic rename tmp -> slideN.raw
  virtual void abortTmp(int index)             = 0;  // delete slideN.tmp
  virtual int  list(SlideMeta* out, int max)   = 0;  // count; dense 0..count-1
  virtual bool del(int index)                  = 0;  // delete + compact higher indices down
  virtual void clear()                         = 0;  // delete all slides + tmp
};

// One upload in flight at a time. Reset by handleSlideLine on slide_begin.
struct SlideUpload { int index = -1; int nextSeq = 0; size_t got = 0; bool active = false; };

// true if the line's "cmd" is a slide_* command (cheap router hint; substring match).
bool isSlideCmd(const std::string& line);

// Handle one slide_* JSON line. Returns the JSON response string.
//   isUpload   -> set true while a slide_begin..slide_end is in flight (caller tight-drains serial).
//   slidesChanged -> set true when the on-disk slide set changed (commit/del/clear succeeded).
std::string handleSlideLine(const std::string& line, SlideStore& store, SlideUpload& up,
                            bool& isUpload, bool& slidesChanged);

// base64 decode. Returns decoded byte count, or -1 on invalid input.
// `out` must have room for (inlen/4)*3 bytes.
int b64decode(const char* in, size_t inlen, uint8_t* out);
