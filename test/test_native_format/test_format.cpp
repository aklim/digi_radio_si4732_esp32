// ============================================================================
// test_format.cpp — Unit tests for radioFormatFrequencyPure().
//
// Run via:
//   pio test -e native -f test_native_format
// or together with the other suites via `pio test -e native`.
//
// radioFormatFrequencyPure is the pure string builder that the thread-safe
// radioFormatFrequency() wrapper in radio.cpp delegates to once it has
// snapshotted the band mode + freq under the mutex. The wrapper itself is
// not unit-tested (it would pull in SI4735 / FreeRTOS); the pure core is
// where the interesting logic lives.
// ============================================================================

#include <string.h>
#include <unity.h>

#include "radio_format.h"

void setUp() {}
void tearDown() {}

// --- FM bands (10 kHz units, rendered as "NN.N MHz") ------------------------

static void test_fm_default_10240_renders_102_4_mhz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_FM, 10240, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("102.4 MHz", buf);
}

static void test_fm_low_edge_8700_renders_87_0_mhz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_FM, 8700, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("87.0 MHz", buf);
}

static void test_fm_high_edge_10800_renders_108_0_mhz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_FM, 10800, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("108.0 MHz", buf);
}

static void test_fm_10245_truncates_to_102_4_mhz() {
    // The ones digit (10 kHz precision) is intentionally dropped — display
    // shows MHz with one decimal, matching the ATS-Mini UX.
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_FM, 10245, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("102.4 MHz", buf);
}

// --- AM bands (1 kHz units, rendered as "NNNN kHz") -------------------------

static void test_am_1530_renders_1530_khz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_AM, 1530, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1530 kHz", buf);
}

static void test_mw_520_renders_520_khz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_AM, 520, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("520 kHz", buf);
}

static void test_sw_9700_renders_9700_khz() {
    char buf[16] = {};
    radioFormatFrequencyPure(MODE_AM, 9700, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("9700 kHz", buf);
}

// --- Robustness: null buf / tiny bufsize must not write or crash ------------

static void test_null_buf_is_safe() {
    // Should not crash. No observable output.
    radioFormatFrequencyPure(MODE_FM, 10240, nullptr, 16);
}

static void test_bufsize_below_2_is_safe() {
    char guard[4] = { 'X', 'X', 'X', 'X' };
    radioFormatFrequencyPure(MODE_FM, 10240, guard, 1);
    // Guard bytes should be untouched.
    TEST_ASSERT_EQUAL_CHAR('X', guard[0]);
    TEST_ASSERT_EQUAL_CHAR('X', guard[1]);
}

static void test_small_buffer_truncates_and_nul_terminates() {
    // bufsize = 4 -> snprintf writes at most 3 chars + NUL.
    char buf[4];
    memset(buf, 'Z', sizeof(buf));
    radioFormatFrequencyPure(MODE_FM, 10240, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR(0, buf[3]);
    TEST_ASSERT_EQUAL_size_t(3, strlen(buf));
}

// --- Test runner ------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fm_default_10240_renders_102_4_mhz);
    RUN_TEST(test_fm_low_edge_8700_renders_87_0_mhz);
    RUN_TEST(test_fm_high_edge_10800_renders_108_0_mhz);
    RUN_TEST(test_fm_10245_truncates_to_102_4_mhz);
    RUN_TEST(test_am_1530_renders_1530_khz);
    RUN_TEST(test_mw_520_renders_520_khz);
    RUN_TEST(test_sw_9700_renders_9700_khz);
    RUN_TEST(test_null_buf_is_safe);
    RUN_TEST(test_bufsize_below_2_is_safe);
    RUN_TEST(test_small_buffer_truncates_and_nul_terminates);
    return UNITY_END();
}
