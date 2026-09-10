#pragma once

#include <algorithm>
#include <cstring>

#include <fosu/beatmap.h>
#include <fosu/engine/hit_objects/common_fields.h>
#include <fosu/engine/hit_objects/object_types.h>
#include <fosu/engine/hit_objects/slider_fields.h>
#include <fosu/engine/parsing/arena_list.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>

namespace fosu::internal {

enum class HitObjectParseResult : uint8_t {
  Accepted,
  Malformed,
  AllocationFailure,
};

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
//
// Each point is parsed into a local value before it is appended to scratch
// storage. A malformed point list is discarded. Points parsed before a
// malformed later field remain, matching the official decoder's behavior.
__attribute__((noinline)) inline HitObjectParseResult parse_slider(
    Arena* arena,
    ArenaList<Slider>& sliders,
    ArenaList<SliderPoint>& points,
    HitObject& object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  if (p >= end) [[unlikely]]
    return HitObjectParseResult::Malformed;

  const auto curve_type = parse_curve_type(*p++);
  if (!curve_type)
    return HitObjectParseResult::Malformed;
  const auto point_position = arena_list_position(points);
  const size_t point_begin = points.count;
#if FOSU_SIMD
  if (const auto initial_points =
          try_parse_slider_point_prefix_fast<SliderPoint>(p, constants)) {
    if (!arena_list_push(arena, points, initial_points->first) ||
        (initial_points->has_second &&
         !arena_list_push(arena, points, initial_points->second))) {
      return HitObjectParseResult::AllocationFailure;
    }
    p = initial_points->next;
  }
#endif
  while (p < end && *p == '|') {
    const auto point = parse_slider_point<SliderPoint>(p, end, constants);
    if (!point) [[unlikely]] {
      arena_list_pop_to(points, point_position);
      return HitObjectParseResult::Malformed;
    }
    if (!arena_list_push(arena, points, point->value))
      return HitObjectParseResult::AllocationFailure;
    p = point->next;
  }

  const auto tail = parse_slider_tail(p, end, constants);
  if (!tail) [[unlikely]]
    return HitObjectParseResult::Malformed;

  Slider slider{
      .point_begin = static_cast<uint32_t>(point_begin),
      .point_count = static_cast<uint32_t>(points.count - point_begin),
      .slides = std::max(1, tail->slides),
      .curve_type = *curve_type,
      .length = std::max(0.0, tail->length),
      .edge_sounds = tail->sounds.edge_sounds,
      .edge_sets = tail->sounds.edge_sets,
  };
  object.hit_sample = tail->sounds.hit_sample;
  object.slider = static_cast<uint32_t>(sliders.count);
  if (!arena_list_push(arena, sliders, slider))
    return HitObjectParseResult::AllocationFailure;
  return HitObjectParseResult::Accepted;
}

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, or a trailing hit sample.
inline HitObjectParseResult parse_hitobject_details(
    Arena* arena,
    ArenaList<Slider>& sliders,
    ArenaList<SliderPoint>& points,
    HitObject& object,
    const char* p,
    const char* end,
    const HitObjectParseConstants& constants) {
  switch (classify_hitobject_kind(object.type)) {
    case HitObjectKind::Circle: {
      const auto details = parse_circle_details(p, end);
      if (!details)
        return HitObjectParseResult::Malformed;
      object.end_time = 0;
      object.hit_sample = details->hit_sample;
      return HitObjectParseResult::Accepted;
    }
    case HitObjectKind::Slider:
      return p < end && *p == ','
                 ? parse_slider(arena, sliders, points, object, p + 1, end, constants)
                 : HitObjectParseResult::Malformed;
    case HitObjectKind::Spinner: {
      const auto details = parse_spinner_details(p, end);
      if (!details)
        return HitObjectParseResult::Malformed;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return HitObjectParseResult::Accepted;
    }
    case HitObjectKind::Hold: {
      const auto details = parse_hold_details(object.time, p, end);
      if (!details)
        return HitObjectParseResult::Malformed;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return HitObjectParseResult::Accepted;
    }
    case HitObjectKind::Invalid:
      return HitObjectParseResult::Malformed;
  }
  return HitObjectParseResult::Malformed;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
__attribute__((noinline)) inline HitObjectParseResult parse_hitobject_line_scalar(
    Beatmap& beatmap,
    Arena* arena,
    ArenaList<Slider>& sliders,
    ArenaList<SliderPoint>& points,
    HitObject& object,
    const char* p,
    const char* line_end,
    const HitObjectParseConstants& constants) {
  const auto prefix = parse_hitobject_prefix_scalar(p, static_cast<size_t>(line_end - p));
  if (!prefix)
    return HitObjectParseResult::Malformed;
  initialize_hitobject(object, prefix->value);
  ++beatmap.stats.slow_path_lines;
  return parse_hitobject_details(arena, sliders, points, object, prefix->next, line_end,
                                 constants);
}

// Interpret a successfully decoded record before appending it to scratch storage.
// The preceding accepted object is still in source order, including across
// repeated HitObjects sections. No separate state crosses the engine boundary.
inline HitObject normalize_hitobject(HitObject object,
                                     const HitObject* previous,
                                     int offset) {
  const bool explicit_combo = object.type & 4;
  object.time += offset;
  object.new_combo = false;
  object.combo_skip = 0;
  if (object.is_circle() || object.is_slider()) {
    object.new_combo = !previous || explicit_combo ||
                       classify_hitobject_kind(previous->type) == HitObjectKind::Spinner;
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
    Arena* arena,
    ArenaList<HitObject>& hit_objects,
    ArenaList<Slider>& sliders,
    ArenaList<SliderPoint>& points,
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
      const auto parsed = parse_hitobject_line_scalar(beatmap, arena, sliders, points,
                                                      object, p, line_end, constants);
      if (parsed == HitObjectParseResult::Accepted) {
        object = normalize_hitobject(object, arena_list_back(hit_objects), time_offset);
        if (!arena_list_push(arena, hit_objects, object))
          return nullptr;
      } else if (parsed == HitObjectParseResult::AllocationFailure) {
        return nullptr;
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
                                                 Arena* arena,
                                                 ArenaList<HitObject>& hit_objects,
                                                 ArenaList<Slider>& sliders,
                                                 ArenaList<SliderPoint>& points,
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
      HitObjectParseResult parsed;
      const auto prefix = decode_hitobject_prefix(ascii, zero, shape);
      if (prefix) [[likely]] {
        initialize_hitobject(object, *prefix);
        ++fast_lines;
        const HitObjectKind kind = classify_hitobject_kind(prefix->type);
        if (kind == HitObjectKind::Circle) {
          if (shape.p4 == length) {
            object.hit_sample = {};
            parsed = HitObjectParseResult::Accepted;
          } else if (length - shape.p4 == 9 && after_prefix == ',' &&
                     short_sample(p + shape.p4 + 1)) {
            object.hit_sample = {p + shape.p4 + 1, 8};
            parsed = HitObjectParseResult::Accepted;
          } else {
            parsed = parse_hitobject_details(arena, sliders, points, object, p + shape.p4,
                                             line_end, constants);
          }
        } else if (kind == HitObjectKind::Slider) {
          parsed = shape.p4 < length && after_prefix == ','
                       ? parse_slider(arena, sliders, points, object, p + shape.p4 + 1,
                                      line_end, constants)
                       : HitObjectParseResult::Malformed;
        } else {
          parsed = parse_hitobject_details(arena, sliders, points, object, p + shape.p4,
                                           line_end, constants);
        }
      } else {
        parsed = parse_hitobject_line_scalar(beatmap, arena, sliders, points, object, p,
                                             line_end, constants);
      }

      if (parsed == HitObjectParseResult::Accepted) {
        object = normalize_hitobject(object, arena_list_back(hit_objects), time_offset);
        if (!arena_list_push(arena, hit_objects, object))
          return nullptr;
      } else if (parsed == HitObjectParseResult::AllocationFailure) {
        return nullptr;
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
        const auto parsed = parse_hitobject_line_scalar(beatmap, arena, sliders, points,
                                                        object, p, line_end, constants);
        if (parsed == HitObjectParseResult::Accepted) {
          object = normalize_hitobject(object, arena_list_back(hit_objects), time_offset);
          if (!arena_list_push(arena, hit_objects, object))
            return nullptr;
        } else if (parsed == HitObjectParseResult::AllocationFailure) {
          return nullptr;
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
                                            Arena* arena,
                                            ArenaList<HitObject>& hit_objects,
                                            ArenaList<Slider>& sliders,
                                            ArenaList<SliderPoint>& points,
                                            const char* p,
                                            const char* file_end,
                                            int time_offset = 0) {
  const HitObjectParseConstants constants;
#if FOSU_SIMD
  return parse_hitobjects_section_simd(beatmap, arena, hit_objects, sliders, points, p,
                                       file_end, constants, time_offset);
#else
  return parse_hitobjects_section_scalar(beatmap, arena, hit_objects, sliders, points, p,
                                         file_end, constants, time_offset);
#endif
}

}  // namespace fosu::internal
