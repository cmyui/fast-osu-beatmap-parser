#pragma once

// Branchless SWAR digit-run detection and conversion, used for slider
// control points and decimal parsing. Portable (plain integer ops, works
// on ARM too); assumes a little-endian target.
//
// All helpers load 8 bytes at `p` unconditionally, so they inherit the
// parser-wide contract: kBufferPadding readable zero bytes past the end
// of the input buffer.

#include <cstdint>
#include <cstring>

namespace fosu::detail {

inline uint32_t load_u32_le(const char* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

inline uint64_t load_u64_le(const char* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

// Number of leading ASCII digits in the next 8 bytes (0..8). A byte is a
// digit iff its high nibble is 3 and adding 6 doesn't change that (which
// rules out ':'..'?'). The +6 carry can only corrupt classification of
// bytes *after* a non-digit byte, which tzcnt never reaches.
inline uint32_t digit_run8(const char* p) {
    const uint64_t chunk = load_u64_le(p);
    constexpr uint64_t kHi = 0xF0F0F0F0F0F0F0F0ull;
    constexpr uint64_t kThrees = 0x3030303030303030ull;
    const uint64_t nondigit = (((chunk & kHi) ^ kThrees) |
                               (((chunk + 0x0606060606060606ull) & kHi) ^ kThrees));
    if (nondigit == 0) return 8;
    return static_cast<uint32_t>(__builtin_ctzll(nondigit)) >> 3;
}

// Convert `len` (1..4) leading digits at `p`. Digits are left-shifted so
// the value gains leading zeros, then two multiply steps combine pairs:
// bytes -> "tens", 16-bit lanes -> full value.
inline uint32_t swar_parse_u32(const char* p, uint32_t len) {
    uint32_t c = load_u32_le(p) & 0x0F0F0F0F;
    c <<= 8 * (4 - len);
    c = (c * 2561u) >> 8;              //   10*256 + 1
    return ((c & 0x00FF00FF) * 6553601u) >> 16;  // 100*65536 + 1
}

// swar_parse_u64 with the shift made defined for ANY len (result is
// garbage outside 1..8; speculative callers discard it via a validity
// predicate). The &63 matches shlx hardware masking and costs nothing.
inline uint64_t swar_parse_u64_safe(const char* p, uint32_t len) {
    uint64_t c = load_u64_le(p) & 0x0F0F0F0F0F0F0F0Full;
    c <<= (8 * (8 - len)) & 63;
    c = (c * 2561ull) >> 8;
    c = ((c & 0x00FF00FF00FF00FFull) * 6553601ull) >> 16;
    return ((c & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;
}

// Convert `len` (1..8) leading digits at `p`.
inline uint64_t swar_parse_u64(const char* p, uint32_t len) {
    uint64_t c = load_u64_le(p) & 0x0F0F0F0F0F0F0F0Full;
    c <<= 8 * (8 - len);
    c = (c * 2561ull) >> 8;
    c = ((c & 0x00FF00FF00FF00FFull) * 6553601ull) >> 16;
    return ((c & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;  // 1e4*2^32 + 1
}

}  // namespace fosu::detail
