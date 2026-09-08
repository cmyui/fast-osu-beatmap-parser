#pragma once

#include <fosu/engine/simd.h>
#include <array>
#include <cstddef>

namespace fosu::internal {

#if FOSU_SIMD_X86
consteval auto make_digit_alignment() {
  std::array<std::array<uint8_t, 8>, 9> masks{};
  for (int digits = 0; digits <= 8; ++digits) {
    masks[digits].fill(0x80);
    for (int i = 0; i < digits; ++i)
      masks[digits][8 - digits + i] = i;
  }
  return masks;
}
inline constexpr auto kDigitAlignment = make_digit_alignment();

struct DecimalChunks {
  uint32_t integer;
  uint32_t fraction;
};

// Decode two already-validated digit runs in parallel. Each has at most eight
// digits; an absent fractional run decodes to zero. The caller supplies padded
// input and assembles these integer chunks into the full decimal mantissa.
inline DecimalChunks decode_decimal_chunks(const char* integer,
                                           uint32_t integer_digits,
                                           const char* fraction,
                                           uint32_t fraction_digits) {
  const auto text =
      _mm_unpacklo_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(integer)),
                         _mm_loadl_epi64(reinterpret_cast<const __m128i*>(fraction)));
  const auto integer_mask = _mm_loadl_epi64(
      reinterpret_cast<const __m128i*>(kDigitAlignment[integer_digits].data()));
  const auto fraction_mask = _mm_add_epi8(
      _mm_loadl_epi64(
          reinterpret_cast<const __m128i*>(kDigitAlignment[fraction_digits].data())),
      _mm_set1_epi8(8));
  const auto digits = _mm_shuffle_epi8(_mm_sub_epi8(text, _mm_set1_epi8('0')),
                                       _mm_unpacklo_epi64(integer_mask, fraction_mask));
  const auto groups = _mm_madd_epi16(
      _mm_maddubs_epi16(
          digits, _mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1)),
      _mm_setr_epi16(100, 1, 100, 1, 100, 1, 100, 1));
  const auto values =
      _mm_madd_epi16(_mm_packus_epi32(groups, groups),
                     _mm_setr_epi16(10000, 1, 10000, 1, 10000, 1, 10000, 1));
  DecimalChunks chunks;
  static_assert(sizeof(chunks) == 8 && offsetof(DecimalChunks, fraction) == 4);
  _mm_storel_epi64(reinterpret_cast<__m128i*>(&chunks), values);
  return chunks;
}
#endif

}  // namespace fosu::internal
