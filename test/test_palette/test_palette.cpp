#include <unity.h>
#include <cmath>
#include "../../palette.h"

// Independent reference for the legacy rainbow (mirrors main.cpp initSinLUT/pColor).
static int refQsin(int a) {
    return (int)(int16_t)(std::sin((a & 0xFF) * (2.0 * 3.141592653589793) / 256.0) * 127);
}
static uint16_t refRainbow(int p) {
    int r = refQsin(p) + 128, g = refQsin(p + 85) + 128, b = refQsin(p + 170) + 128;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void test_rainbow_lut_is_byte_exact() {
    uint16_t lut[256]; buildRainbowLUT(lut);
    for (int p = 0; p < 256; p++) TEST_ASSERT_EQUAL_HEX16(refRainbow(p), lut[p]);
}

void test_two_stop_lut_endpoints_and_midpoint() {
    uint16_t lut[256]; buildPaletteLUT({0x0000, 0xFFFF}, lut);   // black -> white, cyclic
    TEST_ASSERT_EQUAL_HEX16(0x0000, lut[0]);     // stop 0
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, lut[128]);   // stop 1 (half way round the 2-stop ring)
    TEST_ASSERT_EQUAL_HEX16(0x7BEF, lut[64]);    // channel midpoint (r=15,g=31,b=15)
}

void test_single_stop_lut_is_solid() {
    uint16_t lut[256]; buildPaletteLUT({0xF800}, lut);   // red only
    TEST_ASSERT_EQUAL_HEX16(0xF800, lut[0]);
    TEST_ASSERT_EQUAL_HEX16(0xF800, lut[200]);
}

void test_rotation_list_resolves_and_falls_back() {
    std::vector<Palette> customs;
    customs.push_back({"c0", {0xF800, 0x001F}});           // custom id 16 valid; 17-19 absent
    // presets 0 & 2, custom 16, a reserved bit 10, and an absent custom 17 all "enabled":
    uint32_t mask = (1u << 0) | (1u << 2) | (1u << 10) | (1u << 16) | (1u << 17);
    std::vector<uint8_t> list = activeRotationList(mask, customs);
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)list.size());   // 10 and 17 dropped as unresolvable
    TEST_ASSERT_EQUAL_UINT8(0,  list[0]);
    TEST_ASSERT_EQUAL_UINT8(2,  list[1]);
    TEST_ASSERT_EQUAL_UINT8(16, list[2]);
    std::vector<uint8_t> empty = activeRotationList(0, customs);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)empty.size());
    TEST_ASSERT_EQUAL_UINT8(0, empty[0]);                 // Rainbow fallback
}

void test_preset_stops_are_pinned() {
    std::vector<Palette> none;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)paletteStops(0, none).size());   // Rainbow special-cased -> empty
    std::vector<uint16_t> mg = paletteStops(1, none);                       // Matrix green
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)mg.size());
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#00ff41"), mg[0]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#002200"), mg[1]);
    std::vector<uint16_t> fire = paletteStops(2, none);                     // Fire
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)fire.size());
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#ff0000"), fire[0]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#ff7700"), fire[1]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#ffdd00"), fire[2]);
    std::vector<uint16_t> ice = paletteStops(3, none);                      // Ice
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)ice.size());
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#001a66"), ice[0]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#00ccff"), ice[1]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#ffffff"), ice[2]);
    std::vector<uint16_t> mono = paletteStops(4, none);                     // Mono
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)mono.size());
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#1a1a1a"), mono[0]);
    TEST_ASSERT_EQUAL_HEX16(hexToRgb565("#ffffff"), mono[1]);
}

void test_empty_stops_guard_is_black() {
    uint16_t lut[256]; buildPaletteLUT({}, lut);
    TEST_ASSERT_EQUAL_HEX16(0x0000, lut[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, lut[255]);
}

void test_wheel_lut_landmarks() {
    uint16_t lut[256];
    buildWheelLUT(lut, 0);
    TEST_ASSERT_EQUAL_HEX16(0xF800, lut[0]);     // pure red
    TEST_ASSERT_EQUAL_HEX16(0x07E0, lut[96]);    // pure green
    TEST_ASSERT_EQUAL_HEX16(0x001F, lut[160]);   // pure blue
    // FastLED's rainbow yellow third: at hue 64, r ~= g and b = 0
    TEST_ASSERT_EQUAL(0, lut[64] & 0x1F);
    TEST_ASSERT_TRUE(((lut[64] >> 11) & 0x1F) > 15);
}

void test_wheel_lut_incandescent_warms() {
    uint16_t plain[256], warm[256];
    buildWheelLUT(plain, 0);
    buildWheelLUT(warm, 255);
    // Post-normalization the tint shows as a RATIO shift on mixed colors, not a luminance drop:
    // aqua (h=128) loses blue fraction at full mix, while primaries (red, blue) normalize back
    // to full -- the wheel must never go dim (the LCD dimness bug the normalization exists for).
    TEST_ASSERT_TRUE((warm[128] & 0x1F) < (plain[128] & 0x1F));    // aqua's blue fraction shrinks = warmer
    TEST_ASSERT_TRUE(((warm[0] >> 11) & 0x1F) >= 30);              // red still full
    TEST_ASSERT_EQUAL_HEX16(0x001F, warm[160]);                    // pure blue normalizes back to full (tint is ratio-only on primaries)
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rainbow_lut_is_byte_exact);
    RUN_TEST(test_two_stop_lut_endpoints_and_midpoint);
    RUN_TEST(test_single_stop_lut_is_solid);
    RUN_TEST(test_rotation_list_resolves_and_falls_back);
    RUN_TEST(test_preset_stops_are_pinned);
    RUN_TEST(test_empty_stops_guard_is_black);
    RUN_TEST(test_wheel_lut_landmarks);
    RUN_TEST(test_wheel_lut_incandescent_warms);
    return UNITY_END();
}
