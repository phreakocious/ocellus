#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

// Chunked-binary-upload protocols over the config serial link: slides (fixed-size RGB565) and
// GIFs (variable-size, name-keyed). They share b64decode and the one-upload-in-flight shape,
// which is why they live in one module despite the filename.

constexpr size_t SLIDE_BYTES = 240 * 240 * 2;   // 115200
constexpr int    SLIDE_MAX   = 128;             // hard cap on slide index

constexpr int    GIF_MAX       = 32;                 // clips per unit; well above what the fs holds
constexpr size_t GIF_NAME_MAX  = 24;                 // stem only; ".gif" is appended device-side
constexpr size_t GIF_MAX_BYTES = 2u * 1024 * 1024;   // mirrors tools/bake_gif.py's per-clip cap

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


// --- GIF clips (animation id 48) ------------------------------------------------------------
// Name-keyed, NOT index-keyed like slides. The spec originally specified dense gN.gif indices
// mirroring the slide store, but `tools/flash.py --gifs SET` writes human-named clips straight to
// LittleFS via uploadfs -- a bulk path that did not exist when the spec was written. Two naming
// schemes for one directory would mean the device could not enumerate its own filesystem, so
// names win and the index-compaction logic disappears with them.

struct GifMeta { char name[GIF_NAME_MAX + 1]; uint32_t bytes; };

// Storage backend, injected so this module stays Arduino-free (native-testable).
struct GifStore {
  virtual ~GifStore() {}
  virtual bool beginTmp()                        = 0;  // open/truncate the single upload tmp
  virtual bool write(const uint8_t* d, size_t n) = 0;
  virtual bool commit(const char* name)          = 0;  // fsync + atomic rename tmp -> <name>.gif
  virtual void abortTmp()                        = 0;  // delete the tmp
  virtual int  list(GifMeta* out, int max)       = 0;  // count, sorted by name
  virtual bool del(const char* name)             = 0;
  virtual void clear()                           = 0;
};

struct GifUpload {
  char     name[GIF_NAME_MAX + 1] = {0};
  uint32_t declared = 0;    // bytes promised by gif_begin; a GIF has no fixed size to check against
  uint32_t got      = 0;
  int      nextSeq  = 0;
  bool     active   = false;
};

// Trust boundary: this name becomes a filesystem path. [A-Za-z0-9_-] only and 1..GIF_NAME_MAX
// chars, so "..", "/" and "." cannot appear and traversal is impossible by construction.
bool gifNameOk(const char* name);

// true if the line's "cmd" is a gif_* command (cheap router hint; substring match).
bool isGifCmd(const std::string& line);

// Handle one gif_* JSON line. Returns the JSON response string.
//   isUpload    -> true while a gif_begin..gif_end is in flight (caller tight-drains serial).
//   gifsChanged -> true when the on-disk clip set changed (commit/del/clear succeeded).
std::string handleGifLine(const std::string& line, GifStore& store, GifUpload& up,
                          bool& isUpload, bool& gifsChanged);
