#include <unity.h>
#include <ArduinoJson.h>
#include "../../protocol.h"
#include "../../config.h"
#include "../../animations.h"

void test_catalog_lists_all_animations() {
    Config c;
    bool changed = true;
    std::string r = handleLine("{\"cmd\":\"catalog\"}", c, changed);
    TEST_ASSERT_FALSE(changed);
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, r));
    TEST_ASSERT_EQUAL_STRING("catalog", d["type"]);
    TEST_ASSERT_EQUAL_UINT32(REGISTRY_COUNT, (uint32_t)d["animations"].as<JsonArray>().size());  // playable + dev sensor-debug
}

void test_get_returns_config() {
    Config c;
    c.name = "Zed";
    bool changed = true;
    std::string r = handleLine("{\"cmd\":\"get\"}", c, changed);
    TEST_ASSERT_FALSE(changed);
    JsonDocument d;
    deserializeJson(d, r);
    TEST_ASSERT_EQUAL_STRING("config", d["type"]);
    TEST_ASSERT_EQUAL_STRING("Zed", d["name"]);
}

void test_set_applies_and_flags_changed() {
    Config c;
    bool changed = false;
    std::string r = handleLine("{\"cmd\":\"set\",\"config\":{\"brightness\":99}}", c, changed);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_UINT8(99, c.brightness);
    JsonDocument d;
    deserializeJson(d, r);
    TEST_ASSERT_EQUAL_STRING("config", d["type"]);   // echoes new config
}

void test_unknown_cmd_errors() {
    Config c;
    bool changed = true;
    std::string r = handleLine("{\"cmd\":\"frobnicate\"}", c, changed);
    TEST_ASSERT_FALSE(changed);
    JsonDocument d;
    deserializeJson(d, r);
    TEST_ASSERT_EQUAL_STRING("err", d["type"]);
}

void test_anim_selects_live_animation() {
    Config c;
    bool changed = true;
    int sel = -99;
    std::string r = handleLine("{\"cmd\":\"anim\",\"id\":24}", c, changed, &sel);
    TEST_ASSERT_FALSE(changed);          // live jump doesn't persist config
    TEST_ASSERT_EQUAL_INT(24, sel);
    JsonDocument d;
    deserializeJson(d, r);
    TEST_ASSERT_EQUAL_STRING("anim", d["type"]);
    TEST_ASSERT_EQUAL_INT(24, (int)d["id"]);
    int sel2 = 5;
    std::string r2 = handleLine("{\"cmd\":\"anim\",\"id\":99}", c, changed, &sel2);  // out of range -> rejected, sel2 reset to -1
    TEST_ASSERT_EQUAL_INT(-1, sel2);
    JsonDocument d2;
    deserializeJson(d2, r2);
    TEST_ASSERT_EQUAL_STRING("err", d2["type"]);
    int sel3 = 5;
    std::string r3 = handleLine("{\"cmd\":\"anim\",\"id\":46}", c, changed, &sel3);  // unknown id -> rejected
    TEST_ASSERT_EQUAL_INT(-1, sel3);
    JsonDocument d3;
    deserializeJson(d3, r3);
    TEST_ASSERT_EQUAL_STRING("err", d3["type"]);
    int sel4 = -99;
    handleLine("{\"cmd\":\"anim\",\"id\":42}", c, changed, &sel4);   // debug ids stay reachable
    TEST_ASSERT_EQUAL_INT(42, sel4);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_catalog_lists_all_animations);
    RUN_TEST(test_anim_selects_live_animation);
    RUN_TEST(test_get_returns_config);
    RUN_TEST(test_set_applies_and_flags_changed);
    RUN_TEST(test_unknown_cmd_errors);
    return UNITY_END();
}
