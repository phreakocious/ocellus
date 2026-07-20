#include <unity.h>
#include <vector>
#include <map>
#include <string>
#include "../../slide_proto.h"

// In-memory fake: tmp buffer + committed slides keyed by index.
struct FakeStore : SlideStore {
  std::map<int, std::vector<uint8_t>> slides;
  std::vector<uint8_t> tmp; int tmpIndex = -1; bool tmpOpen = false;
  bool beginTmp(int i) override { tmp.clear(); tmpIndex = i; tmpOpen = true; return true; }
  bool write(const uint8_t* d, size_t n) override { if (!tmpOpen) return false; tmp.insert(tmp.end(), d, d + n); return true; }
  bool commit(int i) override { if (!tmpOpen) return false; slides[i] = tmp; tmpOpen = false; return true; }
  void abortTmp(int) override { tmp.clear(); tmpOpen = false; }
  int list(SlideMeta* out, int max) override { int n = 0; for (int i = 0; i < max && slides.count(i); i++) { out[n].index = i; out[n].bytes = (uint32_t)slides[i].size(); n++; } return n; }
  bool del(int i) override { if (!slides.count(i)) return false; for (int k = i; slides.count(k + 1); k++) slides[k] = slides[k + 1]; int last = 0; while (slides.count(last)) last++; slides.erase(last - 1); return true; }
  void clear() override { slides.clear(); }
};

static std::string b64(const std::vector<uint8_t>& v) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o; for (size_t i = 0; i < v.size(); i += 3) {
    uint32_t n = v[i] << 16 | (i + 1 < v.size() ? v[i + 1] << 8 : 0) | (i + 2 < v.size() ? v[i + 2] : 0);
    o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63];
    o += (i + 1 < v.size()) ? T[(n >> 6) & 63] : '='; o += (i + 2 < v.size()) ? T[n & 63] : '=';
  } return o;
}

void test_b64_roundtrip() {
  std::vector<uint8_t> in = {0, 255, 16, 32, 240, 7};
  std::string e = b64(in);
  uint8_t out[16]; int n = b64decode(e.c_str(), e.size(), out);
  TEST_ASSERT_EQUAL_INT(6, n);
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(in[i], out[i]);
}

void test_full_upload_commits_once() {
  FakeStore s; SlideUpload up; bool isUp, chg;
  handleSlideLine("{\"cmd\":\"slide_begin\",\"index\":0}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(isUp); TEST_ASSERT_FALSE(chg);
  std::vector<uint8_t> chunk(1024, 0xAB);
  int seq = 0; size_t sent = 0;
  while (sent < SLIDE_BYTES) {
    size_t n = (SLIDE_BYTES - sent < 1024) ? SLIDE_BYTES - sent : 1024;
    std::vector<uint8_t> c(chunk.begin(), chunk.begin() + n);
    std::string line = "{\"cmd\":\"slide_chunk\",\"seq\":" + std::to_string(seq) + ",\"data\":\"" + b64(c) + "\"}";
    std::string r = handleSlideLine(line, s, up, isUp, chg);
    TEST_ASSERT_TRUE(r.find("slide_ack") != std::string::npos);
    seq++; sent += n;
  }
  handleSlideLine("{\"cmd\":\"slide_end\"}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(chg); TEST_ASSERT_FALSE(isUp); TEST_ASSERT_FALSE(up.active);
  SlideMeta m[SLIDE_MAX]; TEST_ASSERT_EQUAL_INT(1, s.list(m, SLIDE_MAX));
  TEST_ASSERT_EQUAL_UINT32(SLIDE_BYTES, m[0].bytes);
}

void test_out_of_order_seq_aborts() {
  FakeStore s; SlideUpload up; bool isUp, chg;
  handleSlideLine("{\"cmd\":\"slide_begin\",\"index\":0}", s, up, isUp, chg);
  std::vector<uint8_t> c(1024, 1);
  std::string bad = "{\"cmd\":\"slide_chunk\",\"seq\":5,\"data\":\"" + b64(c) + "\"}";
  std::string r = handleSlideLine(bad, s, up, isUp, chg);
  TEST_ASSERT_TRUE(r.find("err") != std::string::npos);
  TEST_ASSERT_FALSE(up.active);                 // aborted
  SlideMeta m[SLIDE_MAX];
  TEST_ASSERT_EQUAL_INT(0, s.list(m, SLIDE_MAX)); // nothing committed
}

void test_short_upload_end_aborts() {
  FakeStore s; SlideUpload up; bool isUp, chg;
  handleSlideLine("{\"cmd\":\"slide_begin\",\"index\":0}", s, up, isUp, chg);
  std::vector<uint8_t> c(1024, 2);
  handleSlideLine("{\"cmd\":\"slide_chunk\",\"seq\":0,\"data\":\"" + b64(c) + "\"}", s, up, isUp, chg);
  std::string r = handleSlideLine("{\"cmd\":\"slide_end\"}", s, up, isUp, chg);   // only 1024 of 115200
  TEST_ASSERT_TRUE(r.find("err") != std::string::npos);
  SlideMeta m[SLIDE_MAX]; TEST_ASSERT_EQUAL_INT(0, s.list(m, SLIDE_MAX));
}

void test_del_compacts() {
  FakeStore s; s.slides[0] = {1}; s.slides[1] = {2}; s.slides[2] = {3};
  SlideUpload up; bool isUp, chg;
  std::string r = handleSlideLine("{\"cmd\":\"slide_del\",\"index\":0}", s, up, isUp, chg);
  TEST_ASSERT_TRUE(chg);
  SlideMeta m[SLIDE_MAX]; int n = s.list(m, SLIDE_MAX);
  TEST_ASSERT_EQUAL_INT(2, n);                   // dense 0..1 after compaction
  TEST_ASSERT_EQUAL_UINT8(2, s.slides[0][0]);    // old index 1 slid down to 0
}

void test_is_slide_cmd() {
  TEST_ASSERT_TRUE(isSlideCmd("{\"cmd\":\"slide_begin\",\"index\":0}"));
  TEST_ASSERT_FALSE(isSlideCmd("{\"cmd\":\"get\"}"));
}

void test_is_slide_cmd_matches_cmd_value_only() {
  TEST_ASSERT_TRUE(isSlideCmd("{\"cmd\":\"slide_begin\",\"index\":0}"));   // compact (config.html)
  TEST_ASSERT_TRUE(isSlideCmd("{\"cmd\": \"slide_chunk\", \"seq\": 0}"));  // spaced (config_cli json.dumps)
  TEST_ASSERT_FALSE(isSlideCmd("{\"cmd\":\"set\",\"config\":{\"name\":\"slide_thing\"}}")); // name value, NOT a cmd
  TEST_ASSERT_FALSE(isSlideCmd("{\"cmd\":\"get\"}"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_b64_roundtrip);
  RUN_TEST(test_full_upload_commits_once);
  RUN_TEST(test_out_of_order_seq_aborts);
  RUN_TEST(test_short_upload_end_aborts);
  RUN_TEST(test_del_compacts);
  RUN_TEST(test_is_slide_cmd);
  RUN_TEST(test_is_slide_cmd_matches_cmd_value_only);
  return UNITY_END();
}
