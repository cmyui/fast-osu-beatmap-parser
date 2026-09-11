#pragma once

#include <algorithm>
#include <cstring>

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
__attribute__((noinline)) inline bool parse_slider(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& segment_count,
    size_t& point_count,
    HitObject& object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return false;

  const bool lazer_format = beatmap.format_version >= 128;
  const auto first_curve = parse_curve_type(p, end);
  if (!first_curve)
    return false;
  p = first_curve->next;
  const size_t point_begin = point_count;
  const size_t segment_begin = segment_count;
  size_t current_segment_begin = point_count;
  auto curve = *first_curve;
  bool segmented = false;
#if FOSU_SIMD
  if (const auto initial_points =
          try_parse_slider_point_prefix_fast<SliderPoint>(p, constants)) {
    beatmap.slider_points[point_count++] = initial_points->first;
    if (initial_points->has_second)
      beatmap.slider_points[point_count++] = initial_points->second;
    p = initial_points->next;
  }
#endif
  while (p < end && *p == '|') {
    if (lazer_format && p + 1 < end &&
        (p[1] == 'B' || p[1] == 'C' || p[1] == 'L' || p[1] == 'P')) {
      const auto next_curve = parse_curve_type(p + 1, end);
      if (!next_curve || point_count == current_segment_begin) {
        point_count = point_begin;
        segment_count = segment_begin;
        return false;
      }
      if (segment_count == beatmap.slider_segments.size()) {
        point_count = point_begin;
        segment_count = segment_begin;
        return false;
      }
      beatmap.slider_segments[segment_count++] = {
          .type = curve.type,
          .degree = curve.degree,
          .point_begin = static_cast<uint32_t>(current_segment_begin),
          .point_count = static_cast<uint32_t>(point_count - current_segment_begin),
      };
      segmented = true;
      current_segment_begin = point_count - 1;
      curve = *next_curve;
      p = next_curve->next;
      continue;
    }
    const auto point = parse_slider_point<SliderPoint>(p, end, constants, lazer_format);
    if (!point) [[unlikely]] {
      point_count = point_begin;
      segment_count = segment_begin;
      return false;
    }
    beatmap.slider_points[point_count++] = point->value;
    p = point->next;
  }

  const auto tail = parse_slider_tail(p, end, constants);
  if (!tail) [[unlikely]] {
    segment_count = segment_begin;
    return false;
  }

  if (segmented) {
    if (point_count == current_segment_begin) {
      point_count = point_begin;
      segment_count = segment_begin;
      return false;
    }
    if (segment_count == beatmap.slider_segments.size()) {
      point_count = point_begin;
      segment_count = segment_begin;
      return false;
    }
    beatmap.slider_segments[segment_count++] = {
        .type = curve.type,
        .degree = curve.degree,
        .point_begin = static_cast<uint32_t>(current_segment_begin),
        .point_count = static_cast<uint32_t>(point_count - current_segment_begin),
    };
  }

  Slider slider{
      .point_begin = static_cast<uint32_t>(point_begin),
      .point_count = static_cast<uint32_t>(point_count - point_begin),
      .segment_begin = static_cast<uint32_t>(segment_begin),
      .segment_count = static_cast<uint32_t>(segment_count - segment_begin),
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

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, or a trailing hit sample.
inline bool parse_hitobject_details(Beatmap& beatmap,
                                    size_t& slider_count,
                                    size_t& segment_count,
                                    size_t& point_count,
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
             parse_slider(beatmap, slider_count, segment_count, point_count, object,
                          p + 1, end, constants);
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
__attribute__((noinline)) inline bool parse_hitobject_line_scalar(
    Beatmap& beatmap,
    size_t& slider_count,
    size_t& segment_count,
    size_t& point_count,
    HitObject& object,
    const char* p,
    const char* line_end,
    const HitObjectParseConstants& constants) {
  const auto prefix = parse_hitobject_prefix_scalar(p, static_cast<size_t>(line_end - p));
  if (!prefix)
    return false;
  initialize_hitobject(object, prefix->value);
  ++beatmap.stats.slow_path_lines;
  return parse_hitobject_details(beatmap, slider_count, segment_count, point_count,
                                 object, prefix->next, line_end, constants);
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
    size_t& segment_count,
    size_t& point_count,
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
      HitObject object{};
      if (parse_hitobject_line_scalar(beatmap, slider_count, segment_count, point_count,
                                      object, p, line_end, constants)) {
        beatmap.hit_objects[hit_object_count] =
            normalize_hitobject(object, beatmap, hit_object_count, time_offset);
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
                                                 size_t& segment_count,
                                                 size_t& point_count,
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
    const char after_prefix = p[shape.p4];

    if (shape.ok && (shape.p4 == length || after_prefix == ',' || after_prefix == '\0'))
        [[likely]] {
      HitObject object{};
      bool accepted;
      const auto prefix = decode_hitobject_prefix(ascii, zero, shape);
      if (prefix) [[likely]] {
        initialize_hitobject(object, *prefix);
        ++fast_lines;
        const HitObjectKind kind = classify_hitobject_kind(prefix->type);
        if (kind == HitObjectKind::Circle) {
          if (shape.p4 == length) {
            object.hit_sample = {};
            accepted = true;
          } else if (length - shape.p4 == 9 && after_prefix == ',' &&
                     short_sample(p + shape.p4 + 1)) {
            object.hit_sample = {p + shape.p4 + 1, 8};
            accepted = true;
          } else {
            accepted =
                parse_hitobject_details(beatmap, slider_count, segment_count, point_count,
                                        object, p + shape.p4, line_end, constants);
          }
        } else if (kind == HitObjectKind::Slider) {
          accepted = shape.p4 < length && after_prefix == ',' &&
                     parse_slider(beatmap, slider_count, segment_count, point_count,
                                  object, p + shape.p4 + 1, line_end, constants);
        } else {
          accepted =
              parse_hitobject_details(beatmap, slider_count, segment_count, point_count,
                                      object, p + shape.p4, line_end, constants);
        }
      } else {
        accepted =
            parse_hitobject_line_scalar(beatmap, slider_count, segment_count, point_count,
                                        object, p, line_end, constants);
      }

      if (accepted) {
        beatmap.hit_objects[hit_object_count] =
            normalize_hitobject(object, beatmap, hit_object_count, time_offset);
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
        HitObject object{};
        if (parse_hitobject_line_scalar(beatmap, slider_count, segment_count, point_count,
                                        object, p, line_end, constants)) {
          beatmap.hit_objects[hit_object_count] =
              normalize_hitobject(object, beatmap, hit_object_count, time_offset);
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
                                            size_t& segment_count,
                                            size_t& point_count,
                                            const char* p,
                                            const char* file_end,
                                            int time_offset = 0) {
  const HitObjectParseConstants constants;
#if FOSU_SIMD
  return parse_hitobjects_section_simd(beatmap, hit_object_count, slider_count,
                                       segment_count, point_count, p, file_end, constants,
                                       time_offset);
#else
  return parse_hitobjects_section_scalar(beatmap, hit_object_count, slider_count,
                                         segment_count, point_count, p, file_end,
                                         constants, time_offset);
#endif
}

}  // namespace fosu::internal
