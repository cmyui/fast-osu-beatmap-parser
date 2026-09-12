#pragma once

#include <algorithm>
#include <cstring>
#include <optional>

#include <fosu/beatmap.h>
#include <fosu/engine/hit_objects/common_fields.h>
#include <fosu/engine/hit_objects/object_types.h>
#include <fosu/engine/hit_objects/slider_fields.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>

namespace fosu::internal {

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

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, or a trailing hit sample.
__attribute__((noinline)) inline bool parse_hitobject_details(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject& object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  switch (classify_hitobject_kind(object.type)) {
    case HitObjectKind::Circle: {
      const auto details = parse_circle_details(p, end);
      if (!details)
        return false;
      object.end_time = 0;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Slider:
      return p < end && *p == ',' &&
             parse_slider(beatmap, slider_count, slider_segment_count, slider_point_count,
                          object, p + 1, end, constants);
    case HitObjectKind::Spinner: {
      const auto details = parse_spinner_details(p, end);
      if (!details)
        return false;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Hold: {
      const auto details = parse_hold_details(object.time, p, end);
      if (!details)
        return false;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Invalid:
      return false;
  }
  return false;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
inline std::optional<HitObject> parse_hitobject_line_scalar(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    const char* p,
    const char* line_end,
    const HitObjectParseConstants& constants) {
  float x;
  const char* next = parse_osu_float(p, line_end, x, 131072);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  float y;
  next = parse_osu_float(p, line_end, y, 131072);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  double time;
  next = parse_osu_double(p, line_end, time);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  int64_t type;
  next = parse_osu_int(p, line_end, type);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  int64_t hitsound;
  next = parse_osu_int(p, line_end, hitsound);
  if (next == p || (next < line_end && *next != ','))
    return std::nullopt;

  if (beatmap.format_version < 128) {
    x = static_cast<float>(static_cast<int32_t>(x));
    y = static_cast<float>(static_cast<int32_t>(y));
  }

  ++beatmap.stats.slow_path_lines;
  HitObject object{
      .x = x,
      .y = y,
      .type = static_cast<uint32_t>(type),
      .hitsound = static_cast<uint32_t>(hitsound),
      .time = time,
      .end_time = 0,
      .slider = HitObject::kNoSlider,
      .new_combo = false,
      .combo_skip = 0,
      .hit_sample = {},
  };
  if (!parse_hitobject_details(beatmap, slider_count, slider_segment_count,
                               slider_point_count, object, next, line_end, constants)) {
    return std::nullopt;
  }
  return object;
}

// Interpret a successfully decoded record before publishing it to the arena.
// The preceding accepted object is still in source order, including across
// repeated HitObjects sections. No separate state crosses the engine boundary.
inline HitObject normalize_hitobject(HitObject object,
                                     const Beatmap& beatmap,
                                     size_t preceding_count,
                                     int offset) {
  const bool explicit_combo = object.type & 4;
  object.time += offset;
  object.new_combo = false;
  object.combo_skip = 0;
  if (object.is_circle() || object.is_slider()) {
    object.new_combo =
        !preceding_count || explicit_combo ||
        classify_hitobject_kind(beatmap.hit_objects[preceding_count - 1].type) ==
            HitObjectKind::Spinner;
    object.combo_skip = explicit_combo ? (object.type >> 4) & 7 : 0;
    object.end_time = object.is_circle() ? object.time : 0;
  } else if (object.is_spinner()) {
    object.new_combo = explicit_combo;
    object.x = 256;
    object.y = 192;
    object.end_time = std::max(object.time, object.end_time + offset);
  } else {
    // Legacy holds clamp against the offset start before offsetting the end.
    object.end_time = std::max(object.time, object.end_time) + offset;
  }
  return object;
}

inline const char* parse_hitobjects_section_scalar(
    Beatmap& beatmap,
    size_t& hit_object_count,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    const char* p,
    const char* file_end,
    const HitObjectParseConstants& constants,
    int time_offset) {
  while (p < file_end) {
    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    if (c == '[')
      break;
    const auto* newline =
        static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(file_end - p)));
    const char* line_end = newline ? newline : file_end;
    if (line_end > p && line_end[-1] == '\r')
      --line_end;
    const char* next_line = newline ? newline + 1 : file_end;
    if (!ignored_line(p, line_end)) {
      if (const auto object =
              parse_hitobject_line_scalar(beatmap, slider_count, slider_segment_count,
                                          slider_point_count, p, line_end, constants)) {
        beatmap.hit_objects[hit_object_count] =
            normalize_hitobject(*object, beatmap, hit_object_count, time_offset);
        ++hit_object_count;
      } else [[unlikely]] {
        ++beatmap.stats.malformed_lines;
      }
    }
    p = next_line;
  }
  return p;
}

#if FOSU_SIMD
// SIMD section loop. One 32-byte load per line yields the newline, comma and
// non-digit masks. Lines outside the common editor shape take the scalar path.
inline const char* parse_hitobjects_section_simd(Beatmap& beatmap,
                                                 size_t& hit_object_count,
                                                 size_t& slider_count,
                                                 size_t& slider_segment_count,
                                                 size_t& slider_point_count,
                                                 const char* p,
                                                 const char* file_end,
                                                 const HitObjectParseConstants& constants,
                                                 int time_offset) {
  const ByteVector newline_value = constants.nl;
  const ByteVector comma_value = constants.comma;
  const ByteVector zero = constants.zero;
  uint32_t fast_lines = 0;
  uint32_t malformed = 0;

  while (p < file_end) {
    const Bytes32 ascii = load32(p);
    const auto newline_mask = equal_mask32(ascii, newline_value);
    const auto commas = equal_mask32(ascii, comma_value);
    const uint32_t nondigits = nondigit_mask32(ascii);
    const char* newline = newline_mask ? p + trailing_zeros(newline_mask)
                                       : find_byte<'\n'>(p + 32, file_end);
    const char* next_line = newline + (newline < file_end);
    const char* line_end = newline - (newline > p && newline[-1] == '\r');
    const auto length = static_cast<size_t>(line_end - p);
    const uint32_t m1 = nondigits & (nondigits - 1);
    const uint32_t m2 = m1 & (m1 - 1);
    const uint32_t m3 = m2 & (m2 - 1);
    const uint32_t m4 = m3 & (m3 - 1);
    const uint64_t p0 = trailing_zeros(nondigits);
    const uint64_t p1 = trailing_zeros(m1);
    const uint64_t p2 = trailing_zeros(m2);
    const uint64_t p3 = trailing_zeros(m3);
    const uint32_t prefix_end = trailing_zeros(m4);

    // Lanes (low to high): the first four delimiter positions. Their
    // differences are the lengths of x, y, time and type.
    const uint64_t delimiter_positions = p0 | p1 << 16 | p2 << 32 | p3 << 48;
    const uint64_t field_lengths =
        (delimiter_positions - (delimiter_positions << 16)) - 0x0002000200020001ull;
    const uint64_t overlong_fields = field_lengths + 0x7FFD7FF67FFD7FFDull;
    const bool field_lengths_ok =
        ((field_lengths | overlong_fields) & 0x8000800080008000ull) == 0;
    const uint32_t through_type = static_cast<uint32_t>((2ull << p3) - 1);
    const bool delimiters_are_commas = ((nondigits ^ commas) & through_type) == 0;
    const uint32_t hitsound_length = prefix_end - static_cast<uint32_t>(p3) - 1;
    const bool hitsound_length_ok = hitsound_length - 1 <= 1;
    const bool common_layout =
        field_lengths_ok & delimiters_are_commas & hitsound_length_ok;
    const char after_prefix = p[prefix_end];

    if (common_layout && (prefix_end == length || after_prefix == ',' ||
                          after_prefix == '\0')) [[likely]] {
      // 60*p0 + 27*p1 + 2*p2 + p3 identifies the shuffle for these
      // delimiter positions. hitSound has a separate one/two-digit dimension.
      uint32_t mask_index =
          static_cast<uint32_t>((delimiter_positions * 0x003C001B00020001ull) >> 48) -
          158;
      mask_index = mask_index * 2 + hitsound_length - 1;
      const uint32_t time_span = static_cast<uint32_t>(p2 - p1);

      uint32_t fields[4];
      double time;
      bool time_ok = true;
#if FOSU_SIMD_X86
      const __m256i digits = _mm256_sub_epi8(ascii, zero);
      const LaneMasks& masks = kLaneMasks[mask_index];
      const __m256i perm =
          _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.perm));
      const __m256i shuf =
          _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.shuf));
      const __m256i placed =
          _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(digits, perm), shuf);
      const __m256i pair_weights =
          _mm256_setr_epi8(0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10, 1, 0, 0, 10, 1, 0, 0,
                           10, 1, 0, 0, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
      const __m256i word_weights =
          _mm256_setr_epi16(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1);
      const __m256i words = _mm256_maddubs_epi16(placed, pair_weights);
      const __m256i values = _mm256_madd_epi16(words, word_weights);
      const __m128i low = _mm256_castsi256_si128(values);
      const __m128i time_groups = _mm256_extracti128_si256(values, 1);
      const __m128i field_values = _mm_add_epi32(low, _mm_slli_si128(time_groups, 12));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(fields), field_values);
      if (time_span <= 9) [[likely]] {
        const __m128i packed = _mm_packus_epi32(time_groups, time_groups);
        const __m128i combined =
            _mm_madd_epi16(packed, _mm_setr_epi16(0, 0, 10000, 1, 0, 0, 0, 0));
        const __m128i pair = _mm_shuffle_epi32(combined, _MM_SHUFFLE(0, 0, 0, 1));
        _mm_store_sd(&time, _mm_cvtepi32_pd(pair));
      } else {
        const uint64_t parsed_time =
            static_cast<uint32_t>(_mm_extract_epi32(time_groups, 1)) * 100000000ull +
            static_cast<uint32_t>(_mm_extract_epi32(time_groups, 2)) * 10000ull +
            static_cast<uint32_t>(_mm_extract_epi32(time_groups, 3));
        time_ok = parsed_time <= INT32_MAX;
        time = static_cast<double>(parsed_time);
      }
#else
      const Bytes32 digits{{vsubq_u8(ascii.val[0], zero), vsubq_u8(ascii.val[1], zero)}};
      const PrefixShuffle& masks = kPrefixShuffles[mask_index];
      const auto field_values = decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes)));
      const auto time_groups =
          decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes + 16)));
      vst1q_u32(fields, field_values);
      if (time_span <= 9) [[likely]] {
        const uint32_t weights[2] = {10000, 1};
        const auto terms = vmul_u32(vget_high_u32(time_groups), vld1_u32(weights));
        const auto sum = vpadd_u32(terms, terms);
        time = vgetq_lane_f64(vcvtq_f64_u64(vmovl_u32(sum)), 0);
      } else {
        const uint64_t parsed_time =
            uint64_t(vgetq_lane_u32(time_groups, 1)) * 100000000 +
            uint64_t(vgetq_lane_u32(time_groups, 2)) * 10000 +
            vgetq_lane_u32(time_groups, 3);
        time_ok = parsed_time <= INT32_MAX;
        time = static_cast<double>(parsed_time);
      }
#endif

      if (time_ok) [[likely]] {
        ++fast_lines;
        HitObject object{
            .x = static_cast<float>(fields[0]),
            .y = static_cast<float>(fields[1]),
            .type = fields[2],
            .hitsound = fields[3],
            .time = time,
            .end_time = 0,
            .slider = HitObject::kNoSlider,
            .new_combo = false,
            .combo_skip = 0,
            .hit_sample = {},
        };
        const HitObjectKind kind = classify_hitobject_kind(fields[2]);
        if (kind == HitObjectKind::Circle) {
          if (prefix_end == length) {
            object.hit_sample = {};
          } else if (length - prefix_end == 9 && after_prefix == ',' &&
                     short_sample(p + prefix_end + 1)) {
            object.hit_sample = {p + prefix_end + 1, 8};
          } else if (!parse_hitobject_details(beatmap, slider_count, slider_segment_count,
                                              slider_point_count, object, p + prefix_end,
                                              line_end, constants)) {
            ++malformed;
            p = next_line;
            continue;
          }
        } else if (kind == HitObjectKind::Slider) {
          if (prefix_end >= length || after_prefix != ',' ||
              !parse_slider(beatmap, slider_count, slider_segment_count,
                            slider_point_count, object, p + prefix_end + 1, line_end,
                            constants)) {
            ++malformed;
            p = next_line;
            continue;
          }
        } else if (!parse_hitobject_details(beatmap, slider_count, slider_segment_count,
                                            slider_point_count, object, p + prefix_end,
                                            line_end, constants)) {
          ++malformed;
          p = next_line;
          continue;
        }

        beatmap.hit_objects[hit_object_count] =
            normalize_hitobject(object, beatmap, hit_object_count, time_offset);
        ++hit_object_count;
      } else {
        const auto object =
            parse_hitobject_line_scalar(beatmap, slider_count, slider_segment_count,
                                        slider_point_count, p, line_end, constants);
        if (object) {
          beatmap.hit_objects[hit_object_count] =
              normalize_hitobject(*object, beatmap, hit_object_count, time_offset);
          ++hit_object_count;
        } else [[unlikely]] {
          ++malformed;
        }
      }
    } else {
      const char c = *p;
      if (c == '\r' || c == '\n') {
        ++p;
        continue;
      }
      if (c == '[')
        break;
      if (!ignored_line(p, line_end)) {
        if (const auto object =
                parse_hitobject_line_scalar(beatmap, slider_count, slider_segment_count,
                                            slider_point_count, p, line_end, constants)) {
          beatmap.hit_objects[hit_object_count] =
              normalize_hitobject(*object, beatmap, hit_object_count, time_offset);
          ++hit_object_count;
        } else [[unlikely]] {
          ++malformed;
        }
      }
    }
    p = next_line;
  }

  beatmap.stats.fast_path_lines += fast_lines;
  beatmap.stats.malformed_lines += malformed;
  return p;
}
#endif

inline const char* parse_hitobjects_section(Beatmap& beatmap,
                                            size_t& hit_object_count,
                                            size_t& slider_count,
                                            size_t& slider_segment_count,
                                            size_t& slider_point_count,
                                            const char* p,
                                            const char* file_end,
                                            int time_offset = 0) {
  const HitObjectParseConstants constants;
#if FOSU_SIMD
  return parse_hitobjects_section_simd(beatmap, hit_object_count, slider_count,
                                       slider_segment_count, slider_point_count, p,
                                       file_end, constants, time_offset);
#else
  return parse_hitobjects_section_scalar(beatmap, hit_object_count, slider_count,
                                         slider_segment_count, slider_point_count, p,
                                         file_end, constants, time_offset);
#endif
}

}  // namespace fosu::internal
