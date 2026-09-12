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

inline HitObject make_hitobject(const HitObjectPrefix& prefix) {
  return HitObject{
      .x = static_cast<float>(prefix.x),
      .y = static_cast<float>(prefix.y),
      .type = prefix.type,
      .hitsound = prefix.hit_sound,
      .time = prefix.time,
      .end_time = 0,
      .slider = HitObject::kNoSlider,
      .new_combo = false,
      .combo_skip = 0,
      .hit_sample = {},
  };
}

inline HitObject make_hitobject(const ParsedScalarHitObjectPrefix& prefix) {
  return HitObject{
      .x = prefix.x,
      .y = prefix.y,
      .type = prefix.type,
      .hitsound = prefix.hit_sound,
      .time = prefix.time,
      .end_time = 0,
      .slider = HitObject::kNoSlider,
      .new_combo = false,
      .combo_skip = 0,
      .hit_sample = {},
  };
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

inline std::optional<HitObject> parse_legacy_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_point_count,
    HitObject object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return std::nullopt;

  const auto first_curve = parse_curve_type(p, end);
  if (!first_curve)
    return std::nullopt;
  p = first_curve->next;
  const size_t slider_point_begin = slider_point_count;
  p = parse_legacy_slider_points(beatmap, slider_point_count, p, end, constants);
  if (!p) {
    slider_point_count = slider_point_begin;
    return std::nullopt;
  }

  const auto tail = parse_slider_tail(p, end, constants);
  if (!tail) [[unlikely]]
    return std::nullopt;

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
  return object;
}

inline std::optional<HitObject> parse_lazer_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return std::nullopt;

  const auto first_curve = parse_curve_type(p, end);
  if (!first_curve)
    return std::nullopt;
  const size_t slider_point_begin = slider_point_count;
  const size_t slider_segment_begin = slider_segment_count;
  const auto points = parse_lazer_slider_points(
      beatmap, slider_segment_count, slider_point_count, slider_point_begin, *first_curve,
      first_curve->next, end, constants);
  if (!points) {
    slider_point_count = slider_point_begin;
    slider_segment_count = slider_segment_begin;
    return std::nullopt;
  }

  const auto tail = parse_slider_tail(points->next, end, constants);
  if (!tail) [[unlikely]] {
    slider_segment_count = slider_segment_begin;
    return std::nullopt;
  }

  if ((points->has_explicit_segments || first_curve->degree) &&
      !append_slider_segment(beatmap, slider_segment_count, points->final_curve,
                             slider_point_begin, points->final_segment_point_begin,
                             slider_point_count)) {
    slider_point_count = slider_point_begin;
    slider_segment_count = slider_segment_begin;
    return std::nullopt;
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
  return object;
}

inline std::optional<HitObject> parse_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject object,
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
inline std::optional<HitObject> parse_hitobject_details(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    HitObject object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  switch (classify_hitobject_kind(object.type)) {
    case HitObjectKind::Circle: {
      const auto details = parse_circle_details(p, end);
      if (!details)
        return std::nullopt;
      object.end_time = 0;
      object.hit_sample = details->hit_sample;
      return object;
    }
    case HitObjectKind::Slider: {
      if (p >= end || *p != ',')
        return std::nullopt;
      return parse_slider(beatmap, slider_count, slider_segment_count, slider_point_count,
                          object, p + 1, end, constants);
    }
    case HitObjectKind::Spinner: {
      const auto details = parse_spinner_details(p, end);
      if (!details)
        return std::nullopt;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return object;
    }
    case HitObjectKind::Hold: {
      const auto details = parse_hold_details(object.time, p, end);
      if (!details)
        return std::nullopt;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return object;
    }
    case HitObjectKind::Invalid:
      return std::nullopt;
  }
  return std::nullopt;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
__attribute__((noinline)) inline std::optional<HitObject> parse_hitobject_line_scalar(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    const char* p,
    const char* line_end,
    const HitObjectParseConstants& constants) {
  const auto prefix = parse_hitobject_prefix_scalar(p, static_cast<size_t>(line_end - p),
                                                    beatmap.format_version >= 128);
  if (!prefix)
    return std::nullopt;
  ++beatmap.stats.slow_path_lines;
  return parse_hitobject_details(beatmap, slider_count, slider_segment_count,
                                 slider_point_count, make_hitobject(*prefix),
                                 prefix->next, line_end, constants);
}

inline std::optional<HitObject> parse_hitobject_line_fast(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& slider_segment_count,
    size_t& slider_point_count,
    const HitObjectPrefix& prefix,
    size_t prefix_end,
    const char* p,
    const char* line_end,
    const HitObjectParseConstants& constants) {
  HitObject object = make_hitobject(prefix);
  const auto length = static_cast<size_t>(line_end - p);
  const HitObjectKind kind = classify_hitobject_kind(prefix.type);

  if (kind == HitObjectKind::Circle) {
    if (prefix_end == length)
      return object;
    if (length - prefix_end == 9 && p[prefix_end] == ',' &&
        short_sample(p + prefix_end + 1)) {
      object.hit_sample = {p + prefix_end + 1, 8};
      return object;
    }
  } else if (kind == HitObjectKind::Slider) {
    if (prefix_end >= length || p[prefix_end] != ',')
      return std::nullopt;
    return parse_slider(beatmap, slider_count, slider_segment_count, slider_point_count,
                        object, p + prefix_end + 1, line_end, constants);
  }

  return parse_hitobject_details(beatmap, slider_count, slider_segment_count,
                                 slider_point_count, object, p + prefix_end, line_end,
                                 constants);
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
    const auto shape = classify_hitobject_prefix(nondigits, commas);
    const char after_prefix = p[shape.prefix_end];

    if (shape.ok && (shape.prefix_end == length || after_prefix == ',' ||
                     after_prefix == '\0')) [[likely]] {
      const auto prefix = decode_hitobject_prefix(ascii, zero, shape);
      std::optional<HitObject> object;
      if (prefix) [[likely]] {
        ++fast_lines;
        object = parse_hitobject_line_fast(beatmap, slider_count, slider_segment_count,
                                           slider_point_count, *prefix, shape.prefix_end,
                                           p, line_end, constants);
      } else {
        object = parse_hitobject_line_scalar(beatmap, slider_count, slider_segment_count,
                                             slider_point_count, p, line_end, constants);
      }

      if (object) {
        beatmap.hit_objects[hit_object_count] =
            normalize_hitobject(*object, beatmap, hit_object_count, time_offset);
        ++hit_object_count;
      } else [[unlikely]]
        ++malformed;
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
