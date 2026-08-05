#include <unity.h>
#include "../../config.h"
#include "../../palette.h"
#include "../../animations.h"   // ANIM_COUNT: the "unknown id" the favorites test needs

void test_roundtrip_preserves_fields() {
    Config a;
    a.name = "Ada";
    a.brightness = 128;
    a.sleepMin = 5;
    a.startupMode = "fixed";
    a.startupId = 7;
    a.nameMatrixRain = false;
    a.nameBootSplash = true;
    a.bootSplashStyle = "slide";
    a.flip = true;
    a.skinColor = 0x07E0;   // survives hex round-trip (565 bits preserved)
    a.irisColor = 0xF800;   // red iris tint
    a.voidColor = 0x001F;   // Void bg blue
    a.eyelids = false;      // lids off
    a.favoritesMask = (1ull << 0) | (1ull << 4) | (1ull << 17);
    a.palettesEnabled = (1u << 0) | (1u << 2);
    a.paletteRotateSec = 45;
    a.customPalettes.push_back({"Sunset", {hexToRgb565("#ff5500"), hexToRgb565("#aa00ff")}});

    Config b;
    TEST_ASSERT_TRUE(configFromJson(configToJson(a), b));
    TEST_ASSERT_EQUAL_STRING("Ada", b.name.c_str());
    TEST_ASSERT_EQUAL_UINT8(128, b.brightness);
    TEST_ASSERT_EQUAL_UINT8(5, b.sleepMin);
    TEST_ASSERT_EQUAL_STRING("fixed", b.startupMode.c_str());
    TEST_ASSERT_EQUAL_UINT8(7, b.startupId);
    TEST_ASSERT_FALSE(b.nameMatrixRain);
    TEST_ASSERT_TRUE(b.nameBootSplash);
    TEST_ASSERT_EQUAL_STRING("slide", b.bootSplashStyle.c_str());
    TEST_ASSERT_TRUE(b.flip);
    TEST_ASSERT_EQUAL_UINT16(0x07E0, b.skinColor);
    TEST_ASSERT_EQUAL_UINT16(0xF800, b.irisColor);
    TEST_ASSERT_EQUAL_UINT16(0x001F, b.voidColor);
    TEST_ASSERT_FALSE(b.eyelids);
    TEST_ASSERT_EQUAL_UINT64(a.favoritesMask, b.favoritesMask);
    TEST_ASSERT_EQUAL_UINT32(a.palettesEnabled, b.palettesEnabled);
    TEST_ASSERT_EQUAL_UINT16(45, b.paletteRotateSec);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)b.customPalettes.size());
    TEST_ASSERT_EQUAL_STRING("Sunset", b.customPalettes[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)b.customPalettes[0].colors.size());
}

void test_partial_set_leaves_others() {
    Config c;               // defaults
    c.brightness = 200;
    TEST_ASSERT_TRUE(configFromJson("{\"name\":\"Bo\"}", c));
    TEST_ASSERT_EQUAL_STRING("Bo", c.name.c_str());
    TEST_ASSERT_EQUAL_UINT8(200, c.brightness);   // untouched
}

void test_bad_json_returns_false() {
    Config c;
    TEST_ASSERT_FALSE(configFromJson("{not json", c));
}

void test_custom_palette_cap_is_four() {
    Config c;
    const char* j = "{\"palettes\":{\"custom\":["
        "{\"name\":\"a\",\"colors\":[\"#ffffff\"]},"
        "{\"name\":\"b\",\"colors\":[\"#ffffff\"]},"
        "{\"name\":\"c\",\"colors\":[\"#ffffff\"]},"
        "{\"name\":\"d\",\"colors\":[\"#ffffff\"]},"
        "{\"name\":\"e\",\"colors\":[\"#ffffff\"]}]}}";
    TEST_ASSERT_TRUE(configFromJson(j, c));
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)c.customPalettes.size());
}

void test_clamps_out_of_range_scalars() {
    Config c;
    TEST_ASSERT_TRUE(configFromJson("{\"brightness\":300,\"startup\":{\"id\":99},\"sleepMin\":300}", c));
    TEST_ASSERT_EQUAL_UINT8(255, c.brightness);
    TEST_ASSERT_EQUAL_UINT8(0, c.startupId);
    TEST_ASSERT_EQUAL_UINT8(255, c.sleepMin);
    TEST_ASSERT_TRUE(configFromJson("{\"brightness\":-5,\"sleepMin\":-1}", c));
    TEST_ASSERT_EQUAL_UINT8(0, c.brightness);
    TEST_ASSERT_EQUAL_UINT8(0, c.sleepMin);
}

void test_custom_palette_id_matches_enabled_bit() {
    Config a;   // defaults: startupId 0, so the only "id":16 in the JSON is the custom's
    a.customPalettes.push_back({"Sunset", {hexToRgb565("#ff5500"), hexToRgb565("#aa00ff")}});
    a.palettesEnabled = (1u << CUSTOM_ID_BASE);   // enable the single custom (bit 16)
    std::string js = configToJson(a);
    TEST_ASSERT_TRUE(js.find("\"id\":16")  != std::string::npos);   // custom stamped at 16
    TEST_ASSERT_TRUE(js.find("\"id\":100") == std::string::npos);   // not the old 100+i
    Config b;
    TEST_ASSERT_TRUE(configFromJson(js, b));
    TEST_ASSERT_EQUAL_UINT32(a.palettesEnabled, b.palettesEnabled); // bit 16 round-trips
}

void test_audio_mode_id24_roundtrips() {
    Config c;
    c.favoritesMask = (1ull << 24);
    c.startupMode = "fixed"; c.startupId = 24;
    std::string json = configToJson(c);
    Config back;
    TEST_ASSERT_TRUE(configFromJson(json, back));
    TEST_ASSERT_EQUAL_UINT64((1ull << 24), back.favoritesMask & (1ull << 24));
    TEST_ASSERT_EQUAL_UINT8(24, back.startupId);
}

void test_maxfps_roundtrips_and_clamps() {
    Config a;
    TEST_ASSERT_EQUAL_UINT8(30, a.maxFps);                 // default
    a.maxFps = 24;
    Config b;
    TEST_ASSERT_TRUE(configFromJson(configToJson(a), b));
    TEST_ASSERT_EQUAL_UINT8(24, b.maxFps);                 // round-trip
    Config c;                                              // clamp high/low
    TEST_ASSERT_TRUE(configFromJson("{\"maxFps\":200}", c));
    TEST_ASSERT_EQUAL_UINT8(120, c.maxFps);
    TEST_ASSERT_TRUE(configFromJson("{\"maxFps\":0}", c));
    TEST_ASSERT_EQUAL_UINT8(1, c.maxFps);
    Config d; d.maxFps = 45;                               // absent field leaves default/existing
    TEST_ASSERT_TRUE(configFromJson("{\"name\":\"x\"}", d));
    TEST_ASSERT_EQUAL_UINT8(45, d.maxFps);
}

void test_sb_palette_roundtrips() {
    Config c;
    TEST_ASSERT_FALSE(c.sbPalette);                              // default off
    TEST_ASSERT_TRUE(configFromJson("{\"sbPalette\":true}", c)); // partial set
    TEST_ASSERT_TRUE(c.sbPalette);
    Config d;
    TEST_ASSERT_TRUE(configFromJson(configToJson(c), d));        // survives the echo round-trip
    TEST_ASSERT_TRUE(d.sbPalette);
}

void test_slideshow_sec_default_and_clamp() {
    Config c;
    TEST_ASSERT_EQUAL_UINT16(5, c.slideshowSec);                 // default
    TEST_ASSERT_TRUE(configFromJson("{\"slideshowSec\": 12}", c));
    TEST_ASSERT_EQUAL_UINT16(12, c.slideshowSec);
    TEST_ASSERT_TRUE(configFromJson("{\"slideshowSec\": 0}", c)); // 0 -> clamp to 1
    TEST_ASSERT_EQUAL_UINT16(1, c.slideshowSec);
    TEST_ASSERT_TRUE(configFromJson("{\"slideshowSec\": 999}", c)); // > 60 -> 60
    TEST_ASSERT_EQUAL_UINT16(60, c.slideshowSec);
    // round-trips through JSON
    Config d;
    TEST_ASSERT_TRUE(configFromJson(configToJson(c), d));
    TEST_ASSERT_EQUAL_UINT16(60, d.slideshowSec);
}

void test_gif_sec_default_and_clamp() {
    Config c;
    TEST_ASSERT_EQUAL_UINT16(6, c.gifSec);                       // default, distinct from slideshowSec
    TEST_ASSERT_TRUE(configFromJson("{\"gifSec\": 12}", c));
    TEST_ASSERT_EQUAL_UINT16(12, c.gifSec);
    TEST_ASSERT_EQUAL_UINT16(5, c.slideshowSec);                 // the two must not be coupled
    TEST_ASSERT_TRUE(configFromJson("{\"gifSec\": 0}", c));       // 0 -> clamp to 1
    TEST_ASSERT_EQUAL_UINT16(1, c.gifSec);
    TEST_ASSERT_TRUE(configFromJson("{\"gifSec\": 999}", c));     // > 60 -> 60
    TEST_ASSERT_EQUAL_UINT16(60, c.gifSec);
    Config d;
    TEST_ASSERT_TRUE(configFromJson(configToJson(c), d));
    TEST_ASSERT_EQUAL_UINT16(60, d.gifSec);
}

void test_cycle_fields_roundtrip_and_clamp() {
    Config a;
    TEST_ASSERT_EQUAL_UINT16(0, a.cycleSec);                    // defaults: off, follow favorites
    TEST_ASSERT_EQUAL_UINT64(0, a.cycleMask);
    a.cycleSec = 45;
    a.cycleMask = (1ull << 2) | (1ull << 30);
    Config b;
    TEST_ASSERT_TRUE(configFromJson(configToJson(a), b));
    TEST_ASSERT_EQUAL_UINT16(45, b.cycleSec);
    TEST_ASSERT_EQUAL_UINT64(a.cycleMask, b.cycleMask);
    Config c;                                                   // clamp low + drop out-of-range ids
    TEST_ASSERT_TRUE(configFromJson("{\"cycleSec\":-5,\"cycleAnims\":[3,99,-1]}", c));
    TEST_ASSERT_EQUAL_UINT16(0, c.cycleSec);
    TEST_ASSERT_EQUAL_UINT64(1ull << 3, c.cycleMask);           // 99 and -1 dropped
    TEST_ASSERT_TRUE(configFromJson("{\"cycleSec\":70000}", c)); // clamp high
    TEST_ASSERT_EQUAL_UINT16(65535, c.cycleSec);
    Config d; d.cycleSec = 9;                                   // absent fields untouched
    TEST_ASSERT_TRUE(configFromJson("{\"name\":\"x\"}", d));
    TEST_ASSERT_EQUAL_UINT16(9, d.cycleSec);
}

void test_stay_awake_usb_roundtrips() {
    Config c;
    TEST_ASSERT_TRUE(c.stayAwakeUsb);                                 // default ON
    TEST_ASSERT_TRUE(configFromJson("{\"stayAwakeUsb\":false}", c));  // partial set
    TEST_ASSERT_FALSE(c.stayAwakeUsb);
    Config d;
    TEST_ASSERT_TRUE(configFromJson(configToJson(c), d));             // survives the echo round-trip
    TEST_ASSERT_FALSE(d.stayAwakeUsb);
}

void test_qr_fields_roundtrip_and_validate() {
    Config a;
    TEST_ASSERT_EQUAL_UINT8(25, a.qrSize);                           // default: the nullphase.net/oc QR (fresh device works out of the box)
    TEST_ASSERT_EQUAL_STRING("https://nullphase.net/oc/", a.qrText.c_str());
    TEST_ASSERT_EQUAL_UINT(158u, (unsigned)a.qrBits.size());          // 25x25 packed: ceil(625/8)*2
    a.qrText = "https://nullphase.net/oc/";
    a.qrSize = 25;
    a.qrBits = "a5";                                                  // content irrelevant to the codec
    Config b;
    TEST_ASSERT_TRUE(configFromJson(configToJson(a), b));
    TEST_ASSERT_EQUAL_STRING(a.qrText.c_str(), b.qrText.c_str());
    TEST_ASSERT_EQUAL_UINT8(25, b.qrSize);
    TEST_ASSERT_EQUAL_STRING("a5", b.qrBits.c_str());
    Config c;
    TEST_ASSERT_TRUE(configFromJson("{\"qrSize\":200}", c));          // out of range -> unconfigured
    TEST_ASSERT_EQUAL_UINT8(0, c.qrSize);
    TEST_ASSERT_TRUE(configFromJson("{\"qrSize\":7}", c));            // below min QR (21) -> unconfigured
    TEST_ASSERT_EQUAL_UINT8(0, c.qrSize);
    // Ceiling is version 18 (89 modules), NOT version 40: past v18 the qrBits hex outgrows the
    // 2048 B serial line cap and can physically never arrive, so accepting the size was a lie.
    TEST_ASSERT_TRUE(configFromJson("{\"qrSize\":89}", c));
    TEST_ASSERT_EQUAL_UINT8(89, c.qrSize);
    TEST_ASSERT_TRUE(configFromJson("{\"qrSize\":90}", c));           // just past v18 -> unconfigured
    TEST_ASSERT_EQUAL_UINT8(0, c.qrSize);
    TEST_ASSERT_TRUE(configFromJson("{\"qrBits\":\"12zz\"}", c));     // non-hex (RX-mangled line) -> dropped
    TEST_ASSERT_EQUAL_STRING("", c.qrBits.c_str());
}

void test_qr_module_unpacks_msb_first_row_major() {
    // 4x4 pattern, 16 bits = 2 bytes: rows 1000 / 0110 / 0001 / 1111 -> 0x86 0x1F
    std::string bits = "861f";
    bool want[4][4] = {{1,0,0,0},{0,1,1,0},{0,0,0,1},{1,1,1,1}};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            TEST_ASSERT_EQUAL(want[r][c], qrModule(bits, 4, r, c));
    TEST_ASSERT_FALSE(qrModule(bits, 4, 4, 0));   // out of range
    TEST_ASSERT_FALSE(qrModule(bits, 4, 0, -1));
    TEST_ASSERT_FALSE(qrModule("86", 4, 3, 3));   // truncated bits -> false, no read past end
}

void test_favorites_drop_reserved_holes() {
    Config c;
    // The "unknown id" here is ANIM_COUNT, not a literal: it is by definition one past the last
    // playable, so it stays unknown as ids are added. Hardcoding it broke this test twice (47 when
    // Greetz landed, 48 when the GIF player did).
    const std::string unknown = std::to_string(ANIM_COUNT);
    TEST_ASSERT_TRUE(configFromJson("{\"favorites\":[3," + unknown + ",38,42],\"cycleAnims\":["
                                    + unknown + ",41]}", c));
    TEST_ASSERT_EQUAL_UINT64((1ull << 3) | (1ull << 38), c.favoritesMask);  // unknown + 42 (debug) dropped
    TEST_ASSERT_EQUAL_UINT64(1ull << 41, c.cycleMask);                      // unknown dropped
}

// Oversize policy: display copy (name, mode strings, palette labels) clamps -- truncation stays
// usable; the QR pair rejects, keeping the previous value -- a truncated URL or bitmap is silently
// wrong. Either way the rest of the same line must decode untouched.
void test_string_caps_clamp_or_reject() {
    Config c;
    std::string longName(CFG_NAME_MAX + 20, 'n');
    TEST_ASSERT_TRUE(configFromJson("{\"name\":\"" + longName + "\",\"brightness\":77}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_NAME_MAX, (unsigned)c.name.size());   // clamped, not dropped
    TEST_ASSERT_EQUAL_UINT8(77, c.brightness);                       // same line still decoded

    std::string prevText = c.qrText;                                 // the default URL
    std::string bigText(CFG_QR_TEXT_MAX + 1, 'q');
    TEST_ASSERT_TRUE(configFromJson("{\"qrText\":\"" + bigText + "\",\"sleepMin\":9}", c));
    TEST_ASSERT_EQUAL_STRING(prevText.c_str(), c.qrText.c_str());    // rejected -> previous kept
    TEST_ASSERT_EQUAL_UINT8(9, c.sleepMin);
    std::string okText(CFG_QR_TEXT_MAX, 'q');                        // at cap -> accepted
    TEST_ASSERT_TRUE(configFromJson("{\"qrText\":\"" + okText + "\"}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_QR_TEXT_MAX, (unsigned)c.qrText.size());

    std::string okBits(CFG_QR_BITS_MAX, 'a');                        // at cap -> accepted
    TEST_ASSERT_TRUE(configFromJson("{\"qrBits\":\"" + okBits + "\"}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_QR_BITS_MAX, (unsigned)c.qrBits.size());
    std::string bigBits(CFG_QR_BITS_MAX + 2, 'b');
    TEST_ASSERT_TRUE(configFromJson("{\"qrBits\":\"" + bigBits + "\"}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_QR_BITS_MAX, (unsigned)c.qrBits.size());   // rejected -> previous kept
    TEST_ASSERT_EQUAL('a', c.qrBits[0]);

    std::string bigMode(CFG_MODE_STR_MAX + 30, 'm');
    TEST_ASSERT_TRUE(configFromJson("{\"startup\":{\"mode\":\"" + bigMode + "\"},"
                                    "\"nameStyle\":{\"splashStyle\":\"" + bigMode + "\"}}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_MODE_STR_MAX, (unsigned)c.startupMode.size());
    TEST_ASSERT_EQUAL_UINT(CFG_MODE_STR_MAX, (unsigned)c.bootSplashStyle.size());

    std::string palName(CFG_PAL_NAME_MAX + 10, 'p');
    TEST_ASSERT_TRUE(configFromJson("{\"palettes\":{\"custom\":[{\"name\":\"" + palName + "\","
        "\"colors\":[\"#ffffff\",\"#ffffff\",\"#ffffff\",\"#ffffff\",\"#ffffff\",\"#ffffff\",\"#ffffff\"]}]}}", c));
    TEST_ASSERT_EQUAL_UINT(CFG_PAL_NAME_MAX, (unsigned)c.customPalettes[0].name.size());
    TEST_ASSERT_EQUAL_UINT(CFG_PAL_COLORS_MAX, (unsigned)c.customPalettes[0].colors.size());  // 7 stops -> 5
}

// The guard that makes NVS's 4000 B nvs_set_str cap unreachable: every string field at its decode
// cap, every mask full, all 4 custom palettes maxed -- and the serialized blob must still clear
// 3900 B. If adding a field or raising a cap trips this, trim a cap; do not raise 3900 (the last
// 100 B is margin for JSON escaping and the null terminator NVS counts).
void test_maximal_config_stays_under_nvs_cap() {
    Config c;
    c.name = std::string(CFG_NAME_MAX, 'W');
    c.brightness = 255; c.sleepMin = 255; c.maxFps = 120;
    c.slideshowSec = 60; c.gifSec = 60; c.catVariant = 5;
    c.startupMode = std::string(CFG_MODE_STR_MAX, 'm');
    c.bootSplashStyle = std::string(CFG_MODE_STR_MAX, 's');
    c.cycleSec = 65535; c.paletteRotateSec = 65535;
    c.palettesEnabled = 0xFFFFFFFFu;
    for (int i = 0; i < ANIM_COUNT; i++)
        if (isPlayableId(i)) { c.favoritesMask |= (1ull << i); c.cycleMask |= (1ull << i); }
    c.qrText = std::string(CFG_QR_TEXT_MAX, 'q');
    c.qrSize = CFG_QR_SIZE_MAX;
    c.qrBits = std::string(CFG_QR_BITS_MAX, 'f');
    for (int i = 0; i < MAX_CUSTOM; i++)
        c.customPalettes.push_back({std::string(CFG_PAL_NAME_MAX, 'p'),
                                    std::vector<uint16_t>(CFG_PAL_COLORS_MAX, 0xFFFF)});
    std::string js = configToJson(c);
    TEST_ASSERT_LESS_THAN_UINT32(3900u, (uint32_t)js.size());
    Config b;                                    // and the decode caps accept their own maximum
    TEST_ASSERT_TRUE(configFromJson(js, b));
    TEST_ASSERT_EQUAL_UINT(CFG_NAME_MAX, (unsigned)b.name.size());
    TEST_ASSERT_EQUAL_UINT(CFG_QR_TEXT_MAX, (unsigned)b.qrText.size());
    TEST_ASSERT_EQUAL_UINT(CFG_QR_BITS_MAX, (unsigned)b.qrBits.size());
    TEST_ASSERT_EQUAL_UINT(MAX_CUSTOM, (unsigned)b.customPalettes.size());
}

void test_catVariant_clamps_out_of_range() {
  Config c;
  TEST_ASSERT_TRUE(configFromJson("{\"catVariant\":3}", c));
  TEST_ASSERT_EQUAL_UINT8(3, c.catVariant);
  TEST_ASSERT_TRUE(configFromJson("{\"catVariant\":9}", c));   // out of 0..5
  TEST_ASSERT_EQUAL_UINT8(0, c.catVariant);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_preserves_fields);
    RUN_TEST(test_partial_set_leaves_others);
    RUN_TEST(test_bad_json_returns_false);
    RUN_TEST(test_custom_palette_cap_is_four);
    RUN_TEST(test_clamps_out_of_range_scalars);
    RUN_TEST(test_custom_palette_id_matches_enabled_bit);
    RUN_TEST(test_audio_mode_id24_roundtrips);
    RUN_TEST(test_maxfps_roundtrips_and_clamps);
    RUN_TEST(test_sb_palette_roundtrips);
    RUN_TEST(test_slideshow_sec_default_and_clamp);
    RUN_TEST(test_gif_sec_default_and_clamp);
    RUN_TEST(test_cycle_fields_roundtrip_and_clamp);
    RUN_TEST(test_stay_awake_usb_roundtrips);
    RUN_TEST(test_qr_fields_roundtrip_and_validate);
    RUN_TEST(test_qr_module_unpacks_msb_first_row_major);
    RUN_TEST(test_favorites_drop_reserved_holes);
    RUN_TEST(test_string_caps_clamp_or_reject);
    RUN_TEST(test_maximal_config_stays_under_nvs_cap);
    RUN_TEST(test_catVariant_clamps_out_of_range);
    return UNITY_END();
}
