#include "slide_proto.h"
#include <cstring>
#include <ArduinoJson.h>

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;   // '=' padding or invalid
}

int b64decode(const char* in, size_t inlen, uint8_t* out) {
  int acc = 0, bits = 0, n = 0;
  for (size_t i = 0; i < inlen; i++) {
    if (in[i] == '=' || in[i] == '\0') break;
    int v = b64val(in[i]);
    if (v < 0) return -1;
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out[n++] = (uint8_t)(acc >> bits); }
  }
  return n;
}

bool isSlideCmd(const std::string& line) {
  size_t c = line.find("\"cmd\"");
  if (c == std::string::npos) return false;
  size_t colon = line.find(':', c + 5);
  if (colon == std::string::npos) return false;
  size_t q = line.find('"', colon);            // opening quote of the cmd value
  if (q == std::string::npos) return false;
  return line.compare(q + 1, 6, "slide_") == 0;
}

static std::string ack(int seq) {
  return "{\"type\":\"slide_ack\",\"seq\":" + std::to_string(seq) + "}";
}
static std::string err(const char* m) {
  return std::string("{\"type\":\"err\",\"msg\":\"") + m + "\"}";
}

std::string handleSlideLine(const std::string& line, SlideStore& store, SlideUpload& up,
                            bool& isUpload, bool& slidesChanged) {
  isUpload = up.active;   // stays true through a begin..end window unless we end/abort below
  slidesChanged = false;
  JsonDocument d;
  if (deserializeJson(d, line)) { return err("bad json"); }
  std::string cmd = d["cmd"].is<const char*>() ? d["cmd"].as<std::string>() : "";

  if (cmd == "slide_begin") {
    int idx = d["index"].is<int>() ? d["index"].as<int>() : -1;
    if (idx < 0 || idx >= SLIDE_MAX) return err("bad index");
    if (up.active) store.abortTmp(up.index);        // a prior upload never ended; drop it
    if (!store.beginTmp(idx)) { up.active = false; isUpload = false; return err("fs begin"); }
    // Field-by-field, not aggregate brace-init: SlideUpload has default member
    // initializers, which makes it a non-aggregate under C++11 (device envs are
    // pinned to gnu++11 by the Arduino-ESP32 core); brace-init there fails to
    // resolve against any constructor. Native forces gnu++17 (relaxed aggregate
    // rules), which is why the brief's `SlideUpload{idx,0,0,true}` built there
    // but would break every real device firmware the moment this file's swept
    // in by build_src_filter's `+<*>`.
    up.index = idx; up.nextSeq = 0; up.got = 0; up.active = true;
    isUpload = true;
    return ack(-1);
  }
  if (cmd == "slide_chunk") {
    if (!up.active) return err("no upload");
    auto fail = [&](const char* m) { store.abortTmp(up.index); up.active = false; isUpload = false; return err(m); };
    int seq = d["seq"].is<int>() ? d["seq"].as<int>() : -1;
    if (seq != up.nextSeq) return fail("bad seq");
    const char* b64 = d["data"].is<const char*>() ? d["data"].as<const char*>() : "";
    static uint8_t buf[2048];   // a full 2048B line's base64 decodes to <1536B; headroom
    int n = b64decode(b64, strlen(b64), buf);
    if (n < 0) return fail("bad b64");
    if (up.got + (size_t)n > SLIDE_BYTES) return fail("overflow");
    if (!store.write(buf, n)) return fail("fs write");
    up.got += n; up.nextSeq++;
    isUpload = true;
    return ack(seq);
  }
  if (cmd == "slide_end") {
    if (!up.active) return err("no upload");
    int idx = up.index; size_t got = up.got;
    up.active = false; isUpload = false;
    if (got != SLIDE_BYTES) { store.abortTmp(idx); return err("size"); }
    if (!store.commit(idx)) return err("fs commit");
    slidesChanged = true;
    return ack(idx);
  }
  if (cmd == "slide_list") {
    SlideMeta m[SLIDE_MAX]; int n = store.list(m, SLIDE_MAX);
    JsonDocument r; r["type"] = "slides";
    JsonArray a = r["slides"].to<JsonArray>();
    for (int i = 0; i < n; i++) { JsonObject o = a.add<JsonObject>(); o["index"] = m[i].index; o["bytes"] = m[i].bytes; }
    std::string out; serializeJson(r, out); return out;
  }
  if (cmd == "slide_del") {
    int idx = d["index"].is<int>() ? d["index"].as<int>() : -1;
    if (idx < 0 || idx >= SLIDE_MAX) return err("bad index");
    if (!store.del(idx)) return err("no slide");
    slidesChanged = true;
    return ack(idx);
  }
  if (cmd == "slide_clear") {
    store.clear(); slidesChanged = true;
    return "{\"type\":\"slide_ack\",\"seq\":-1}";
  }
  return err("unknown slide cmd");
}
