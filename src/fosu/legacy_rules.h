#pragma once

#include <fosu/beatmap.h>
#include <algorithm>

namespace fosu::internal {

// The parsing engines do not allocate. Only an out-of-order map needs this
// temporary merge buffer; equal timestamps retain their original input order.
inline bool sort_hit_objects(std::span<HitObject> objects, Arena* arena) {
  const size_t checkpoint = arena_pos(arena);
  auto* scratch = arena_push_array<HitObject>(arena, objects.size());
  if (!scratch)
    return false;
  auto* source = objects.data();
  auto* destination = scratch;
  for (size_t width = 1; width < objects.size(); width *= 2) {
    for (size_t begin = 0; begin < objects.size(); begin += width * 2) {
      const size_t middle = std::min(begin + width, objects.size());
      const size_t end = std::min(begin + width * 2, objects.size());
      std::merge(source + begin, source + middle, source + middle, source + end,
                 destination + begin, [](const HitObject& a, const HitObject& b) {
                   return a.time < b.time;
                 });
    }
    std::swap(source, destination);
  }
  if (source != objects.data())
    std::memcpy(objects.data(), source, objects.size_bytes());
  arena_pop_to(arena, checkpoint);
  return true;
}

inline bool apply_legacy_rules(Beatmap& map, Arena* arena) {
  map.hp = std::clamp(map.hp, 0.0, 10.0);
  map.cs = map.mode == 3 ? std::clamp(map.cs, 1.0, 18.0) : std::clamp(map.cs, 0.0, 10.0);
  map.od = std::clamp(map.od, 0.0, 10.0);
  map.ar = std::clamp(map.ar, 0.0, 10.0);
  map.slider_multiplier = std::clamp(map.slider_multiplier, 0.4, 3.6);
  map.slider_tick_rate = std::clamp(map.slider_tick_rate, 0.5, 8.0);
  map.distance_spacing = std::max(0.0, map.distance_spacing);
  map.beat_divisor = std::clamp(map.beat_divisor, 1, 64);
  map.timeline_zoom = std::max(0.0, map.timeline_zoom);
  for (auto& slider : map.sliders) {
    slider.slides = std::max(1, slider.slides);
    slider.length = std::max(0.0, slider.length);
  }

  const int offset = map.format_version < 5 ? 24 : 0;
  if (offset && map.preview_time != -1)
    map.preview_time =
        static_cast<int32_t>(static_cast<uint32_t>(map.preview_time) + offset);
  for (auto& point : map.timing_points)
    point.time += offset;
  for (auto& period : map.breaks) {
    period.start += offset;
    period.end = std::max(period.start, period.end + offset);
  }

  bool first = true, previous_spinner = false, ordered = true;
  double previous_time = 0;
  // osu! applies these rules in input order, before sorting by timestamp.
  for (auto& object : map.hit_objects) {
    const bool explicit_combo = object.type & 4;
    object.time += offset;
    ordered &= first || previous_time <= object.time;
    previous_time = object.time;
    object.new_combo = false;
    object.combo_skip = 0;
    if (object.is_circle() || object.is_slider()) {
      object.new_combo = first || previous_spinner || explicit_combo;
      object.combo_skip = explicit_combo ? (object.type >> 4) & 7 : 0;
      object.end_time = object.is_circle() ? object.time : 0;
      previous_spinner = false;
    } else if (object.is_spinner()) {
      object.new_combo = explicit_combo;
      object.x = 256;
      object.y = 192;
      object.end_time = std::max(object.time, object.end_time + offset);
      previous_spinner = true;
    } else {
      // The legacy hold parser clamps against the offset start, then applies
      // the offset to the end. Preserve that ordering, including pre-v5 files.
      object.end_time = std::max(object.time, object.end_time) + offset;
      previous_spinner = false;
    }
    first = false;
  }
  if (!ordered && !sort_hit_objects(map.hit_objects, arena))
    return false;

  // Breaks are sparse. Find the first later object without another full walk
  // over hitobjects. Keep the cursor to match osu! even for unordered breaks.
  auto next_object = map.hit_objects.begin();
  for (const auto& period : map.breaks) {
    next_object = std::upper_bound(next_object, map.hit_objects.end(), period.end,
                                   [](double time, const HitObject& object) {
                                     return time < object.time;
                                   });
    if (next_object == map.hit_objects.end())
      break;
    next_object->new_combo = true;
  }
  return true;
}

}  // namespace fosu::internal
