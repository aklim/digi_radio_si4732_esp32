// ============================================================================
// test_preset.cpp — Unit tests for the memory-preset u32 pack/unpack codec.
//
// Run via:
//   pio test -e native -f test_native_preset
// or together with the other suites via `pio test -e native`.
//
// The codec is header-only (include/preset_pack.h) so these tests also act as
// the de facto spec for the NVS bit layout. persist.cpp stores the raw u32
// directly under "preset<N>" keys; this test guards the wire format.
// ============================================================================

#include <unity.h>

#include "preset_pack.h"

void setUp() {}
void tearDown() {}

// --- Empty-slot encoding ----------------------------------------------------

static void test_empty_slot_packs_to_zero() {
    PresetSlot p = {0, 5, 1530};  // valid=0 should zero out band/freq too
    TEST_ASSERT_EQUAL_UINT32(0u, presetPack(p));
}

static void test_zero_unpacks_to_empty() {
    PresetSlot p = presetUnpack(0u);
    TEST_ASSERT_EQUAL_UINT8(0, p.valid);
    TEST_ASSERT_EQUAL_UINT8(0, p.band);
    TEST_ASSERT_EQUAL_UINT16(0, p.freq);
}

// --- Bit-layout invariants --------------------------------------------------

static void test_valid_slot_has_msb_set() {
    PresetSlot p = {1, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, presetPack(p));
}

static void test_band_lands_in_bits_19_16() {
    PresetSlot p = {1, 0xF, 0};
    TEST_ASSERT_EQUAL_UINT32(0x80000000u | 0x000F0000u, presetPack(p));
}

static void test_freq_lands_in_low_16_bits() {
    PresetSlot p = {1, 0, 0xABCD};
    TEST_ASSERT_EQUAL_UINT32(0x80000000u | 0x0000ABCDu, presetPack(p));
}

static void test_reserved_bits_are_zero() {
    // Reserved band (bits 30..20) must be clear on a freshly-packed slot
    // so persist.cpp never accidentally trips a future schema bit.
    PresetSlot p = {1, 0xF, 0xFFFF};
    uint32_t raw = presetPack(p);
    TEST_ASSERT_EQUAL_UINT32(0u, raw & 0x7FF00000u);
}

// --- Round-trip (canonical band + freq combos) ------------------------------

static void test_roundtrip_fm_102_4() {
    // Band 0 = FM, freq 10240 = 102.40 MHz (10 kHz units).
    PresetSlot in = {1, 0, 10240};
    PresetSlot out = presetUnpack(presetPack(in));
    TEST_ASSERT_EQUAL_UINT8(1,     out.valid);
    TEST_ASSERT_EQUAL_UINT8(0,     out.band);
    TEST_ASSERT_EQUAL_UINT16(10240, out.freq);
}

static void test_roundtrip_mw_1530() {
    // Band 1 = MW, freq 1530 = 1530 kHz.
    PresetSlot in = {1, 1, 1530};
    PresetSlot out = presetUnpack(presetPack(in));
    TEST_ASSERT_EQUAL_UINT8(1,    out.valid);
    TEST_ASSERT_EQUAL_UINT8(1,    out.band);
    TEST_ASSERT_EQUAL_UINT16(1530, out.freq);
}

static void test_roundtrip_sw_9700() {
    // Band 3 = SW 31m, freq 9700 = 9700 kHz.
    PresetSlot in = {1, 3, 9700};
    PresetSlot out = presetUnpack(presetPack(in));
    TEST_ASSERT_EQUAL_UINT8(1,    out.valid);
    TEST_ASSERT_EQUAL_UINT8(3,    out.band);
    TEST_ASSERT_EQUAL_UINT16(9700, out.freq);
}

static void test_roundtrip_max_band_and_freq() {
    // Edge: 4-bit band = 15, full u16 freq.
    PresetSlot in = {1, 15, 0xFFFF};
    PresetSlot out = presetUnpack(presetPack(in));
    TEST_ASSERT_EQUAL_UINT8(1,       out.valid);
    TEST_ASSERT_EQUAL_UINT8(15,      out.band);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.freq);
}

// --- Overflow handling ------------------------------------------------------

static void test_band_above_15_wraps_silently() {
    // Band=17 has no business in the slot (g_bandCount is currently 4), but
    // the codec must mask rather than corrupt the valid bit or the freq.
    PresetSlot in = {1, 17, 1530};
    uint32_t raw = presetPack(in);
    TEST_ASSERT_TRUE((raw & 0x80000000u) != 0u);  // valid survives
    TEST_ASSERT_EQUAL_UINT16(1530, (uint16_t)(raw & 0xFFFFu));  // freq survives
    PresetSlot out = presetUnpack(raw);
    TEST_ASSERT_EQUAL_UINT8(17 & 0xF, out.band);  // masked to 4 bits
}

// --- Forward-compat: unknown reserved bits must be ignored on unpack --------

static void test_unpack_ignores_reserved_bits() {
    // Simulate a future schema that uses bits 30..20 for a name index — an
    // older build must still decode the band/freq correctly and drop the
    // unknown bits on the floor.
    uint32_t future = 0x80000000u | 0x7FF00000u | 0x00050000u | 0x00000ABC;
    PresetSlot out = presetUnpack(future);
    TEST_ASSERT_EQUAL_UINT8(1,    out.valid);
    TEST_ASSERT_EQUAL_UINT8(5,    out.band);
    TEST_ASSERT_EQUAL_UINT16(0xABC, out.freq);
}

// --- Test runner ------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_slot_packs_to_zero);
    RUN_TEST(test_zero_unpacks_to_empty);
    RUN_TEST(test_valid_slot_has_msb_set);
    RUN_TEST(test_band_lands_in_bits_19_16);
    RUN_TEST(test_freq_lands_in_low_16_bits);
    RUN_TEST(test_reserved_bits_are_zero);
    RUN_TEST(test_roundtrip_fm_102_4);
    RUN_TEST(test_roundtrip_mw_1530);
    RUN_TEST(test_roundtrip_sw_9700);
    RUN_TEST(test_roundtrip_max_band_and_freq);
    RUN_TEST(test_band_above_15_wraps_silently);
    RUN_TEST(test_unpack_ignores_reserved_bits);
    return UNITY_END();
}
