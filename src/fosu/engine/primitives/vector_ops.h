#pragma once
#include <fosu/types.h>

#if (defined(FOSU_COMPILE_AVX2) ||                                     \
     (!defined(_MSC_VER) && defined(__AVX2__) && defined(__BMI__))) && \
    !defined(FOSU_DISABLE_SIMD)
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

#if FOSU_SIMD_NEON
#include <bit>
#endif

namespace fosu::internal {
#if FOSU_SIMD
// Keep the native BMI operation on x86: GCC can add zero-input branches
// around std::countr_zero even when TZCNT already defines that result.
template <typename T>
inline T trailing_zeros(T v) {
#if FOSU_SIMD_X86
  if constexpr (sizeof(T) == 4)
    return _tzcnt_u32(v);
  else
    return _tzcnt_u64(v);
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
template <u8 Value>
inline ByteVector broadcast_byte() {
#if defined(_MSC_VER)
  return _mm256_set1_epi8(static_cast<char>(Value));
#else
  // A memory broadcast prevents GCC rebuilding constants through a general
  // register inside loops containing calls. Keep that detail out of parsers.
  static constexpr u8 value = Value;
  ByteVector          result;
  __asm__("vpbroadcastb %1, %0" : "=x"(result) : "m"(value));
  return result;
#endif
}
inline u32 equal_mask32(Bytes32 v, ByteVector c) {
  return static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, c)));
}
inline u32 line_end_mask32(Bytes32 v) {
  const auto cr = _mm256_cmpeq_epi8(v, broadcast_byte<'\r'>());
  const auto lf = _mm256_cmpeq_epi8(v, broadcast_byte<'\n'>());
  return static_cast<u32>(_mm256_movemask_epi8(_mm256_or_si256(cr, lf)));
}
// AVX2 has no unsigned byte comparison: adding 80 maps ASCII digits to
// [-128, -119], below every other byte under a signed comparison.
inline u32 nondigit_mask32(Bytes32 v) {
  return static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpgt_epi8(
      _mm256_add_epi8(v, broadcast_byte<80>()), broadcast_byte<137>())));
}
#elif FOSU_SIMD_NEON
using ByteVector = uint8x16_t;
using Bytes32 = uint8x16x2_t;
inline Bytes32 load32(const char* p) {
  return {{vld1q_u8(reinterpret_cast<const u8*>(p)),
           vld1q_u8(reinterpret_cast<const u8*>(p + 16))}};
}
template <u8 Value>
inline ByteVector broadcast_byte() {
  return vdupq_n_u8(Value);
}
// Comparisons have all-zero or all-one lanes. Weight each true lane by its bit
// and pairwise-add until each byte holds eight lanes' bits. Both halves share
// one reduction, so a 32-byte mask costs one extraction, as with AVX2.
inline u32 byte_mask32(uint8x16_t low, uint8x16_t high) {
  constexpr u8 weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                              1, 2, 4, 8, 16, 32, 64, 128};
  const auto   w = vld1q_u8(weights);
  const auto   pairs = vpaddq_u8(vandq_u8(low, w), vandq_u8(high, w));
  const auto   fours = vpaddq_u8(pairs, pairs);
  const auto   eights = vpaddq_u8(fours, fours);
  return vgetq_lane_u32(vreinterpretq_u32_u8(eights), 0);
}
inline u32 equal_mask32(Bytes32 v, ByteVector c) {
  return byte_mask32(vceqq_u8(v.val[0], c), vceqq_u8(v.val[1], c));
}
inline u32 line_end_mask32(Bytes32 v) {
  const auto cr = broadcast_byte<'\r'>();
  const auto lf = broadcast_byte<'\n'>();
  return byte_mask32(vorrq_u8(vceqq_u8(v.val[0], cr), vceqq_u8(v.val[0], lf)),
                     vorrq_u8(vceqq_u8(v.val[1], cr), vceqq_u8(v.val[1], lf)));
}
inline uint8x16_t nondigit_bytes16(uint8x16_t v) {
  return vcgtq_u8(vsubq_u8(v, vdupq_n_u8('0')), vdupq_n_u8(9));
}
inline u32 nondigit_mask32(Bytes32 v) {
  return byte_mask32(nondigit_bytes16(v.val[0]), nondigit_bytes16(v.val[1]));
}
#endif
#if FOSU_SIMD
inline u32 comma_mask32(Bytes32 bytes) {
  return equal_mask32(bytes, broadcast_byte<','>());
}
#endif

}  // namespace fosu::internal
