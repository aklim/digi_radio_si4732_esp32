// ============================================================================
// test_rds_ct.cpp — Unit tests for the RDS Clock-Time helpers.
//
// Run via:
//   pio test -e native -f test_native_ct
// or together with the other suites via `pio test -e native`.
//
// rdsCtHmIsValid() and rdsCtFormatHM() live in include/rds_ct.h so radio.cpp
// can call them on hardware while these tests link them directly without
// Arduino / SI4735. The helpers are the only non-trivial pieces of the CT
// pipeline — the rest is a straight mirror pattern exercised on device.
//
// Deliberately NO date-component validator here: Ukrainian FM broadcasts
// land years in 1906/1907 due to a PU2CLR MJD underflow after LTO
// correction, but the HH:MM values are still correct. Gating CT display
// on a full-date validator hides legitimate clock data; see rds_ct.h.
// ============================================================================

#include <unity.h>

#include "rds_ct.h"

void setUp() {}
void tearDown() {}

// --- rdsCtHmIsValid() ------------------------------------------------------

static void test_hm_valid_midday() {
    TEST_ASSERT_TRUE(rdsCtHmIsValid(12, 34));
}

static void test_hm_valid_midnight() {
    TEST_ASSERT_TRUE(rdsCtHmIsValid(0, 0));
}

static void test_hm_valid_max() {
    TEST_ASSERT_TRUE(rdsCtHmIsValid(23, 59));
}

static void test_hm_invalid_hour_24() {
    TEST_ASSERT_FALSE(rdsCtHmIsValid(24, 0));
}

static void test_hm_invalid_minute_60() {
    TEST_ASSERT_FALSE(rdsCtHmIsValid(12, 60));
}

static void test_hm_invalid_hour_overflow() {
    TEST_ASSERT_FALSE(rdsCtHmIsValid(250, 0));
}

// --- rdsCtFormatHM() -------------------------------------------------------

static void test_format_midday() {
    char buf[8] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
    TEST_ASSERT_EQUAL_UINT(5u, rdsCtFormatHM(12, 34, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("12:34", buf);
}

static void test_format_single_digit_pads() {
    char buf[8];
    TEST_ASSERT_EQUAL_UINT(5u, rdsCtFormatHM(5, 9, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("05:09", buf);
}

static void test_format_midnight() {
    char buf[8];
    TEST_ASSERT_EQUAL_UINT(5u, rdsCtFormatHM(0, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("00:00", buf);
}

static void test_format_max_bounds() {
    char buf[8];
    TEST_ASSERT_EQUAL_UINT(5u, rdsCtFormatHM(23, 59, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("23:59", buf);
}

static void test_format_buffer_too_small_empties_output() {
    char buf[5] = {'x', 'x', 'x', 'x', 'x'};
    TEST_ASSERT_EQUAL_UINT(0u, rdsCtFormatHM(12, 34, buf, 5));
    TEST_ASSERT_EQUAL_CHAR(0, buf[0]);
}

static void test_format_zero_size_safe() {
    TEST_ASSERT_EQUAL_UINT(0u, rdsCtFormatHM(12, 34, nullptr, 0));
}

static void test_format_null_buffer_safe() {
    TEST_ASSERT_EQUAL_UINT(0u, rdsCtFormatHM(12, 34, nullptr, 16));
}

static void test_format_clamps_overflow() {
    // Defensive clamp — caller should have validated, but if garbage leaks
    // through we print "99:99" rather than smear three-digit glyphs into
    // the next row.
    char buf[8];
    TEST_ASSERT_EQUAL_UINT(5u, rdsCtFormatHM(250, 180, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("99:99", buf);
}

// --- Test runner ------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hm_valid_midday);
    RUN_TEST(test_hm_valid_midnight);
    RUN_TEST(test_hm_valid_max);
    RUN_TEST(test_hm_invalid_hour_24);
    RUN_TEST(test_hm_invalid_minute_60);
    RUN_TEST(test_hm_invalid_hour_overflow);
    RUN_TEST(test_format_midday);
    RUN_TEST(test_format_single_digit_pads);
    RUN_TEST(test_format_midnight);
    RUN_TEST(test_format_max_bounds);
    RUN_TEST(test_format_buffer_too_small_empties_output);
    RUN_TEST(test_format_zero_size_safe);
    RUN_TEST(test_format_null_buffer_safe);
    RUN_TEST(test_format_clamps_overflow);
    return UNITY_END();
}
