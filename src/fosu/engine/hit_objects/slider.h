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

struct ParsedCurveType {
  CurveType type;
  std::optional<uint32_t> degree;
  const char* next;
};

inline std::optional<ParsedCurveType> parse_curve_type(const char* p, const char* end) {
  if (p == end)
    return std::nullopt;
  const auto type = parse_curve_type(*p++);
  if (!type)
    return std::nullopt;
  std::optional<uint32_t> degree;
  if (*type == CurveType::Bezier && p < end && is_digit(*p)) {
    int64_t value;
    const char* next = parse_osu_int(p, end, value);
    if (next == p || value <= 0 || value > UINT32_MAX)
      return std::nullopt;
    degree = static_cast<uint32_t>(value);
    p = next;
  }
  return ParsedCurveType{*type, degree, p};
}

struct ParsedSliderCoordinate {
  float value;
  const char* next;
};

template <typename Point>
struct ParsedSliderPoint {
  Point value;
  const char* next;
};

// Slider control point coordinate: overwhelmingly 1-4 plain digits, parsed
// branchlessly via SWAR. Signs, 5+ digit values, and empty fields take the
// general path.
inline std::optional<ParsedSliderCoordinate>
parse_slider_coordinate(const char* p, const char* end, bool preserve_fraction = false) {
  const uint32_t run = digit_run8(p);
  if (run - 1 <= 3 && run <= static_cast<size_t>(end - p) &&
      (p[run] == ':' || p[run] == '|' || p[run] == ',')) {
    return ParsedSliderCoordinate{static_cast<float>(swar_parse_u32(p, run)), p + run};
  }
  float v;
  const char* q = parse_osu_float(p, end, v, 131072);
  if (q == p)
    return std::nullopt;
  return ParsedSliderCoordinate{
      preserve_fraction ? v : static_cast<float>(static_cast<int32_t>(v)), q};
}

#if FOSU_SIMD
// Slider length on the editor-emitted shape: up to 8 integer digits, an
// optional '.', up to 13 fraction digits, at most 18 digits in all. One
// 32-byte load classifies the whole number; the mantissa is assembled from
// the same integer chunks parse_double accumulates and divided by the same
// power of ten, so the result is bit-identical. Returns nullptr for any
// other shape (sign, exponent, longer or empty numbers) so the caller can
// run parse_double. Values above the official length bound also defer.
// The line terminator and buffer padding are non-digits,
// so the digit run can never cross the end of the line.
struct ParsedSliderLength {
  double value;
  const char* next;
};

inline std::optional<ParsedSliderLength> try_parse_slider_length_fast(const char* p) {
  const Bytes32 v = load32(p);
  const uint64_t nd = nondigit_mask32(v);
  const auto il = static_cast<uint32_t>(trailing_zeros(nd));  // 1..8 if valid
  if (il - 1 > 7)
    return std::nullopt;
  // Integer and fractional lengths alternate within a map, so the dot and
  // fraction handling is computed unconditionally: a zero-length fraction
  // contributes nothing and dividing by 10^0 is exact.
  const bool has_dot = p[il] == '.';  // il <= 32 stays inside the padding
  // Integer lengths skip the fraction and the division: a branchless form
  // with an unconditional divide measured about 7% slower on the corpus
  // when the input is cache-resident.
  const uint32_t fl = has_dot ? static_cast<uint32_t>(trailing_zeros(nd >> (il + 1))) : 0;
  if (fl > 13 || il + fl > 18)
    return std::nullopt;
  const uint32_t fl1 = fl <= 8 ? fl : 8;
  const uint32_t fl2 = fl - fl1;
  const char* fp = p + il + 1;
#if FOSU_SIMD_X86
  const auto chunks = decode_decimal_chunks(p, il, fp, fl1);
  uint64_t mant = chunks.integer;
  if (fl1)
    mant = mant * kPow10u[fl1] + chunks.fraction;
#else
  uint64_t mant = swar_parse_u64(p, il);
  if (fl1)
    mant = mant * kPow10u[fl1] + swar_parse_u64(fp, fl1);
#endif
  if (fl2)
    mant = mant * kPow10u[fl2] + swar_parse_u64(fp + 8, fl2);
  if (mant > kMaxExactDoubleInteger)
    return std::nullopt;
  double d = static_cast<double>(mant);
  if (fl)
    d /= kPow10[fl];
  if (d > 131072)
    return std::nullopt;
  const char* q = has_dot ? fp + fl : p + il;
  if (*q == 'e' || *q == 'E')
    return std::nullopt;
  return ParsedSliderLength{d, q};
}

// One pshufb per control point right-aligns both coordinates' digits into
// two 4-byte groups (0x80 lanes read as zero); maddubs/madd then decode the
// pair together into one 8-byte point. The source vector starts
// at the '|' that opens the point, so x digits sit at bytes 1..len_x and y
// digits follow the colon. Indexed by (len_x - 1, len_y - 1).
struct alignas(16) PointShuf {
  int8_t b[16];
};
consteval std::array<PointShuf, 16> make_point_shuf() {
  std::array<PointShuf, 16> out{};
  for (int xl = 1; xl <= 4; ++xl)
    for (int yl = 1; yl <= 4; ++yl) {
      PointShuf& m = out[(xl - 1) * 4 + (yl - 1)];
      for (auto& b : m.b)
        b = static_cast<int8_t>(0x80);
      for (int i = 0; i < xl; ++i)
        m.b[4 - xl + i] = static_cast<int8_t>(1 + i);
      for (int i = 0; i < yl; ++i)
        m.b[8 - yl + i] = static_cast<int8_t>(xl + 2 + i);
    }
  return out;
}
inline constexpr auto kPointShuf = make_point_shuf();

// `src` starts at a '|'; callers validate that both digit counts are 1..4.
#if FOSU_SIMD_X86
template <typename Point>
inline Point decode_slider_point(__m128i src,
                                 uint32_t xl,
                                 uint32_t yl,
                                 const HitObjectParseConstants& k) {
  static_assert(sizeof(Point) == 8 && offsetof(Point, x) == 0 && offsetof(Point, y) == 4);
  const __m128i shuf = _mm_load_si128(
      reinterpret_cast<const __m128i*>(kPointShuf[(xl - 1) * 4 + (yl - 1)].b));
  const __m128i placed =
      _mm_shuffle_epi8(_mm_sub_epi8(src, _mm256_castsi256_si128(k.zero)), shuf);
  const auto coordinates =
      _mm_madd_epi16(_mm_maddubs_epi16(placed, k.pair_weights), k.word_weights);
  const auto positions = _mm_cvtepi32_ps(coordinates);
  return std::bit_cast<Point>(
      static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_castps_si128(positions))));
}

#else
template <typename Point>
inline Point decode_slider_point(uint8x16_t src,
                                 uint32_t xl,
                                 uint32_t yl,
                                 const HitObjectParseConstants& k) {
  static_assert(sizeof(Point) == 8 && offsetof(Point, x) == 0 && offsetof(Point, y) == 4);
  const auto* shuf =
      reinterpret_cast<const uint8_t*>(kPointShuf[(xl - 1) * 4 + (yl - 1)].b);
  const auto coordinates =
      decimal_groups(vqtbl1q_u8(vsubq_u8(src, k.zero), vld1q_u8(shuf)));
  const auto positions = vcvtq_f32_u32(coordinates);
  return std::bit_cast<Point>(vgetq_lane_u64(vreinterpretq_u64_f32(positions), 0));
}
#endif

template <typename Point>
struct ParsedSliderPointPrefix {
  Point first;
  Point second;  // Meaningful only when has_second is true.
  const char* next;
  bool has_second;
};

// Up to two editor-shaped points ("|x:y", 1..4 digits each) from one 32-byte
// window. Decode only validated points; if the second is absent or needs
// general parsing, return the first and leave the cursor at its end. Most
// sliders have one or two points, avoiding the general point loop entirely.
// Returns coordinates and the next input position, or nullopt
// if the first point needs the general parser. No destination is modified.
template <typename Point>
inline std::optional<ParsedSliderPointPrefix<Point>> try_parse_slider_point_prefix_fast(
    const char* p,
    const HitObjectParseConstants& k) {
  const Bytes32 v = load32(p);
  const uint32_t nd = nondigit_mask32(v);
  const auto colon = equal_mask32(v, k.colon);
  const auto pipe = equal_mask32(v, k.pipe);
#if FOSU_SIMD_X86
  const uint32_t sep = static_cast<uint32_t>(_mm256_movemask_epi8(
      _mm256_or_si256(_mm256_cmpeq_epi8(v, k.pipe), _mm256_cmpeq_epi8(v, k.comma))));
#else
  const uint32_t sep =
      byte_mask16(vorrq_u8(vceqq_u8(v.val[0], k.pipe), vceqq_u8(v.val[0], k.comma))) |
      (byte_mask16(vorrq_u8(vceqq_u8(v.val[1], k.pipe), vceqq_u8(v.val[1], k.comma)))
       << 16);
#endif
  // Skip the opening '|', then locate ':', separator, ':', separator.
  // Clearing each boundary bit lets later positions be found without
  // waiting for the preceding coordinate length. Missing boundaries are 32;
  // use 64-bit mask tests below so that sentinel remains a defined shift.
  uint32_t boundaries = nd & ~1u;
  const uint32_t first_colon = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const uint32_t first_end = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const uint32_t second_colon = trailing_zeros(boundaries);
  boundaries &= boundaries - 1;
  const uint32_t second_end = trailing_zeros(boundaries);
  const uint32_t first_x_digits = first_colon - 1;
  const uint32_t first_y_digits = first_end - first_colon - 1;
  const bool first_valid = (pipe & 1) &
                           (((first_x_digits - 1) | (first_y_digits - 1)) <= 3) &
                           ((static_cast<uint64_t>(colon) >> first_colon) & 1) &
                           ((static_cast<uint64_t>(sep) >> first_end) & 1);
  const uint32_t second_x_digits = second_colon - first_end - 1;
  const uint32_t second_y_digits = second_end - second_colon - 1;
  const bool second_valid = ((static_cast<uint64_t>(pipe) >> first_end) & 1) &
                            (((second_x_digits - 1) | (second_y_digits - 1)) <= 3) &
                            ((static_cast<uint64_t>(colon) >> second_colon) & 1) &
                            ((static_cast<uint64_t>(sep) >> second_end) & 1);
  if (!first_valid)
    return std::nullopt;
#if FOSU_SIMD_X86
  const auto first = decode_slider_point<Point>(_mm256_castsi256_si128(v), first_x_digits,
                                                first_y_digits, k);
#else
  const auto first =
      decode_slider_point<Point>(v.val[0], first_x_digits, first_y_digits, k);
#endif
  if (!second_valid)
    return ParsedSliderPointPrefix<Point>{first, {}, p + first_end, false};
#if FOSU_SIMD_X86
  const auto second = decode_slider_point<Point>(
      _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + first_end)), second_x_digits,
      second_y_digits, k);
#else
  const auto second = decode_slider_point<Point>(
      vld1q_u8(reinterpret_cast<const uint8_t*>(p + first_end)), second_x_digits,
      second_y_digits, k);
#endif
  return ParsedSliderPointPrefix<Point>{first, second, p + second_end, true};
}
#endif

// Decode one "|x:y" point without modifying input or destination storage.
// The common 1-4 digit shape uses SIMD; other spellings use bounded parsing.
template <typename Point>
inline std::optional<ParsedSliderPoint<Point>> parse_slider_point(
    const char* p,
    const char* end,
    [[maybe_unused]] const HitObjectParseConstants& k,
    bool preserve_fraction = false) {
  if (p >= end || *p != '|')
    return std::nullopt;
#if FOSU_SIMD
#if FOSU_SIMD_X86
  const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
  const auto nd = nondigit_mask16(v);
  const auto colon = static_cast<uint32_t>(
      _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm256_castsi256_si128(k.colon))));
#else
  const auto v = vld1q_u8(reinterpret_cast<const uint8_t*>(p));
  const auto nd = nondigit_mask16(v);
  const auto colon = byte_mask16(vceqq_u8(v, k.colon));
#endif
  const uint32_t xl = trailing_zeros(nd >> 1);
  const uint32_t c = xl & 7;
  const uint32_t yl = trailing_zeros(nd >> (2 + c));
  if (((xl - 1) | (yl - 1)) <= 3 && ((colon >> (1 + c)) & 1)) {
    const char* next = p + 2 + xl + yl;
    if (*next == '|' || *next == ',')
      return ParsedSliderPoint<Point>{decode_slider_point<Point>(v, xl, yl, k), next};
  }
#endif
  const auto x = parse_slider_coordinate(p + 1, end, preserve_fraction);
  if (!x || *x->next != ':')
    return std::nullopt;
  const auto y = parse_slider_coordinate(x->next + 1, end, preserve_fraction);
  if (!y)
    return std::nullopt;
  return ParsedSliderPoint<Point>{Point{x->value, y->value}, y->next};
}

struct SliderSoundFields {
  std::string_view edge_sounds;
  std::string_view edge_sets;
  std::string_view hit_sample;
};

// Fields after length are positional; absent fields are empty and any
// columns after hitSample are ignored. Views borrow the padded input.
inline SliderSoundFields parse_slider_sound_fields(
    const char* p,
    const char* end,
    [[maybe_unused]] const HitObjectParseConstants& k) {
  if (p == end)
    return {};
#if FOSU_SIMD
  const auto span = static_cast<size_t>(end - p);
  if (span <= 32) {
    const auto commas =
        equal_mask32(load32(p), k.comma) & static_cast<uint32_t>((1ull << span) - 1);
    const uint32_t first = trailing_zeros(commas);
    const uint32_t rest = commas & (commas - 1);
    const uint32_t second = trailing_zeros(rest);
    const uint32_t third = trailing_zeros(rest & (rest - 1));
    if (first >= span)
      return {{p, span}, {}, {}};
    if (second >= span)
      return {{p, first}, {p + first + 1, span - first - 1}, {}};
    const auto sample_end = third < span ? third : static_cast<uint32_t>(span);
    return {{p, first},
            {p + first + 1, second - first - 1},
            {p + second + 1, sample_end - second - 1}};
  }
  std::string_view fields[3];
  size_t field_count = 0;
  const char* field_start = p;
  while (p < end) {
    const size_t remaining = static_cast<size_t>(end - p);
    auto commas = equal_mask32(load32(p), k.comma);
    if (remaining < 32)
      commas &= (1u << remaining) - 1;
    while (commas) {
      const char* comma = p + trailing_zeros(commas);
      fields[field_count++] = {field_start, static_cast<size_t>(comma - field_start)};
      if (field_count == 3)
        return {fields[0], fields[1], fields[2]};
      field_start = comma + 1;
      commas &= commas - 1;
    }
    p += std::min(remaining, size_t(32));
  }
  fields[field_count] = {field_start, static_cast<size_t>(end - field_start)};
#else
  std::string_view fields[3];
  for (auto& field : fields) {
    const auto* comma = static_cast<const char*>(memchr(p, ',', end - p));
    const char* field_end = comma ? comma : end;
    field = {p, static_cast<size_t>(field_end - p)};
    if (!comma)
      break;
    p = comma + 1;
  }
#endif
  return {fields[0], fields[1], fields[2]};
}

struct SliderTail {
  int32_t slides;
  double length;
  SliderSoundFields sounds;
};

// Parses ",slides[,length[,edgeSounds,edgeSets,hitSample]]" after the
// point list. A missing length defaults to zero; an explicitly empty one
// is invalid. Validation has no effect on the point pool or hitobject.
// Inline so the returned fields can flow directly into the destination record.
__attribute__((always_inline)) inline std::optional<SliderTail>
parse_slider_tail(const char* p, const char* end, const HitObjectParseConstants& k) {
  if (p >= end || *p != ',')
    return std::nullopt;
  ++p;
  // Editor files normally write one or two bare digits.
  int32_t slides;
  const uint32_t first = static_cast<uint8_t>(p[0] - '0');
  const uint32_t second = static_cast<uint8_t>(p[1] - '0');
  if (first <= 9 && p[1] == ',') {
    slides = static_cast<int32_t>(first);
    p += 1;
  } else if (first <= 9 && second <= 9 && p[2] == ',') {
    slides = static_cast<int32_t>(first * 10 + second);
    p += 2;
  } else {
    const uint32_t run = digit_run8(p);
    if (run - 1 <= 6) {
      slides = static_cast<int32_t>(swar_parse_u64(p, run));
      p = skip_numeric_space(p + run, end);
    } else {
      int64_t wide;
      const char* next = parse_osu_int(p, end, wide);
      if (next == p)
        return std::nullopt;
      slides = clamp_i32(wide);
      p = next;
    }
  }
  if (slides > 9000 || (p < end && *p != ','))
    return std::nullopt;
  double length = 0;
  if (p < end) {
#if FOSU_SIMD
    const auto fast_length = try_parse_slider_length_fast(p + 1);
    const char* next;
    if (fast_length) {
      length = fast_length->value;
      next = fast_length->next;
    } else {
      next = parse_osu_double(p + 1, end, length, 131072);
    }
#else
    const char* next = parse_osu_double(p + 1, end, length, 131072);
#endif
    if (next != p + 1)
      next = skip_numeric_space(next, end);
    if (next == p + 1 || (next < end && *next != ','))
      return std::nullopt;
    p = next;
  }
  const auto sounds =
      p == end ? SliderSoundFields{} : parse_slider_sound_fields(p + 1, end, k);
  if (!valid_sample(sounds.hit_sample, true) ||
      !valid_edge_sets(sounds.edge_sets, slides))
    return std::nullopt;
  return SliderTail{slides, length, sounds};
}

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
//
// Each point is parsed into a local value before it is copied into the
// beatmap arena. A malformed point list is discarded. Points parsed before a
// malformed later field remain, matching the official decoder's behavior.
inline const char* parse_initial_slider_points(Beatmap& beatmap,
                                               size_t& slider_point_count,
                                               const char* p,
                                               const HitObjectParseConstants& constants) {
#if FOSU_SIMD
  if (const auto points = try_parse_slider_point_prefix_fast<SliderPoint>(p, constants)) {
    beatmap.slider_points[slider_point_count++] = points->first;
    if (points->has_second)
      beatmap.slider_points[slider_point_count++] = points->second;
    return points->next;
  }
#else
  (void)beatmap;
  (void)slider_point_count;
  (void)constants;
#endif
  return p;
}

inline const char* parse_legacy_slider_points(Beatmap& beatmap,
                                              size_t& slider_point_count,
                                              const char* p,
                                              const char* end,
                                              const HitObjectParseConstants& constants) {
  p = parse_initial_slider_points(beatmap, slider_point_count, p, constants);
  while (p < end && *p == '|') {
    const auto point = parse_slider_point<SliderPoint>(p, end, constants);
    if (!point) [[unlikely]]
      return nullptr;
    beatmap.slider_points[slider_point_count++] = point->value;
    p = point->next;
  }
  return p;
}

inline bool append_slider_segment(Beatmap& beatmap,
                                  size_t& slider_segment_count,
                                  const ParsedCurveType& curve,
                                  size_t slider_point_begin,
                                  size_t segment_point_begin,
                                  size_t slider_point_count) {
  if (slider_segment_count == beatmap.slider_segments.size())
    return false;
  beatmap.slider_segments[slider_segment_count++] = {
      .type = curve.type,
      .degree = curve.degree,
      .point_begin = static_cast<uint32_t>(segment_point_begin - slider_point_begin),
      .point_count = static_cast<uint32_t>(slider_point_count - segment_point_begin),
  };
  return true;
}

struct ParsedLazerSliderPoints {
  const char* next;
  ParsedCurveType final_curve;
  size_t final_segment_point_begin;
  bool has_explicit_segments;
};

inline std::optional<ParsedLazerSliderPoints> parse_lazer_slider_points(
    Beatmap& beatmap,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    size_t slider_point_begin,
    ParsedCurveType curve,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  size_t segment_point_begin = slider_point_count;
  bool has_explicit_segments = false;
  p = parse_initial_slider_points(beatmap, slider_point_count, p, constants);

  while (p < end && *p == '|') {
    const bool starts_segment =
        p + 1 < end && (p[1] == 'B' || p[1] == 'C' || p[1] == 'L' || p[1] == 'P');
    if (starts_segment) {
      const auto next_curve = parse_curve_type(p + 1, end);
      if (!next_curve)
        return std::nullopt;
      const auto boundary =
          parse_slider_point<SliderPoint>(next_curve->next, end, constants, true);
      if (!boundary)
        return std::nullopt;
      beatmap.slider_points[slider_point_count++] = boundary->value;
      if (!append_slider_segment(beatmap, slider_segment_count, curve, slider_point_begin,
                                 segment_point_begin, slider_point_count)) {
        return std::nullopt;
      }
      has_explicit_segments = true;
      segment_point_begin = slider_point_count - 1;
      curve = *next_curve;
      p = boundary->next;
    } else {
      const auto point = parse_slider_point<SliderPoint>(p, end, constants, true);
      if (!point) [[unlikely]]
        return std::nullopt;
      beatmap.slider_points[slider_point_count++] = point->value;
      p = point->next;
    }
  }

  return ParsedLazerSliderPoints{p, curve, segment_point_begin, has_explicit_segments};
}

inline bool parse_legacy_slider(Beatmap& beatmap,
                                size_t& slider_count,
                                size_t& slider_point_count,
                                HitObject& object,
                                const char* p,
                                const char* end,
                                const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return false;

  const auto first_curve = parse_curve_type(p, end);
  if (!first_curve)
    return false;
  p = first_curve->next;
  const size_t slider_point_begin = slider_point_count;
  p = parse_legacy_slider_points(beatmap, slider_point_count, p, end, constants);
  if (!p) {
    slider_point_count = slider_point_begin;
    return false;
  }

  const auto tail = parse_slider_tail(p, end, constants);
  if (!tail) [[unlikely]]
    return false;

  Slider slider{
      .point_begin = static_cast<uint32_t>(slider_point_begin),
      .point_count = static_cast<uint32_t>(slider_point_count - slider_point_begin),
      .segment_begin = 0,
      .segment_count = 0,
      .slides = std::max(1, tail->slides),
      .curve_type = first_curve->type,
      .length = std::max(0.0, tail->length),
      .edge_sounds = tail->sounds.edge_sounds,
      .edge_sets = tail->sounds.edge_sets,
  };
  object.hit_sample = tail->sounds.hit_sample;
  object.slider = static_cast<uint32_t>(slider_count);
  beatmap.sliders[slider_count++] = slider;
  return true;
}

inline bool parse_lazer_slider(Beatmap& beatmap,
                               size_t& slider_count,
                               size_t& slider_segment_count,
                               size_t& slider_point_count,
                               HitObject& object,
                               const char* p,
                               const char* end,
                               const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return false;

  const auto first_curve = parse_curve_type(p, end);
  if (!first_curve)
    return false;
  const size_t slider_point_begin = slider_point_count;
  const size_t slider_segment_begin = slider_segment_count;
  const auto points = parse_lazer_slider_points(
      beatmap, slider_segment_count, slider_point_count, slider_point_begin, *first_curve,
      first_curve->next, end, constants);
  if (!points) {
    slider_point_count = slider_point_begin;
    slider_segment_count = slider_segment_begin;
    return false;
  }

  const auto tail = parse_slider_tail(points->next, end, constants);
  if (!tail) [[unlikely]] {
    slider_segment_count = slider_segment_begin;
    return false;
  }

  if ((points->has_explicit_segments || first_curve->degree) &&
      !append_slider_segment(beatmap, slider_segment_count, points->final_curve,
                             slider_point_begin, points->final_segment_point_begin,
                             slider_point_count)) {
    slider_point_count = slider_point_begin;
    slider_segment_count = slider_segment_begin;
    return false;
  }

  Slider slider{
      .point_begin = static_cast<uint32_t>(slider_point_begin),
      .point_count = static_cast<uint32_t>(slider_point_count - slider_point_begin),
      .segment_begin = static_cast<uint32_t>(slider_segment_begin),
      .segment_count = static_cast<uint32_t>(slider_segment_count - slider_segment_begin),
      .slides = std::max(1, tail->slides),
      .curve_type = first_curve->type,
      .length = std::max(0.0, tail->length),
      .edge_sounds = tail->sounds.edge_sounds,
      .edge_sets = tail->sounds.edge_sets,
  };
  object.hit_sample = tail->sounds.hit_sample;
  object.slider = static_cast<uint32_t>(slider_count);
  beatmap.sliders[slider_count++] = slider;
  return true;
}

__attribute__((noinline)) inline bool parse_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject& object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  if (beatmap.format_version >= 128) {
    return parse_lazer_slider(beatmap, slider_count, slider_segment_count,
                              slider_point_count, object, p, end, constants);
  }
  return parse_legacy_slider(beatmap, slider_count, slider_point_count, object, p, end,
                             constants);
}

}  // namespace fosu::internal
