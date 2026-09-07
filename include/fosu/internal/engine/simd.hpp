#pragma once
#include <bit>
#include <cstdint>

#if defined(__AVX2__) && defined(__BMI__) && !defined(FOSU_DISABLE_SIMD)
#define FOSU_SIMD_X86 1
#include <immintrin.h>
#else
#define FOSU_SIMD_X86 0
#endif
#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(FOSU_DISABLE_SIMD)
#define FOSU_SIMD_NEON 1
#include <arm_neon.h>
#else
#define FOSU_SIMD_NEON 0
#endif
#define FOSU_SIMD (FOSU_SIMD_X86 || FOSU_SIMD_NEON)

namespace fosu::internal {
#if FOSU_SIMD
// Keep the native BMI operation on x86: GCC can add zero-input branches
// around std::countr_zero even when TZCNT already defines that result.
template <typename T>
inline T trailing_zeros(T v) {
#if FOSU_SIMD_X86
    if constexpr (sizeof(T) == 4) return _tzcnt_u32(v);
    else return _tzcnt_u64(v);
#else
    return static_cast<T>(std::countr_zero(v));
#endif
}
#endif
#if FOSU_SIMD_X86
using ByteVector = __m256i;
using Bytes32 = __m256i;
inline Bytes32 load32(const char* p) {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
}
inline ByteVector broadcast_byte(char c) { return _mm256_set1_epi8(c); }
inline uint32_t equal_mask32(Bytes32 v, ByteVector c) {
    return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, c)));
}
#elif FOSU_SIMD_NEON
using ByteVector = uint8x16_t;
using Bytes32 = uint8x16x2_t;
inline Bytes32 load32(const char* p) {
    return {{vld1q_u8(reinterpret_cast<const uint8_t*>(p)),
             vld1q_u8(reinterpret_cast<const uint8_t*>(p + 16))}};
}
inline ByteVector broadcast_byte(char c) { return vdupq_n_u8(static_cast<uint8_t>(c)); }
// Comparisons have all-zero or all-one lanes. Weight each true lane and
// pairwise-add until the first two bytes hold the masks of the two halves.
// One scalar extraction then gives the same byte-position mask as AVX2.
inline uint32_t byte_mask16(uint8x16_t v) {
    constexpr uint8_t weights[16] = {1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    const auto bits = vandq_u8(v, vld1q_u8(weights));
    const auto pairs = vpaddq_u8(bits, bits);
    const auto fours = vpaddq_u8(pairs, pairs);
    const auto eights = vpaddq_u8(fours, fours);
    return vgetq_lane_u16(vreinterpretq_u16_u8(eights), 0);
}
inline uint32_t equal_mask32(Bytes32 v, ByteVector c) {
    return byte_mask16(vceqq_u8(v.val[0], c)) |
           (byte_mask16(vceqq_u8(v.val[1], c)) << 16);
}
inline uint32_t nondigit_mask16(uint8x16_t v) {
    return byte_mask16(vcgtq_u8(vsubq_u8(v, vdupq_n_u8('0')), vdupq_n_u8(9)));
}
// Four right-aligned groups of four decimal digits, independently converted.
inline uint32x4_t decimal_groups(uint8x16_t digits) {
    constexpr uint8_t pairs[16] = {10,1,10,1,10,1,10,1,10,1,10,1,10,1,10,1};
    constexpr uint16_t words[8] = {100,1,100,1,100,1,100,1};
    return vpaddlq_u16(vmulq_u16(vpaddlq_u8(vmulq_u8(digits, vld1q_u8(pairs))),
                                vld1q_u16(words)));
}
#endif
}  // namespace fosu::internal
