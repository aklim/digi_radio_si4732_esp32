// ============================================================================
// preset_pack.h — Pure memory-preset <-> uint32_t codec (no Arduino deps).
//
// A memory preset is a user-saved station: a band index plus a frequency in
// the band's native units (see radio_bands.h). We persist one packed u32 per
// slot under NVS keys "preset0".."preset15", mirroring the existing per-band
// "freq<N>" convention in persist.cpp.
//
// Bit layout (MSB first):
//   31       : valid     (1 = slot holds a saved station, 0 = empty)
//   30..20   : reserved  (11 bits, must be 0 — future use for per-slot
//                         mode / BW / AGC / a short name index, without a
//                         schema-version bump)
//   19..16   : band      (4 bits, g_bands[] index; masks silently on
//                         overflow — callers should check g_bandCount)
//   15..0    : freq      (16 bits, band's native units; same meaning as
//                         the per-band "freq<N>" NVS key)
//
// Split out of persist.cpp so the codec can be exercised by native host
// unit tests without linking Arduino / Preferences / FreeRTOS. persist.cpp
// #includes this header and uses the inlines directly.
// ============================================================================

#ifndef PRESET_PACK_H
#define PRESET_PACK_H

#include <stdint.h>

// Public slot struct. Kept POD so it round-trips through memcpy / value
// semantics without surprises. `valid == 0` means "empty"; callers should
// ignore band / freq in that case.
struct PresetSlot {
    uint8_t  valid;
    uint8_t  band;
    uint16_t freq;
};

// Bit-field masks + shifts. Exposed as constants so tests can assert the
// layout without duplicating the magic numbers.
static constexpr uint32_t PRESET_VALID_BIT  = 0x80000000u;
static constexpr uint32_t PRESET_BAND_MASK  = 0x000F0000u;  // bits 19..16
static constexpr uint32_t PRESET_BAND_SHIFT = 16;
static constexpr uint32_t PRESET_FREQ_MASK  = 0x0000FFFFu;

// Pack a slot into its NVS u32 representation. An empty slot (valid == 0)
// always encodes to 0 regardless of band / freq, so a freshly-seeded "all
// zeros" NVS entry decodes to {0, 0, 0}. Band is masked to 4 bits — values
// >= 16 overflow silently; valid() callers should clamp against g_bandCount
// on the way in.
static inline uint32_t presetPack(PresetSlot p) {
    if (!p.valid) return 0;
    uint32_t raw = PRESET_VALID_BIT;
    raw |= ((uint32_t)p.band << PRESET_BAND_SHIFT) & PRESET_BAND_MASK;
    raw |= ((uint32_t)p.freq) & PRESET_FREQ_MASK;
    return raw;
}

// Unpack a raw NVS u32 into a slot. A zero input decodes to an empty slot.
// Reserved bits (30..20) are ignored — forward-compatible with future
// schema extensions that re-use them.
static inline PresetSlot presetUnpack(uint32_t raw) {
    PresetSlot p;
    p.valid = (raw & PRESET_VALID_BIT) ? 1 : 0;
    p.band  = (uint8_t)((raw & PRESET_BAND_MASK) >> PRESET_BAND_SHIFT);
    p.freq  = (uint16_t)(raw & PRESET_FREQ_MASK);
    return p;
}

#endif  // PRESET_PACK_H
