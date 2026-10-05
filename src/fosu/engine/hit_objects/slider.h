#pragma once

// Parses the slider-specific portion of one hit object and publishes its
// Slider, control points and curve segments into the Beatmap. Section
// iteration and common hit-object fields belong to sections/hit_objects.h.

#include "fosu/engine/primitives/byte_scan.h"

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/hit_objects/common_fields.h>
#include <fosu/engine/hit_objects/samples.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/engine/primitives/packed_digits.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/enums.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace fosu::internal {

struct HitObjectCounts {
  size_t      objects = 0;
  size_t      sliders = 0;
  size_t      slider_segments = 0;
  size_t      slider_points = 0;
  // Whether osu!'s previous object, in source order, is a spinner. It can be
  // a rejected line: see preceding_spinner_after_rejection.
  bool        preceding_was_spinner = false;
  // Whether the last rejected line broke only a rule fosu adds; osu! itself
  // reads such a line.
  bool        policy_rejection = false;
  // The first line that makes osu!stable refuse the map, if any.
  const char* unloadable_line = nullptr;
};

// Lazer bounds coordinates and slider lengths by ±131072. stable reads any
// finite length, and fosu keeps its coordinates where the int32 truncation
// of a control point stays defined.
template <Client C>
inline constexpr f32 kCoordinateLimit =
    C == Client::Lazer ? 131072.0f : 2147483520.0f;
template <Client C>
inline constexpr f64 kSliderLengthLimit =
    C == Client::Lazer ? 131072.0 : std::numeric_limits<f64>::max();

inline std::optional<CurveType> parse_curve_type(char value) {
  switch (value) {
    case 'B':
    case 'C':
    case 'L':
    case 'P':
      return static_cast<CurveType>(value);
    default:
      return std::nullopt;
  }
}

// SIMD point decoding is an implementation detail of parsing the point list.
// The table is indexed by the digit widths of x and y.
#if FOSU_SIMD
struct alignas(16) SliderPointShuffle {
  i8 bytes[16];
};

consteval std::array<SliderPointShuffle, 16> make_slider_point_shuffles() {
  std::array<SliderPointShuffle, 16> shuffles{};
  for (i32 x_digits = 1; x_digits <= 4; ++x_digits) {
    for (i32 y_digits = 1; y_digits <= 4; ++y_digits) {
      auto& shuffle = shuffles[(x_digits - 1) * 4 + y_digits - 1];
      for (auto& byte : shuffle.bytes)
        byte = static_cast<i8>(0x80);
      for (i32 i = 0; i < x_digits; ++i)
        shuffle.bytes[4 - x_digits + i] = static_cast<i8>(1 + i);
      for (i32 i = 0; i < y_digits; ++i)
        shuffle.bytes[8 - y_digits + i] = static_cast<i8>(x_digits + 2 + i);
    }
  }
  return shuffles;
}

inline constexpr auto kSliderPointShuffles = make_slider_point_shuffles();

#if FOSU_SIMD_X86
inline __m128i load16(const char* p) {
  return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
}

inline SliderPoint decode_slider_point(
    __m128i                        input,
    u32                            x_digits,
    u32                            y_digits,
    const HitObjectParseConstants& constants) {
  static_assert(sizeof(SliderPoint) == 8 && offsetof(SliderPoint, x) == 0 &&
                offsetof(SliderPoint, y) == 4);
  const auto& shuffle = kSliderPointShuffles[(x_digits - 1) * 4 + y_digits - 1];
  const __m128i placed = _mm_shuffle_epi8(
      _mm_sub_epi8(input, _mm256_castsi256_si128(constants.zero)),
      _mm_load_si128(reinterpret_cast<const __m128i*>(shuffle.bytes)));
  const __m128i coordinates =
      _mm_madd_epi16(_mm_maddubs_epi16(placed, constants.pair_weights),
                     constants.word_weights);
  const __m128 positions = _mm_cvtepi32_ps(coordinates);
  return std::bit_cast<SliderPoint>(
      static_cast<u64>(_mm_cvtsi128_si64(_mm_castps_si128(positions))));
}
#else
inline uint8x16_t load16(const char* p) {
  return vld1q_u8(reinterpret_cast<const u8*>(p));
}

inline SliderPoint decode_slider_point(
    uint8x16_t                     input,
    u32                            x_digits,
    u32                            y_digits,
    const HitObjectParseConstants& constants) {
  static_assert(sizeof(SliderPoint) == 8 && offsetof(SliderPoint, x) == 0 &&
                offsetof(SliderPoint, y) == 4);
  const auto& shuffle = kSliderPointShuffles[(x_digits - 1) * 4 + y_digits - 1];
  const auto  coordinates = decimal_groups(
      vqtbl1q_u8(vsubq_u8(input, constants.zero),
                 vld1q_u8(reinterpret_cast<const u8*>(shuffle.bytes))));
  const auto positions = vcvtq_f32_u32(coordinates);
  return std::bit_cast<SliderPoint>(
      vgetq_lane_u64(vreinterpretq_u64_f32(positions), 0));
}
#endif
#endif

struct ParsedSliderPoint {
  SliderPoint point;
  const char* end;
};

template <bool LazerFormat, Client C>
FOSU_ALWAYS_INLINE std::optional<ParsedSliderPoint> parse_slider_point(
    const char* p,
    const char* end) {
  const char* coordinate = p + 1;
  f32         x;
  u32         digits = digit_run8(coordinate);
  if (digits - 1 <= 3 && digits <= static_cast<size_t>(end - coordinate) &&
      coordinate[digits] == ':') {
    x = static_cast<f32>(swar_parse_u32(coordinate, digits));
    coordinate += digits;
  } else {
    const char* next = parse_osu_float(coordinate, end, x, kCoordinateLimit<C>);
    if (next == coordinate || next >= end || *next != ':')
      return std::nullopt;
    if constexpr (!LazerFormat)
      x = static_cast<f32>(static_cast<i32>(x));
    coordinate = next;
  }

  ++coordinate;
  f32 y;
  digits = digit_run8(coordinate);
  if (digits - 1 <= 3 && digits <= static_cast<size_t>(end - coordinate) &&
      (coordinate[digits] == ':' || coordinate[digits] == '|' ||
       coordinate[digits] == ',')) {
    y = static_cast<f32>(swar_parse_u32(coordinate, digits));
    coordinate += digits;
  } else {
    const char* next = parse_osu_float(coordinate, end, y, kCoordinateLimit<C>);
    if (next == coordinate)
      return std::nullopt;
    if constexpr (!LazerFormat)
      y = static_cast<f32>(static_cast<i32>(y));
    coordinate = next;
  }

  // osu! reads only the first two ':'-separated values of a point.
  if (coordinate < end && *coordinate == ':') [[unlikely]] {
    while (coordinate < end && *coordinate != '|' && *coordinate != ',')
      ++coordinate;
  }
  return ParsedSliderPoint{.point = {x, y}, .end = coordinate};
}

#if FOSU_SIMD
// Decodes up to two "|x:y" points whose coordinates have one to four digits.
// One mask finds every boundary; single bytes confirm what each one is.
// Returns p when the first point is anything else.
FOSU_ALWAYS_INLINE const char* parse_ordinary_slider_points(
    Beatmap&                       beatmap,
    size_t&                        slider_point_count,
    const char*                    p,
    const HitObjectParseConstants& constants) {
  const u32 non_digits = nondigit_mask32(load32(p));
  // Two-thirds of points are "|ddd:ddd", which needs no boundary search.
  if ((non_digits & 0x1feu) == 0x110u && p[4] == ':' &&
      (p[8] == '|' || p[8] == ',')) {
    beatmap.slider_points[slider_point_count++] =
        decode_slider_point(load16(p), 3, 3, constants);
    if (p[8] == '|' && (non_digits & 0x1fe00u) == 0x11000u && p[12] == ':' &&
        (p[16] == '|' || p[16] == ',')) {
      beatmap.slider_points[slider_point_count++] =
          decode_slider_point(load16(p + 8), 3, 3, constants);
      return p + 16;
    }
    return p + 8;
  }

  u32       boundaries = non_digits & ~1u;
  const u32 first_colon = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const u32 first_end = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const u32 second_colon = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const u32 second_end = trailing_zeros(boundaries);

  const u32 first_x_digits = first_colon - 1;
  const u32 first_y_digits = first_end - first_colon - 1;
  if (((first_x_digits - 1) | (first_y_digits - 1)) > 3 ||
      p[first_colon] != ':' || (p[first_end] != '|' && p[first_end] != ','))
    return p;
  beatmap.slider_points[slider_point_count++] =
      decode_slider_point(load16(p), first_x_digits, first_y_digits, constants);

  const u32 second_x_digits = second_colon - first_end - 1;
  const u32 second_y_digits = second_end - second_colon - 1;
  if (p[first_end] != '|' ||
      ((second_x_digits - 1) | (second_y_digits - 1)) > 3 ||
      p[second_colon] != ':' || (p[second_end] != '|' && p[second_end] != ','))
    return p + first_end;
  beatmap.slider_points[slider_point_count++] = decode_slider_point(
      load16(p + first_end), second_x_digits, second_y_digits, constants);
  return p + second_end;
}
#endif

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
//
// This is one parse transaction. Control points and completed lazer segments
// are written into their arena arrays as they are accepted. Failures roll both
// arrays back so rejected sliders leave no published data behind.
//
// Lazer starts a segment at each curve type in the point list; before v128 it
// later applies legacy rules to each one. stable instead reads a one-character
// token as the curve type of the whole slider, the last one winning, and
// ignores one naming no type, in every format; fosu follows it in stable mode.
// Lazer would also read a longer token starting with a letter as a type,
// which fosu rejects.
template <bool LazerFormat, Client C>
FOSU_NOINLINE bool parse_slider_as(
    Beatmap&                                        beatmap,
    HitObjectCounts&                                counts,
    HitObject&                                      object,
    const char*                                     p,
    const char*                                     end,
    [[maybe_unused]] const HitObjectParseConstants& constants) {
  if (p >= end)
    return false;

  const size_t   slider_point_begin = counts.slider_points;
  const size_t   slider_segment_begin = counts.slider_segments;

  constexpr bool kSegments = C == Client::Lazer;
  const auto     first_curve_type = parse_curve_type(*p++);
  if (!first_curve_type) {
    // Both clients read an unknown one-character type as Catmull.
    counts.policy_rejection = p >= end || *p == '|' || *p == ',';
    return false;
  }

  std::optional<u32> first_curve_degree;
  if (*first_curve_type == CurveType::Bezier && p < end && is_digit(*p)) {
    // B-spline degrees belong to lazer's v128 format. Otherwise the line is
    // rejected: stable cannot load it, and lazer before v128 (unlike us)
    // reads a B-spline.
    if constexpr (!LazerFormat)
      return false;
    i64         degree;
    const char* next = parse_osu_int(p, end, degree);
    if (next == p || degree <= 0 || degree > UINT32_MAX)
      return false;
    first_curve_degree = static_cast<u32>(degree);
    p = next;
  }

  CurveType          current_curve_type = *first_curve_type;
  std::optional<u32> current_curve_degree = first_curve_degree;
  size_t             segment_point_begin = counts.slider_points;
  bool               has_explicit_segments = false;

  while (p < end && *p == '|') {
#if FOSU_SIMD
    const char* ordinary_points_end = parse_ordinary_slider_points(
        beatmap, counts.slider_points, p, constants);
    if (ordinary_points_end != p) [[likely]] {
      p = ordinary_points_end;
      continue;
    }
#endif

    const bool one_character_token =
        p + 1 < end && p[1] != '|' && p[1] != ',' &&
        (p + 2 == end || p[2] == '|' || p[2] == ',');
    if constexpr (!kSegments) {
      if (one_character_token) {
        if (const auto type = parse_curve_type(p[1]))
          current_curve_type = *type;
        p += 2;
        continue;
      }
    }

    const bool starts_segment =
        kSegments && p + 1 < end &&
        (p[1] == 'B' || p[1] == 'C' || p[1] == 'L' || p[1] == 'P');
    if (kSegments && !starts_segment && one_character_token &&
        ((p[1] | 0x20) >= 'a' && (p[1] | 0x20) <= 'z')) {
      counts.slider_points = slider_point_begin;
      counts.slider_segments = slider_segment_begin;
      counts.policy_rejection = true;
      return false;
    }

    CurveType          next_curve_type = current_curve_type;
    std::optional<u32> next_curve_degree = current_curve_degree;
    if (starts_segment) {
      ++p;
      const auto type = parse_curve_type(*p++);
      if (!type) {
        counts.slider_points = slider_point_begin;
        counts.slider_segments = slider_segment_begin;
        return false;
      }
      next_curve_type = *type;
      next_curve_degree.reset();
      // As for the first type, fosu reads B-spline degrees only in lazer's
      // v128 format.
      if (LazerFormat && next_curve_type == CurveType::Bezier && p < end &&
          is_digit(*p)) {
        i64         degree;
        const char* next = parse_osu_int(p, end, degree);
        if (next == p || degree <= 0 || degree > UINT32_MAX) {
          counts.slider_points = slider_point_begin;
          counts.slider_segments = slider_segment_begin;
          return false;
        }
        next_curve_degree = static_cast<u32>(degree);
        p = next;
      }
    }

    if (p >= end || *p != '|') {
      counts.slider_points = slider_point_begin;
      counts.slider_segments = slider_segment_begin;
      return false;
    }

    const auto point = parse_slider_point<LazerFormat, C>(p, end);
    if (!point) {
      counts.slider_points = slider_point_begin;
      counts.slider_segments = slider_segment_begin;
      return false;
    }
    beatmap.slider_points[counts.slider_points++] = point->point;
    p = point->end;

    if (starts_segment) {
      if (counts.slider_segments == beatmap.slider_segments.size()) {
        counts.slider_points = slider_point_begin;
        counts.slider_segments = slider_segment_begin;
        return false;
      }
      beatmap.slider_segments[counts.slider_segments++] = {
          .type = current_curve_type,
          .degree = current_curve_degree,
          .point_begin =
              static_cast<u32>(segment_point_begin - slider_point_begin),
          .point_count =
              static_cast<u32>(counts.slider_points - segment_point_begin),
      };
      has_explicit_segments = true;
      segment_point_begin = counts.slider_points - 1;
      current_curve_type = next_curve_type;
      current_curve_degree = next_curve_degree;
    }
  }

  // Everything after the point list is positional.
  if (p >= end || *p != ',') {
    counts.slider_points = slider_point_begin;
    counts.slider_segments = slider_segment_begin;
    return false;
  }
  ++p;

  i32       slides;
  const u32 first_slide_digit = static_cast<u8>(p[0] - '0');
  if (first_slide_digit <= 9 && p[1] == ',') {
    slides = static_cast<i32>(first_slide_digit);
    ++p;
  } else {
    const u32 digits = digit_run8(p);
    if (digits - 1 <= 6) {
      slides = static_cast<i32>(swar_parse_u64(p, digits));
      p = skip_numeric_space(p + digits, end);
    } else {
      i64         parsed_slides;
      const char* next = parse_osu_int(p, end, parsed_slides);
      if (next == p) {
        counts.slider_points = slider_point_begin;
        counts.slider_segments = slider_segment_begin;
        return false;
      }
      slides = clamp_i32(parsed_slides);
      p = next;
    }
  }
  if (slides > 9000 || (p < end && *p != ',')) {
    counts.slider_points = slider_point_begin;
    counts.slider_segments = slider_segment_begin;
    return false;
  }

  f64 length = 0;
  if (p < end) {
    const char* length_begin = p + 1;
    const char* next = parse_short_decimal(length_begin, length);
    if (!next || next > end || *next == 'e' || *next == 'E' ||
        !(std::abs(length) <= kSliderLengthLimit<C>))
      next = parse_osu_double(length_begin, end, length, kSliderLengthLimit<C>);
    if (next == length_begin || (next < end && *next != ',')) {
      counts.slider_points = slider_point_begin;
      counts.slider_segments = slider_segment_begin;
      return false;
    }
    p = next;
  }

  std::string_view sound_fields[3];
  // Most sliders end in the editor's unrepeated tail ",d|d,d:d|d:d,d:d:d:d:",
  // which needs no field search or further validation.
  const bool editor_tail = end - p == 21 && p[0] == ',' && is_digit(p[1]) &&
                           p[2] == '|' && is_digit(p[3]) && p[4] == ',' &&
                           short_edge_sets(p + 5) && p[12] == ',' &&
                           short_sample(p + 13);
  if (editor_tail) {
    sound_fields[0] = {p + 1, 3};
    sound_fields[1] = {p + 5, 7};
    sound_fields[2] = {p + 13, 8};
  } else if (p < end) {
    const char* sound_begin = p + 1;
    for (auto& field : sound_fields) {
      const char* field_end = find_byte<','>(sound_begin, end);
      field = {sound_begin, static_cast<size_t>(field_end - sound_begin)};
      if (field_end == end)
        break;
      sound_begin = field_end + 1;
    }
  }

  const std::string_view edge_sounds = sound_fields[0];
  const std::string_view edge_sets = sound_fields[1];
  const std::string_view hit_sample = sound_fields[2];
  if (!editor_tail && (!valid_sample(hit_sample, true) ||
                       !valid_edge_sets(edge_sets, slides))) {
    counts.slider_points = slider_point_begin;
    counts.slider_segments = slider_segment_begin;
    return false;
  }

  if constexpr (kSegments) {
    if (has_explicit_segments || first_curve_degree) {
      if (counts.slider_segments == beatmap.slider_segments.size()) {
        counts.slider_points = slider_point_begin;
        counts.slider_segments = slider_segment_begin;
        return false;
      }
      beatmap.slider_segments[counts.slider_segments++] = {
          .type = current_curve_type,
          .degree = current_curve_degree,
          .point_begin =
              static_cast<u32>(segment_point_begin - slider_point_begin),
          .point_count =
              static_cast<u32>(counts.slider_points - segment_point_begin),
      };
    }
  }

  beatmap.sliders[counts.sliders] = {
      .point_begin = static_cast<u32>(slider_point_begin),
      .point_count =
          static_cast<u32>(counts.slider_points - slider_point_begin),
      .segment_begin = static_cast<u32>(slider_segment_begin),
      .segment_count = kSegments ? static_cast<u32>(counts.slider_segments -
                                                    slider_segment_begin)
                                 : 0,
      .slides = std::max(1, slides),
      .curve_type = kSegments ? *first_curve_type : current_curve_type,
      .length = std::max(0.0, length),
      .edge_sounds = edge_sounds,
      .edge_sets = edge_sets,
  };
  object.hit_sample = hit_sample;
  object.slider = static_cast<u32>(counts.sliders++);
  return true;
}
}  // namespace fosu::internal
