// ============================================================================
// test_seek_step.cpp — Unit tests for the auto-seek frequency-step helper.
//
// Run via:
//   pio test -e native -f test_native_seek
// or together with the other suites via `pio test -e native`.
//
// seekNextFreq() lives in include/seek_step.h so Seek.cpp can call it on
// hardware while these tests link it directly without Arduino / SI4735.
// The wrap arithmetic is the only thing worth testing in isolation —
// the full state machine in Seek.cpp is a thin loop over this helper
// plus radioScanMeasure, which is exercised on hardware.
// ============================================================================

#include <unity.h>

#include "seek_step.h"

void setUp() {}
void tearDown() {}

// --- SEEK_UP within band bounds --------------------------------------------

static void test_up_step_inside_band() {
    // FM-ish numbers: 87.0 MHz .. 108.0 MHz, step 10 (= 100 kHz).
    TEST_ASSERT_EQUAL_UINT16(10250,
        seekNextFreq(10240, 8700, 10800, 10, SEEK_UP));
}

static void test_up_step_one_below_max_lands_on_max() {
    // 107.9 + 100 kHz = 108.0 (max) — must NOT wrap yet.
    TEST_ASSERT_EQUAL_UINT16(10800,
        seekNextFreq(10790, 8700, 10800, 10, SEEK_UP));
}

static void test_up_step_at_max_wraps_to_min() {
    // 108.0 MHz + one step falls off the top; wrap to 87.0.
    TEST_ASSERT_EQUAL_UINT16(8700,
        seekNextFreq(10800, 8700, 10800, 10, SEEK_UP));
}

// --- SEEK_DOWN within band bounds ------------------------------------------

static void test_down_step_inside_band() {
    TEST_ASSERT_EQUAL_UINT16(10230,
        seekNextFreq(10240, 8700, 10800, 10, SEEK_DOWN));
}

static void test_down_step_one_above_min_lands_on_min() {
    // 87.1 - 100 kHz = 87.0 (min) — must NOT wrap yet.
    TEST_ASSERT_EQUAL_UINT16(8700,
        seekNextFreq(8710, 8700, 10800, 10, SEEK_DOWN));
}

static void test_down_step_at_min_wraps_to_max() {
    // 87.0 - one step falls off the bottom; wrap to 108.0.
    TEST_ASSERT_EQUAL_UINT16(10800,
        seekNextFreq(8700, 8700, 10800, 10, SEEK_DOWN));
}

// --- AM/SW step granularity -------------------------------------------------

static void test_mw_5khz_step_up() {
    // MW: 520..1710 kHz, 9 kHz channel grid in region 1 (we configure 5
    // in the band table today). The helper is agnostic — it just adds step.
    TEST_ASSERT_EQUAL_UINT16(1535,
        seekNextFreq(1530, 520, 1710, 5, SEEK_UP));
}

static void test_mw_5khz_step_wrap_down() {
    TEST_ASSERT_EQUAL_UINT16(1710,
        seekNextFreq(520, 520, 1710, 5, SEEK_DOWN));
}

// --- Edge: step larger than current..min distance --------------------------

static void test_down_wrap_when_step_exceeds_floor_gap() {
    // current=8705, min=8700, step=10 → 8705-10 underflows in u16 space
    // but the helper's guard (current < min + step) short-circuits to max.
    TEST_ASSERT_EQUAL_UINT16(10800,
        seekNextFreq(8705, 8700, 10800, 10, SEEK_DOWN));
}

// --- Edge: step larger than max..current distance --------------------------

static void test_up_wrap_when_step_exceeds_ceiling_gap() {
    // current=10795, max=10800, step=10 → 10795+10 = 10805 > max, wrap.
    TEST_ASSERT_EQUAL_UINT16(8700,
        seekNextFreq(10795, 8700, 10800, 10, SEEK_UP));
}

// --- Test runner ------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_up_step_inside_band);
    RUN_TEST(test_up_step_one_below_max_lands_on_max);
    RUN_TEST(test_up_step_at_max_wraps_to_min);
    RUN_TEST(test_down_step_inside_band);
    RUN_TEST(test_down_step_one_above_min_lands_on_min);
    RUN_TEST(test_down_step_at_min_wraps_to_max);
    RUN_TEST(test_mw_5khz_step_up);
    RUN_TEST(test_mw_5khz_step_wrap_down);
    RUN_TEST(test_down_wrap_when_step_exceeds_floor_gap);
    RUN_TEST(test_up_wrap_when_step_exceeds_ceiling_gap);
    return UNITY_END();
}
