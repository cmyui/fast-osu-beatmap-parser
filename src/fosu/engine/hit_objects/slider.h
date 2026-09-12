#pragma once

// Parses the slider-specific portion of one hit object and publishes its
// Slider, control points and curve segments into the Beatmap. Section
// iteration and common hit-object fields belong to sections/hit_objects.h.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/hit_objects/common_fields.h>
#include <fosu/engine/hit_objects/samples.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/enums.h>

namespace fosu::internal {

inline std::optional<CurveType> parse_curve_type(char value) {
  switch (value) {
    case 'B':
      return CurveType::Bezier;
    case 'C':
      return CurveType::Catmull;
    case 'L':
      return CurveType::Linear;
    case 'P':
      return CurveType::PerfectCurve;
    default:
      return std::nullopt;
  }
}

// SIMD point decoding is an implementation detail of parsing the point list.
// The table is indexed by the digit widths of x and y.
#if FOSU_SIMD
struct alignas(16) SliderPointShuffle {
  int8_t bytes[16];
};

consteval std::array<SliderPointShuffle, 16> make_slider_point_shuffles() {
  std::array<SliderPointShuffle, 16> shuffles{};
  for (int x_digits = 1; x_digits <= 4; ++x_digits) {
    for (int y_digits = 1; y_digits <= 4; ++y_digits) {
      auto& shuffle = shuffles[(x_digits - 1) * 4 + y_digits - 1];
      for (auto& byte : shuffle.bytes)
        byte = static_cast<int8_t>(0x80);
      for (int i = 0; i < x_digits; ++i)
        shuffle.bytes[4 - x_digits + i] = static_cast<int8_t>(1 + i);
      for (int i = 0; i < y_digits; ++i)
        shuffle.bytes[8 - y_digits + i] = static_cast<int8_t>(x_digits + 2 + i);
    }
  }
  return shuffles;
}

inline constexpr auto kSliderPointShuffles = make_slider_point_shuffles();

#if FOSU_SIMD_X86
inline SliderPoint decode_slider_point(__m128i input,
                                       uint32_t x_digits,
                                       uint32_t y_digits,
                                       const HitObjectParseConstants& constants) {
  static_assert(sizeof(SliderPoint) == 8 && offsetof(SliderPoint, x) == 0 &&
                offsetof(SliderPoint, y) == 4);
  const auto& shuffle = kSliderPointShuffles[(x_digits - 1) * 4 + y_digits - 1];
  const __m128i placed =
      _mm_shuffle_epi8(_mm_sub_epi8(input, _mm256_castsi256_si128(constants.zero)),
                       _mm_load_si128(reinterpret_cast<const __m128i*>(shuffle.bytes)));
  const __m128i coordinates = _mm_madd_epi16(
      _mm_maddubs_epi16(placed, constants.pair_weights), constants.word_weights);
  const __m128 positions = _mm_cvtepi32_ps(coordinates);
  return std::bit_cast<SliderPoint>(
      static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_castps_si128(positions))));
}
#else
inline SliderPoint decode_slider_point(uint8x16_t input,
                                       uint32_t x_digits,
                                       uint32_t y_digits,
                                       const HitObjectParseConstants& constants) {
  static_assert(sizeof(SliderPoint) == 8 && offsetof(SliderPoint, x) == 0 &&
                offsetof(SliderPoint, y) == 4);
  const auto& shuffle = kSliderPointShuffles[(x_digits - 1) * 4 + y_digits - 1];
  const auto coordinates = decimal_groups(
      vqtbl1q_u8(vsubq_u8(input, constants.zero),
                 vld1q_u8(reinterpret_cast<const uint8_t*>(shuffle.bytes))));
  const auto positions = vcvtq_f32_u32(coordinates);
  return std::bit_cast<SliderPoint>(vgetq_lane_u64(vreinterpretq_u64_f32(positions), 0));
}
#endif
#endif

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
//
// This is one parse transaction. Control points and completed lazer segments
// are written into their arena arrays as they are accepted. Failures while
// parsing the path roll those arrays back. A malformed later field may leave
// parsed points unused, matching the official decoder's behavior.
FOSU_NOINLINE inline bool parse_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject& object,
    const char* p,
    const char* end,
    [[maybe_unused]] const HitObjectParseConstants& constants) {
  if (p >= end)
    return false;

  const bool lazer = beatmap.format_version >= 128;
  const size_t slider_point_begin = slider_point_count;
  const size_t slider_segment_begin = slider_segment_count;

  const auto first_curve_type = parse_curve_type(*p++);
  if (!first_curve_type)
    return false;

  std::optional<uint32_t> first_curve_degree;
  if (*first_curve_type == CurveType::Bezier && p < end && is_digit(*p)) {
    int64_t degree;
    const char* next = parse_osu_int(p, end, degree);
    if (next == p || degree <= 0 || degree > UINT32_MAX)
      return false;
    first_curve_degree = static_cast<uint32_t>(degree);
    p = next;
  }

  CurveType current_curve_type = *first_curve_type;
  std::optional<uint32_t> current_curve_degree = first_curve_degree;
  size_t segment_point_begin = slider_point_count;
  bool has_explicit_segments = false;

#if FOSU_SIMD
  // Most sliders have only one or two ordinary points. Decode both from one
  // 32-byte window when their delimiter shape permits it.
  {
    const Bytes32 input = load32(p);
    const uint32_t non_digits = nondigit_mask32(input);
    const uint32_t colons = equal_mask32(input, constants.colon);
    const uint32_t pipes = equal_mask32(input, constants.pipe);
#if FOSU_SIMD_X86
    const uint32_t separators = static_cast<uint32_t>(
        _mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(input, constants.pipe),
                                             _mm256_cmpeq_epi8(input, constants.comma))));
#else
    const uint32_t separators =
        byte_mask16(vorrq_u8(vceqq_u8(input.val[0], constants.pipe),
                             vceqq_u8(input.val[0], constants.comma))) |
        (byte_mask16(vorrq_u8(vceqq_u8(input.val[1], constants.pipe),
                              vceqq_u8(input.val[1], constants.comma)))
         << 16);
#endif
    uint32_t boundaries = non_digits & ~1u;
    const uint32_t first_colon = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t first_end = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t second_colon = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t second_end = trailing_zeros(boundaries);

    const uint32_t first_x_digits = first_colon - 1;
    const uint32_t first_y_digits = first_end - first_colon - 1;
    const bool first_point_is_simple =
        (pipes & 1) & (((first_x_digits - 1) | (first_y_digits - 1)) <= 3) &
        ((static_cast<uint64_t>(colons) >> first_colon) & 1) &
        ((static_cast<uint64_t>(separators) >> first_end) & 1);

    if (first_point_is_simple) {
#if FOSU_SIMD_X86
      beatmap.slider_points[slider_point_count++] = decode_slider_point(
          _mm256_castsi256_si128(input), first_x_digits, first_y_digits, constants);
#else
      beatmap.slider_points[slider_point_count++] =
          decode_slider_point(input.val[0], first_x_digits, first_y_digits, constants);
#endif
      p += first_end;

      const uint32_t second_x_digits = second_colon - first_end - 1;
      const uint32_t second_y_digits = second_end - second_colon - 1;
      const bool second_point_is_simple =
          ((static_cast<uint64_t>(pipes) >> first_end) & 1) &
          (((second_x_digits - 1) | (second_y_digits - 1)) <= 3) &
          ((static_cast<uint64_t>(colons) >> second_colon) & 1) &
          ((static_cast<uint64_t>(separators) >> second_end) & 1);
      if (second_point_is_simple) {
#if FOSU_SIMD_X86
        beatmap.slider_points[slider_point_count++] =
            decode_slider_point(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),
                                second_x_digits, second_y_digits, constants);
#else
        beatmap.slider_points[slider_point_count++] =
            decode_slider_point(vld1q_u8(reinterpret_cast<const uint8_t*>(p)),
                                second_x_digits, second_y_digits, constants);
#endif
        p += second_end - first_end;
      }
    }
  }
#endif

  while (p < end && *p == '|') {
    const bool starts_segment =
        lazer && p + 1 < end &&
        (p[1] == 'B' || p[1] == 'C' || p[1] == 'L' || p[1] == 'P');

    CurveType next_curve_type = current_curve_type;
    std::optional<uint32_t> next_curve_degree = current_curve_degree;
    if (starts_segment) {
      ++p;
      const auto type = parse_curve_type(*p++);
      if (!type) {
        slider_point_count = slider_point_begin;
        slider_segment_count = slider_segment_begin;
        return false;
      }
      next_curve_type = *type;
      next_curve_degree.reset();
      if (next_curve_type == CurveType::Bezier && p < end && is_digit(*p)) {
        int64_t degree;
        const char* next = parse_osu_int(p, end, degree);
        if (next == p || degree <= 0 || degree > UINT32_MAX) {
          slider_point_count = slider_point_begin;
          slider_segment_count = slider_segment_begin;
          return false;
        }
        next_curve_degree = static_cast<uint32_t>(degree);
        p = next;
      }
    }

    if (p >= end || *p != '|') {
      slider_point_count = slider_point_begin;
      slider_segment_count = slider_segment_begin;
      return false;
    }

    SliderPoint point;
    const char* point_end = nullptr;

#if FOSU_SIMD
#if FOSU_SIMD_X86
    const __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    const uint32_t non_digits = nondigit_mask16(input);
    const uint32_t colons = static_cast<uint32_t>(_mm_movemask_epi8(
        _mm_cmpeq_epi8(input, _mm256_castsi256_si128(constants.colon))));
#else
    const auto input = vld1q_u8(reinterpret_cast<const uint8_t*>(p));
    const uint32_t non_digits = nondigit_mask16(input);
    const uint32_t colons = byte_mask16(vceqq_u8(input, constants.colon));
#endif
    const uint32_t x_digits = trailing_zeros(non_digits >> 1);
    const uint32_t colon = x_digits & 7;
    const uint32_t y_digits = trailing_zeros(non_digits >> (2 + colon));
    if (((x_digits - 1) | (y_digits - 1)) <= 3 && ((colons >> (1 + colon)) & 1)) {
      const char* next = p + 2 + x_digits + y_digits;
      if (*next == '|' || *next == ',') {
        point = decode_slider_point(input, x_digits, y_digits, constants);
        point_end = next;
      }
    }
#endif

    if (!point_end) {
      const char* coordinate = p + 1;
      float x;
      uint32_t digits = digit_run8(coordinate);
      if (digits - 1 <= 3 && digits <= static_cast<size_t>(end - coordinate) &&
          coordinate[digits] == ':') {
        x = static_cast<float>(swar_parse_u32(coordinate, digits));
        coordinate += digits;
      } else {
        const char* next = parse_osu_float(coordinate, end, x, 131072);
        if (next == coordinate || next >= end || *next != ':') {
          slider_point_count = slider_point_begin;
          slider_segment_count = slider_segment_begin;
          return false;
        }
        if (!lazer)
          x = static_cast<float>(static_cast<int32_t>(x));
        coordinate = next;
      }

      ++coordinate;
      float y;
      digits = digit_run8(coordinate);
      if (digits - 1 <= 3 && digits <= static_cast<size_t>(end - coordinate) &&
          (coordinate[digits] == ':' || coordinate[digits] == '|' ||
           coordinate[digits] == ',')) {
        y = static_cast<float>(swar_parse_u32(coordinate, digits));
        coordinate += digits;
      } else {
        const char* next = parse_osu_float(coordinate, end, y, 131072);
        if (next == coordinate) {
          slider_point_count = slider_point_begin;
          slider_segment_count = slider_segment_begin;
          return false;
        }
        if (!lazer)
          y = static_cast<float>(static_cast<int32_t>(y));
        coordinate = next;
      }

      point = {x, y};
      point_end = coordinate;
    }

    beatmap.slider_points[slider_point_count++] = point;
    p = point_end;

    if (starts_segment) {
      if (slider_segment_count == beatmap.slider_segments.size()) {
        slider_point_count = slider_point_begin;
        slider_segment_count = slider_segment_begin;
        return false;
      }
      beatmap.slider_segments[slider_segment_count++] = {
          .type = current_curve_type,
          .degree = current_curve_degree,
          .point_begin = static_cast<uint32_t>(segment_point_begin - slider_point_begin),
          .point_count = static_cast<uint32_t>(slider_point_count - segment_point_begin),
      };
      has_explicit_segments = true;
      segment_point_begin = slider_point_count - 1;
      current_curve_type = next_curve_type;
      current_curve_degree = next_curve_degree;
    }
  }

  // Everything after the point list is positional.
  if (p >= end || *p != ',') {
    slider_segment_count = slider_segment_begin;
    return false;
  }
  ++p;

  int32_t slides;
  const uint32_t first_slide_digit = static_cast<uint8_t>(p[0] - '0');
  const uint32_t second_slide_digit = static_cast<uint8_t>(p[1] - '0');
  if (first_slide_digit <= 9 && p[1] == ',') {
    slides = static_cast<int32_t>(first_slide_digit);
    ++p;
  } else if (first_slide_digit <= 9 && second_slide_digit <= 9 && p[2] == ',') {
    slides = static_cast<int32_t>(first_slide_digit * 10 + second_slide_digit);
    p += 2;
  } else {
    const uint32_t digits = digit_run8(p);
    if (digits - 1 <= 6) {
      slides = static_cast<int32_t>(swar_parse_u64(p, digits));
      p = skip_numeric_space(p + digits, end);
    } else {
      int64_t parsed_slides;
      const char* next = parse_osu_int(p, end, parsed_slides);
      if (next == p) {
        slider_segment_count = slider_segment_begin;
        return false;
      }
      slides = clamp_i32(parsed_slides);
      p = next;
    }
  }
  if (slides > 9000 || (p < end && *p != ',')) {
    slider_segment_count = slider_segment_begin;
    return false;
  }

  double length = 0;
  if (p < end) {
    const char* length_begin = p + 1;
    const char* next = nullptr;

#if FOSU_SIMD
    const Bytes32 input = load32(length_begin);
    const uint64_t non_digits = nondigit_mask32(input);
    const uint32_t integer_digits = static_cast<uint32_t>(trailing_zeros(non_digits));
    if (integer_digits - 1 <= 7) {
      const bool has_dot = length_begin[integer_digits] == '.';
      const uint32_t fraction_digits =
          has_dot
              ? static_cast<uint32_t>(trailing_zeros(non_digits >> (integer_digits + 1)))
              : 0;
      if (fraction_digits <= 13 && integer_digits + fraction_digits <= 18) {
        const uint32_t first_fraction_digits = std::min(fraction_digits, uint32_t(8));
        const uint32_t second_fraction_digits = fraction_digits - first_fraction_digits;
        const char* fraction = length_begin + integer_digits + 1;
#if FOSU_SIMD_X86
        const auto chunks = decode_decimal_chunks(length_begin, integer_digits, fraction,
                                                  first_fraction_digits);
        uint64_t mantissa = chunks.integer;
        if (first_fraction_digits) {
          mantissa = mantissa * kPow10u[first_fraction_digits] + chunks.fraction;
        }
#else
        uint64_t mantissa = swar_parse_u64(length_begin, integer_digits);
        if (first_fraction_digits) {
          mantissa = mantissa * kPow10u[first_fraction_digits] +
                     swar_parse_u64(fraction, first_fraction_digits);
        }
#endif
        if (second_fraction_digits) {
          mantissa = mantissa * kPow10u[second_fraction_digits] +
                     swar_parse_u64(fraction + 8, second_fraction_digits);
        }
        if (mantissa <= kMaxExactDoubleInteger) {
          double parsed_length = static_cast<double>(mantissa);
          if (fraction_digits)
            parsed_length /= kPow10[fraction_digits];
          const char* parsed_end =
              has_dot ? fraction + fraction_digits : length_begin + integer_digits;
          if (parsed_length <= 131072 && *parsed_end != 'e' && *parsed_end != 'E') {
            length = parsed_length;
            next = parsed_end;
          }
        }
      }
    }
#endif

    if (!next)
      next = parse_osu_double(length_begin, end, length, 131072);
    if (next != length_begin)
      next = skip_numeric_space(next, end);
    if (next == length_begin || (next < end && *next != ',')) {
      slider_segment_count = slider_segment_begin;
      return false;
    }
    p = next;
  }

  std::string_view sound_fields[3];
  if (p < end) {
    const char* sound_begin = p + 1;
#if FOSU_SIMD
    const size_t span = static_cast<size_t>(end - sound_begin);
    if (span <= 32) {
      const uint32_t commas = equal_mask32(load32(sound_begin), constants.comma) &
                              static_cast<uint32_t>((1ull << span) - 1);
      const uint32_t first = trailing_zeros(commas);
      const uint32_t remaining = commas & (commas - 1);
      const uint32_t second = trailing_zeros(remaining);
      const uint32_t third = trailing_zeros(remaining & (remaining - 1));
      if (first >= span) {
        sound_fields[0] = {sound_begin, span};
      } else if (second >= span) {
        sound_fields[0] = {sound_begin, first};
        sound_fields[1] = {sound_begin + first + 1, span - first - 1};
      } else {
        const uint32_t sample_end = third < span ? third : static_cast<uint32_t>(span);
        sound_fields[0] = {sound_begin, first};
        sound_fields[1] = {sound_begin + first + 1, second - first - 1};
        sound_fields[2] = {sound_begin + second + 1, sample_end - second - 1};
      }
    } else {
      size_t field = 0;
      const char* field_begin = sound_begin;
      while (sound_begin < end) {
        const size_t remaining = static_cast<size_t>(end - sound_begin);
        uint32_t commas = equal_mask32(load32(sound_begin), constants.comma);
        if (remaining < 32)
          commas &= (1u << remaining) - 1;
        while (commas) {
          const char* comma = sound_begin + trailing_zeros(commas);
          sound_fields[field++] = {field_begin, static_cast<size_t>(comma - field_begin)};
          if (field == 3)
            break;
          field_begin = comma + 1;
          commas &= commas - 1;
        }
        if (field == 3)
          break;
        sound_begin += std::min(remaining, size_t(32));
      }
      if (field < 3) {
        sound_fields[field] = {field_begin, static_cast<size_t>(end - field_begin)};
      }
    }
#else
    for (auto& field : sound_fields) {
      const auto* comma =
          static_cast<const char*>(memchr(sound_begin, ',', end - sound_begin));
      const char* field_end = comma ? comma : end;
      field = {sound_begin, static_cast<size_t>(field_end - sound_begin)};
      if (!comma)
        break;
      sound_begin = comma + 1;
    }
#endif
  }

  const std::string_view edge_sounds = sound_fields[0];
  const std::string_view edge_sets = sound_fields[1];
  const std::string_view hit_sample = sound_fields[2];
  if (!valid_sample(hit_sample, true) || !valid_edge_sets(edge_sets, slides)) {
    slider_segment_count = slider_segment_begin;
    return false;
  }

  if (lazer && (has_explicit_segments || first_curve_degree)) {
    if (slider_segment_count == beatmap.slider_segments.size()) {
      slider_point_count = slider_point_begin;
      slider_segment_count = slider_segment_begin;
      return false;
    }
    beatmap.slider_segments[slider_segment_count++] = {
        .type = current_curve_type,
        .degree = current_curve_degree,
        .point_begin = static_cast<uint32_t>(segment_point_begin - slider_point_begin),
        .point_count = static_cast<uint32_t>(slider_point_count - segment_point_begin),
    };
  }

  beatmap.sliders[slider_count] = {
      .point_begin = static_cast<uint32_t>(slider_point_begin),
      .point_count = static_cast<uint32_t>(slider_point_count - slider_point_begin),
      .segment_begin = static_cast<uint32_t>(slider_segment_begin),
      .segment_count =
          lazer ? static_cast<uint32_t>(slider_segment_count - slider_segment_begin) : 0,
      .slides = std::max(1, slides),
      .curve_type = *first_curve_type,
      .length = std::max(0.0, length),
      .edge_sounds = edge_sounds,
      .edge_sets = edge_sets,
  };
  object.hit_sample = hit_sample;
  object.slider = static_cast<uint32_t>(slider_count++);
  return true;
}
}  // namespace fosu::internal
