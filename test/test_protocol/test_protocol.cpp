#include <unity.h>
#include <ArduinoJson.h>
#include <cstring>
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

// Firmware identity rides on `catalog`, never on `get`. A Config field would round-trip through
// `set` into NVS and then report whichever build was running when the config was last saved --
// stale in exactly the situation you'd be asking the question.
//
// Asserts non-empty rather than an exact value: the global [env] section in platformio.ini stamps
// the host build too, so this reads a real `git describe` here, not version.h's "dev" fallback.
// Pinning "dev" would pass only by accident of how the hook happens to be wired.
void test_catalog_carries_firmware_version() {
    Config c;
    bool changed = true;
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, handleLine("{\"cmd\":\"catalog\"}", c, changed)));
    TEST_ASSERT_TRUE(d["fw"].is<const char*>());
    TEST_ASSERT_TRUE(strlen(d["fw"].as<const char*>()) > 0);

    // ...and must NOT be in `get`, which is the config surface.
    TEST_ASSERT_FALSE(deserializeJson(d, handleLine("{\"cmd\":\"get\"}", c, changed)));
    TEST_ASSERT_FALSE(d["fw"].is<const char*>());
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
    // ANIM_COUNT, not a literal -- always one past the last playable, so this stays an unknown id
    // as the registry grows (a hardcoded 47 then 48 both went stale here).
    std::string r3 = handleLine("{\"cmd\":\"anim\",\"id\":" + std::to_string(ANIM_COUNT) + "}",
                                c, changed, &sel3);  // unknown id -> rejected
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
    RUN_TEST(test_catalog_carries_firmware_version);
    RUN_TEST(test_anim_selects_live_animation);
    RUN_TEST(test_get_returns_config);
    RUN_TEST(test_set_applies_and_flags_changed);
    RUN_TEST(test_unknown_cmd_errors);
    return UNITY_END();
}
