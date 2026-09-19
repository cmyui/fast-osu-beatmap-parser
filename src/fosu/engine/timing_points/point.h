#pragma once

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/parsing/sample_sets.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/engine/timing_points/beat_length.h>
#include <fosu/types.h>

#include <algorithm>
#include <bit>
#include <optional>

namespace fosu::internal {

// Parse a timing point, including omitted legacy fields. A present field
// must parse completely; NaN is meaningful only when inherited. Invalid
// lines return nullopt.
// UseCommaMask is for lines of at most 64 bytes, with a mask relative to p.
template <bool UseCommaMask = false>
inline std::optional<TimingPoint> parse_timing_point(const char* p,
                                                     const char* end,
                                                     u64         commas = 0,
                                                     i32 time_offset = 0) {
  [[maybe_unused]] const char* line = p;
  f64                          time, beat_length;
  const char*                  q = parse_osu_double(p, end, time);
  if (q == p || q >= end || *q != ',')
    return std::nullopt;
  p = q + 1;
  q = parse_beat_length(p, end, beat_length);
  if (q == p)
    return std::nullopt;
  p = q;
  i64 rest[6] = {4, 0, 0, 100, 1, 0};
  for (i32 i = 0; i < 6 && p < end; ++i) {
    if (*p++ != ',')
      return std::nullopt;
    const char* field_end;
    if constexpr (UseCommaMask) {
      const size_t offset = static_cast<size_t>(p - line);
      commas = offset < 64 ? commas & (~0ull << offset) : 0;
      field_end = commas ? line + std::countr_zero(commas) : end;
    } else {
      field_end = find_byte<','>(p, end);
    }
    if (p == field_end)
      return std::nullopt;
    if (i == 4)
      rest[i] = *p == '1';
    else if (i == 0 && *p == '0') {
      // The official decoder treats any meter field beginning in 0 as
      // 4/4. Preserve a plain raw zero; use 4 for nonnumeric spellings.
      q = parse_osu_int(p, field_end, rest[i]);
      if (q != field_end)
        rest[i] = 4;
    } else {
      q = parse_osu_int(p, field_end, rest[i]);
      if (q == p || q != field_end || (i == 0 && rest[i] <= 0))
        return std::nullopt;
    }
    p = field_end;
  }
  // Additional legacy columns are ignored by the official decoder.
  if ((p < end && *p != ',') || (rest[4] != 0 && std::isnan(beat_length)))
    return std::nullopt;
  const auto sample_set = parse_sample_set(rest[1]);
  if (!sample_set)
    return std::nullopt;
  return TimingPoint{
      .time = time + time_offset,
      .beat_length = beat_length,
      .meter = clamp_i32(rest[0]),
      .sample_set = *sample_set,
      .sample_index = clamp_i32(rest[2]),
      .volume = clamp_i32(rest[3]),
      .uninherited = rest[4] != 0,
      .effects = static_cast<u32>(rest[5]),
  };
}

#if FOSU_SIMD_X86
consteval auto make_timing_tail_masks() {
  std::array<std::array<u8, 16>, 6> masks{};
  for (i32 index = 1; index <= 2; ++index)
    for (i32 volume = 1; volume <= 3; ++volume) {
      auto& mask = masks[(index - 1) * 3 + volume - 1];
      mask.fill(0x80);
      mask[3] = 0;
      mask[7] = 2;
      for (i32 i = 0; i < index; ++i)
        mask[12 - index + i] = 4 + i;
      for (i32 i = 0; i < volume; ++i)
        mask[16 - volume + i] = 5 + index + i;
    }
  return masks;
}
inline constexpr auto kTimingTailMasks = make_timing_tail_masks();

// Already-validated "meter,set,index,volume": one digit each for meter/set,
// one or two for index, and one to three for volume. Return all four together.
inline std::array<i32, 4> decode_timing_tail(const char* p,
                                             u32         index_digits,
                                             u32         volume_digits) {
  const auto& mask =
      kTimingTailMasks[(index_digits - 1) * 3 + volume_digits - 1];
  const auto digits = _mm_shuffle_epi8(
      _mm_sub_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),
                   _mm_set1_epi8('0')),
      _mm_loadu_si128(reinterpret_cast<const __m128i*>(mask.data())));
  const auto values = _mm_madd_epi16(
      _mm_maddubs_epi16(digits, _mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 10, 1,
                                              10, 1, 10, 1, 10, 1)),
      _mm_setr_epi16(100, 1, 100, 1, 100, 1, 100, 1));
  std::array<i32, 4> fields;
  _mm_storeu_si128(reinterpret_cast<__m128i*>(fields.data()), values);
  return fields;
}
#endif

#if FOSU_SIMD
// Common eight-field timing rows: an unsigned integer timestamp, a signed
// decimal beat length, and small integer tail fields. Validate their spelling
// and widths before decoding. Wider values and legacy spellings use the
// general parser; these limits constrain only the fast path. A nullopt
// result requests general parsing rather than declaring the line malformed.
// Keep this inlined in the section loop so accepted records need no call.
FOSU_ALWAYS_INLINE std::optional<TimingPoint>
try_parse_timing_point_fast_masked(u64         commas,
                                   u64         nondig,
                                   const char* p,
                                   size_t      len,
                                   i32         time_offset = 0) {
  if (std::popcount(commas) != 7)
    return std::nullopt;

  // Seven comma positions -> eight fields.
  const u64  m1 = (commas & (commas - 1));
  const u64  m2 = (m1 & (m1 - 1));
  const u64  m3 = (m2 & (m2 - 1));
  const u64  m4 = (m3 & (m3 - 1));
  const u64  m5 = (m4 & (m4 - 1));
  const u64  m6 = (m5 & (m5 - 1));
  const auto time_end = static_cast<u32>(trailing_zeros(commas));
  const auto beat_length_end = static_cast<u32>(trailing_zeros(m1));
  const auto meter_end = static_cast<u32>(trailing_zeros(m2));
  const auto sample_set_end = static_cast<u32>(trailing_zeros(m3));
  const auto sample_index_end = static_cast<u32>(trailing_zeros(m4));
  const auto volume_end = static_cast<u32>(trailing_zeros(m5));
  const auto uninherited_end = static_cast<u32>(trailing_zeros(m6));

  const u32  meter_digits = meter_end - beat_length_end - 1;
  const u32  sample_set_digits = sample_set_end - meter_end - 1;
  const u32  sample_index_digits = sample_index_end - sample_set_end - 1;
  const u32  volume_digits = volume_end - sample_index_end - 1;
  const u32  uninherited_digits = uninherited_end - volume_end - 1;
  const u32  effects_digits = static_cast<u32>(len) - uninherited_end - 1;
  // Subtracting one makes zero-length fields fail this unsigned range check.
  if (time_end - 1 > 7 || ((meter_digits - 1) | (sample_set_digits - 1) |
                           (sample_index_digits - 1) | (volume_digits - 1) |
                           (uninherited_digits - 1) | (effects_digits - 1)) > 3)
    return std::nullopt;

  const char* magnitude = p + time_end + 1;
  const bool  negative = *magnitude == '-';
  magnitude += negative;
  const u32  magnitude_length = beat_length_end - time_end - 1 - negative;
  const u32  integer_digits = trailing_zeros(nondig >> (magnitude - p));
  const bool has_dot = integer_digits < magnitude_length;
  const u32  fraction_digits = magnitude_length - integer_digits - has_dot;
  if (integer_digits - 1 > 7 || fraction_digits > 13 ||
      integer_digits + fraction_digits > 18 ||
      (has_dot && magnitude[integer_digits] != '.') ||
      std::popcount(nondig) !=
          7 + static_cast<i32>(has_dot) + static_cast<i32>(negative))
    return std::nullopt;

  const auto parse_small_integer = [](const char* field, u32 digits) {
    if (digits == 1)
      return static_cast<i32>(static_cast<u8>(*field - '0'));
    return static_cast<i32>(swar_parse_u32(field, digits));
  };

#if FOSU_SIMD_X86
  const auto chunks = decode_decimal_chunks(magnitude, integer_digits,
                                            magnitude + integer_digits + 1,
                                            std::min(fraction_digits, 8u));
  u64        mantissa = chunks.integer;
#else
  u64 mantissa = swar_parse_u64(magnitude, integer_digits);
#endif
  if (fraction_digits) {
    const u32   first_digits = std::min(fraction_digits, 8u);
    const u32   second_digits = fraction_digits - first_digits;
    const char* fraction = magnitude + integer_digits + 1;
    mantissa = mantissa * kPow10u[first_digits] +
#if FOSU_SIMD_X86
               chunks.fraction;
#else
               swar_parse_u64(fraction, first_digits);
#endif
    if (second_digits)
      mantissa = mantissa * kPow10u[second_digits] +
                 swar_parse_u64(fraction + 8, second_digits);
  }
  f64 beat_length;
  if (mantissa > kMaxExactDoubleInteger) {
    // Preserve correct rounding when the integer mantissa is not exact.
    if (bounded_double(p + time_end + 1, p + beat_length_end, beat_length) !=
        p + beat_length_end)
      return std::nullopt;
  } else {
    beat_length = static_cast<f64>(mantissa);
    if (fraction_digits)
      beat_length /= kPow10[fraction_digits];
    if (negative)
      beat_length = -beat_length;
  }
  std::array<i32, 4> fields;
#if FOSU_SIMD_X86
  if (meter_digits == 1 && sample_set_digits == 1 && sample_index_digits <= 2 &&
      volume_digits <= 3) {
    fields = decode_timing_tail(p + beat_length_end + 1, sample_index_digits,
                                volume_digits);
  } else
#endif
  {
    fields = {
        parse_small_integer(p + beat_length_end + 1, meter_digits),
        parse_small_integer(p + meter_end + 1, sample_set_digits),
        parse_small_integer(p + sample_set_end + 1, sample_index_digits),
        parse_small_integer(p + sample_index_end + 1, volume_digits),
    };
  }
  const auto sample_set = parse_sample_set(fields[1]);
  if (!sample_set)
    return std::nullopt;
  return TimingPoint{
      .time = static_cast<f64>(swar_parse_u64(p, time_end)) + time_offset,
      .beat_length = beat_length,
      .meter = fields[0],
      .sample_set = *sample_set,
      .sample_index = fields[2],
      .volume = fields[3],
      .uninherited = p[volume_end + 1] == '1',
      .effects = static_cast<u32>(
          parse_small_integer(p + uninherited_end + 1, effects_digits)),
  };
}

// Compute masks from the input vectors before attempting fast parsing.
FOSU_ALWAYS_INLINE std::optional<TimingPoint>
try_parse_timing_point_fast(Bytes32 a, Bytes32 b, const char* p, size_t len) {
  if (len > 64 || len < 15)
    return std::nullopt;
  const u64 line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
  const u64 commas =
      (comma_mask32(a) | static_cast<u64>(comma_mask32(b)) << 32) & line_mask;
  const u64 nondig =
      (nondigit_mask32(a) | static_cast<u64>(nondigit_mask32(b)) << 32) &
      line_mask;
  return try_parse_timing_point_fast_masked(commas, nondig, p, len);
}

#endif

}  // namespace fosu::internal
