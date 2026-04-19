// ============================================================================
// test_rds_sanitize.cpp — Unit tests for rdsSanitizeRt().
//
// Run via:
//   pio test -e native -f test_native_rds
// or together with the other suites via `pio test -e native`.
//
// Covers the gate that decides whether the PU2CLR library's rds_buffer2A[]
// is printable enough to show on the UI — the same behaviour that fixed
// the stray 0x2F-lookalike RT flashes in v2.1.x.
// ============================================================================

#include <string.h>
#include <unity.h>

#include "rds_sanitize.h"

void setUp() {}
void tearDown() {}

static void test_plain_text_passes_through() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("HELLO", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO", dst);
}

static void test_leading_whitespace_stripped() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("   HELLO", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO", dst);
}

static void test_trailing_whitespace_trimmed() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("HELLO   ", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO", dst);
}

static void test_leading_and_trailing_trimmed_together() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("   HELLO WORLD   ", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO WORLD", dst);
}

static void test_non_printable_folded_to_space() {
    char dst[65] = {};
    // 0x01 (SOH) is a control char — fold to space.
    bool ok = rdsSanitizeRt("A\x01" "B", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("A B", dst);
}

static void test_high_byte_folded_to_space() {
    char dst[65] = {};
    // 0xA0 (> 0x7E) also folds to space.
    bool ok = rdsSanitizeRt("A\xA0" "B", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("A B", dst);
}

static void test_cr_terminates() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("HELLO\rWORLD", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO", dst);
}

static void test_lf_terminates() {
    char dst[65] = {};
    bool ok = rdsSanitizeRt("HELLO\nWORLD", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HELLO", dst);
}

static void test_empty_input_returns_false() {
    char dst[65];
    memset(dst, 'Z', sizeof(dst));
    bool ok = rdsSanitizeRt("", dst, sizeof(dst));
    TEST_ASSERT_FALSE(ok);
    // dst should still be NUL-terminated (no writes happened, but the early
    // exit on empty-after-whitespace path sets dst[0] = 0).
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
}

static void test_all_whitespace_returns_false() {
    char dst[65];
    memset(dst, 'Z', sizeof(dst));
    bool ok = rdsSanitizeRt("        ", dst, sizeof(dst));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
}

static void test_all_non_printable_returns_false() {
    // Non-printables fold to space, then right-trim leaves nothing.
    char dst[65];
    memset(dst, 'Z', sizeof(dst));
    bool ok = rdsSanitizeRt("\x01\x02\x03\x04", dst, sizeof(dst));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
}

static void test_64_byte_source_boundary() {
    // Source longer than 64 bytes must be truncated at 64. Use 70 A's; the
    // function reads only the first 64 and ignores the rest.
    char src[71];
    memset(src, 'A', 70);
    src[70] = 0;
    char dst[65] = {};
    bool ok = rdsSanitizeRt(src, dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(64, strlen(dst));
}

static void test_small_dst_truncates_and_terminates() {
    // dstSize = 4 -> at most 3 chars + NUL (and then trailing-space trim may
    // shorten further, but with "HELLO" the result is "HEL").
    char dst[4];
    memset(dst, 'Z', sizeof(dst));
    bool ok = rdsSanitizeRt("HELLO", dst, sizeof(dst));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("HEL", dst);
}

static void test_null_src_returns_false() {
    char dst[65];
    memset(dst, 'Z', sizeof(dst));
    bool ok = rdsSanitizeRt(nullptr, dst, sizeof(dst));
    TEST_ASSERT_FALSE(ok);
    // No write happened — guard byte still there.
    TEST_ASSERT_EQUAL_CHAR('Z', dst[0]);
}

static void test_null_dst_returns_false() {
    bool ok = rdsSanitizeRt("HELLO", nullptr, 16);
    TEST_ASSERT_FALSE(ok);
}

static void test_dstsize_below_2_returns_false() {
    char dst[4] = { 'Z', 'Z', 'Z', 'Z' };
    bool ok = rdsSanitizeRt("HELLO", dst, 1);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_CHAR('Z', dst[0]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_text_passes_through);
    RUN_TEST(test_leading_whitespace_stripped);
    RUN_TEST(test_trailing_whitespace_trimmed);
    RUN_TEST(test_leading_and_trailing_trimmed_together);
    RUN_TEST(test_non_printable_folded_to_space);
    RUN_TEST(test_high_byte_folded_to_space);
    RUN_TEST(test_cr_terminates);
    RUN_TEST(test_lf_terminates);
    RUN_TEST(test_empty_input_returns_false);
    RUN_TEST(test_all_whitespace_returns_false);
    RUN_TEST(test_all_non_printable_returns_false);
    RUN_TEST(test_64_byte_source_boundary);
    RUN_TEST(test_small_dst_truncates_and_terminates);
    RUN_TEST(test_null_src_returns_false);
    RUN_TEST(test_null_dst_returns_false);
    RUN_TEST(test_dstsize_below_2_returns_false);
    return UNITY_END();
}
