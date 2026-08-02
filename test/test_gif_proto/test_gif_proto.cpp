#include <unity.h>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include "../../slide_proto.h"

// In-memory fake: one tmp buffer + committed clips keyed by name (a std::map, so list() comes back
// name-sorted for free -- same order LittleFS enumeration is normalized to on device).
struct FakeGifStore : GifStore {
  std::map<std::string, std::vector<uint8_t>> gifs;
  std::vector<uint8_t> tmp; bool tmpOpen = false;
  int commits = 0, aborts = 0;

  bool beginTmp() override { tmp.clear(); tmpOpen = true; return true; }
  bool write(const uint8_t* d, size_t n) override {
    if (!tmpOpen) return false;
    tmp.insert(tmp.end(), d, d + n); return true;
  }
  bool commit(const char* name) override {
    if (!tmpOpen) return false;
    gifs[name] = tmp; tmpOpen = false; commits++; return true;
  }
  void abortTmp() override { tmp.clear(); tmpOpen = false; aborts++; }
  int list(GifMeta* out, int max) override {
    int n = 0;
    for (auto& kv : gifs) {
      if (n >= max) break;
      strncpy(out[n].name, kv.first.c_str(), GIF_NAME_MAX);
      out[n].name[GIF_NAME_MAX] = '\0';
      out[n].bytes = (uint32_t)kv.second.size();
      n++;
    }
    return n;
  }
  bool del(const char* name) override { return gifs.erase(name) > 0; }
  void clear() override { gifs.clear(); }
};

static std::string b64(const std::vector<uint8_t>& v) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < v.size(); i += 3) {
    uint32_t n = v[i] << 16 | (i + 1 < v.size() ? v[i + 1] << 8 : 0) | (i + 2 < v.size() ? v[i + 2] : 0);
    o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63];
    o += (i + 1 < v.size()) ? T[(n >> 6) & 63] : '=';
    o += (i + 2 < v.size()) ? T[n & 63] : '=';
  }
  return o;
}

static std::string chunk(int seq, const std::vector<uint8_t>& d) {
  return "{\"cmd\":\"gif_chunk\",\"seq\":" + std::to_string(seq) + ",\"data\":\"" + b64(d) + "\"}";
}
static std::string begin(const char* name, uint32_t bytes) {
  return std::string("{\"cmd\":\"gif_begin\",\"name\":\"") + name +
         "\",\"bytes\":" + std::to_string(bytes) + "}";
}

// --- the trust boundary: a name becomes a filesystem path ---------------------------------------

void test_name_validation_rejects_traversal() {
  TEST_ASSERT_TRUE(gifNameOk("shark"));
  TEST_ASSERT_TRUE(gifNameOk("sea-angel_2"));
  TEST_ASSERT_FALSE(gifNameOk(".."));
  TEST_ASSERT_FALSE(gifNameOk("../../etc/passwd"));
  TEST_ASSERT_FALSE(gifNameOk("a/b"));
  TEST_ASSERT_FALSE(gifNameOk("a.gif"));        // no dots: the device appends the extension
  TEST_ASSERT_FALSE(gifNameOk(""));
  TEST_ASSERT_FALSE(gifNameOk(nullptr));
  TEST_ASSERT_FALSE(gifNameOk("this_name_is_far_too_long_to_fit"));   // > GIF_NAME_MAX
}

void test_begin_rejects_bad_name() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  std::string r = handleGifLine(begin("../evil", 4), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("bad name") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);
  TEST_ASSERT_FALSE(isUp);
}

// --- declared-length accounting (the spec's named requirement) ----------------------------------

void test_full_upload_commits_once() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  std::vector<uint8_t> data(9, 0xAB);
  handleGifLine(begin("shark", 9), s, up, isUp, chg);
  TEST_ASSERT_TRUE(isUp); TEST_ASSERT_FALSE(chg);
  handleGifLine(chunk(0, data), s, up, isUp, chg);
  TEST_ASSERT_FALSE(chg);
  std::string r = handleGifLine("{\"cmd\":\"gif_end\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(chg);
  TEST_ASSERT_FALSE(isUp);
  TEST_ASSERT_EQUAL_INT(1, s.commits);
  TEST_ASSERT_EQUAL_INT(9, (int)s.gifs["shark"].size());
}

void test_begin_requires_bytes() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  std::string r = handleGifLine("{\"cmd\":\"gif_begin\",\"name\":\"x\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("need bytes") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);
}

void test_over_length_upload_aborts() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  handleGifLine(begin("x", 4), s, up, isUp, chg);
  std::string r = handleGifLine(chunk(0, std::vector<uint8_t>(9, 1)), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("overflow") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);
  TEST_ASSERT_EQUAL_INT(1, s.aborts);
  TEST_ASSERT_EQUAL_INT(0, s.commits);
}

void test_short_upload_end_aborts() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  handleGifLine(begin("x", 9), s, up, isUp, chg);
  handleGifLine(chunk(0, std::vector<uint8_t>(3, 1)), s, up, isUp, chg);
  std::string r = handleGifLine("{\"cmd\":\"gif_end\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("size") != std::string::npos);
  TEST_ASSERT_FALSE(chg);
  TEST_ASSERT_EQUAL_INT(0, s.commits);
  TEST_ASSERT_TRUE(s.gifs.empty());
}

void test_out_of_order_seq_aborts() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  handleGifLine(begin("x", 6), s, up, isUp, chg);
  handleGifLine(chunk(0, std::vector<uint8_t>(3, 1)), s, up, isUp, chg);
  std::string r = handleGifLine(chunk(5, std::vector<uint8_t>(3, 1)), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("bad seq") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);
  TEST_ASSERT_EQUAL_INT(0, s.commits);
}

void test_abort_and_restart_leaves_no_partial() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  handleGifLine(begin("x", 60), s, up, isUp, chg);
  handleGifLine(chunk(0, std::vector<uint8_t>(3, 7)), s, up, isUp, chg);
  // Host gives up and starts over without ending: the stale tmp must be dropped, not appended to.
  handleGifLine(begin("x", 3), s, up, isUp, chg);
  handleGifLine(chunk(0, std::vector<uint8_t>(3, 9)), s, up, isUp, chg);
  handleGifLine("{\"cmd\":\"gif_end\"}", s, up, isUp, chg);
  TEST_ASSERT_EQUAL_INT(1, s.commits);
  TEST_ASSERT_EQUAL_INT(3, (int)s.gifs["x"].size());
  TEST_ASSERT_EQUAL_UINT8(9, s.gifs["x"][0]);
}

void test_chunk_without_begin_errors() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  std::string r = handleGifLine(chunk(0, std::vector<uint8_t>(3, 1)), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("no upload") != std::string::npos);
}

void test_cap_rejects_oversize_declaration() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  std::string r = handleGifLine(begin("x", GIF_MAX_BYTES + 1), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("bad bytes") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);
}

void test_full_store_rejects_new_but_allows_replace() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  for (int i = 0; i < GIF_MAX; i++) s.gifs["clip" + std::to_string(i)] = {1};
  std::string r = handleGifLine(begin("brandnew", 1), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("full") != std::string::npos);
  // Overwriting one that already exists does not grow the set, so it must still be allowed.
  r = handleGifLine(begin("clip0", 1), s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("gif_ack") != std::string::npos);
}

// --- listing / deletion -------------------------------------------------------------------------

void test_list_reports_names_and_total() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  s.gifs["anya"] = std::vector<uint8_t>(10, 0);
  s.gifs["shark"] = std::vector<uint8_t>(25, 0);
  std::string r = handleGifLine("{\"cmd\":\"gif_list\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("\"anya\"") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("\"shark\"") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("\"bytes\":35") != std::string::npos);   // budget readout
}

void test_del_and_clear() {
  FakeGifStore s; GifUpload up; bool isUp, chg;
  s.gifs["a"] = {1}; s.gifs["b"] = {2};
  handleGifLine("{\"cmd\":\"gif_del\",\"name\":\"a\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(chg);
  TEST_ASSERT_EQUAL_INT(1, (int)s.gifs.size());
  std::string r = handleGifLine("{\"cmd\":\"gif_del\",\"name\":\"nope\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("no gif") != std::string::npos);
  handleGifLine("{\"cmd\":\"gif_clear\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(s.gifs.empty());
}

// --- routing ------------------------------------------------------------------------------------

void test_is_gif_cmd_matches_cmd_value_only() {
  TEST_ASSERT_TRUE(isGifCmd("{\"cmd\":\"gif_begin\",\"name\":\"x\"}"));
  TEST_ASSERT_TRUE(isGifCmd("{\"cmd\": \"gif_list\"}"));
  TEST_ASSERT_FALSE(isGifCmd("{\"cmd\":\"slide_begin\",\"index\":0}"));
  TEST_ASSERT_FALSE(isGifCmd("{\"cmd\":\"get\"}"));
  // A config value that merely contains "gif_" must not route to the gif handler.
  TEST_ASSERT_FALSE(isGifCmd("{\"cmd\":\"set\",\"config\":{\"name\":\"gif_thing\"}}"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_name_validation_rejects_traversal);
  RUN_TEST(test_begin_rejects_bad_name);
  RUN_TEST(test_full_upload_commits_once);
  RUN_TEST(test_begin_requires_bytes);
  RUN_TEST(test_over_length_upload_aborts);
  RUN_TEST(test_short_upload_end_aborts);
  RUN_TEST(test_out_of_order_seq_aborts);
  RUN_TEST(test_abort_and_restart_leaves_no_partial);
  RUN_TEST(test_chunk_without_begin_errors);
  RUN_TEST(test_cap_rejects_oversize_declaration);
  RUN_TEST(test_full_store_rejects_new_but_allows_replace);
  RUN_TEST(test_list_reports_names_and_total);
  RUN_TEST(test_del_and_clear);
  RUN_TEST(test_is_gif_cmd_matches_cmd_value_only);
  return UNITY_END();
}
