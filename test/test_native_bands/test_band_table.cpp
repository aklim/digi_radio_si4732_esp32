// ============================================================================
// test_band_table.cpp — Invariant checks for g_bands[] in band_table.cpp.
//
// Run via:
//   pio test -e native -f test_native_bands
// or together with the other suites via `pio test -e native`.
//
// The band table is pure data (name / min / max / default / step), so the
// tests here are structural: every band's default frequency sits inside
// [min, max], its step is non-zero, its name is non-empty, and the table
// starts with FM at index 0 (guarantees first-boot default matches v1).
// ============================================================================

#include <string.h>
#include <unity.h>

#include "radio_bands.h"

void setUp() {}
void tearDown() {}

static void test_band_count_matches_shipping_layout() {
    // Four bands today: FM Broadcast + MW + SW 41m + SW 31m. If this
    // changes, update CLAUDE.md "Bands currently:" line and touch up
    // the docs before bumping this assertion.
    TEST_ASSERT_EQUAL_size_t(4, g_bandCount);
}

static void test_first_band_is_fm_broadcast() {
    // Index 0 is the first-boot default (before NVS has a saved band).
    // Keeping FM at 0 preserves the v1 single-band UX.
    TEST_ASSERT_EQUAL_UINT8(BAND_FM, g_bands[0].type);
    TEST_ASSERT_EQUAL_UINT8(MODE_FM, g_bands[0].mode);
}

static void test_every_band_has_non_empty_name() {
    for (size_t i = 0; i < g_bandCount; i++) {
        TEST_ASSERT_NOT_NULL(g_bands[i].name);
        TEST_ASSERT_TRUE_MESSAGE(strlen(g_bands[i].name) > 0,
                                 "band name must be non-empty");
    }
}

static void test_every_band_has_positive_step() {
    for (size_t i = 0; i < g_bandCount; i++) {
        TEST_ASSERT_TRUE_MESSAGE(g_bands[i].step > 0,
                                 "band step must be > 0 (encoder delta)");
    }
}

static void test_every_band_has_min_below_max() {
    for (size_t i = 0; i < g_bandCount; i++) {
        TEST_ASSERT_TRUE_MESSAGE(g_bands[i].minFreq < g_bands[i].maxFreq,
                                 "band minFreq must be < maxFreq");
    }
}

static void test_every_band_default_is_inside_range() {
    for (size_t i = 0; i < g_bandCount; i++) {
        TEST_ASSERT_TRUE_MESSAGE(
            g_bands[i].defaultFreq >= g_bands[i].minFreq,
            "defaultFreq must be >= minFreq");
        TEST_ASSERT_TRUE_MESSAGE(
            g_bands[i].defaultFreq <= g_bands[i].maxFreq,
            "defaultFreq must be <= maxFreq");
    }
}

static void test_every_band_current_is_inside_range() {
    // currentFreq is a mutable runtime field, but the static initialiser
    // in band_table.cpp must start inside the band's range so first-boot
    // tune is valid before any NVS restore runs.
    for (size_t i = 0; i < g_bandCount; i++) {
        TEST_ASSERT_TRUE_MESSAGE(
            g_bands[i].currentFreq >= g_bands[i].minFreq,
            "currentFreq must be >= minFreq");
        TEST_ASSERT_TRUE_MESSAGE(
            g_bands[i].currentFreq <= g_bands[i].maxFreq,
            "currentFreq must be <= maxFreq");
    }
}

static void test_fm_default_is_canonical_102_4_mhz() {
    // 10240 (10 kHz units) == 102.40 MHz. Matches v1 default and the
    // frequency the hardware has been running on since day one — a
    // regression here would silently change first-boot UX.
    TEST_ASSERT_EQUAL_UINT16(10240, g_bands[0].defaultFreq);
    TEST_ASSERT_EQUAL_UINT16(10, g_bands[0].step);  // 100 kHz
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_band_count_matches_shipping_layout);
    RUN_TEST(test_first_band_is_fm_broadcast);
    RUN_TEST(test_every_band_has_non_empty_name);
    RUN_TEST(test_every_band_has_positive_step);
    RUN_TEST(test_every_band_has_min_below_max);
    RUN_TEST(test_every_band_default_is_inside_range);
    RUN_TEST(test_every_band_current_is_inside_range);
    RUN_TEST(test_fm_default_is_canonical_102_4_mhz);
    return UNITY_END();
}
