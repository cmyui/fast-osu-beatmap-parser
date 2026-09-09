#pragma once

// Full-map stacking adapted from ppy's OsuBeatmapProcessor.
// Copyright (c) ppy Pty Ltd <contact@ppy.sh>; MIT licence in slider_geometry.h.
#include <fosu/beatmap.h>

namespace fosu::internal {

inline PathPoint object_position(const HitObject& object) {
  return {static_cast<float>(object.x), static_cast<float>(object.y)};
}

inline PathPoint slider_end_position(const Beatmap& map,
                                     const HitObject& object,
                                     bool include_repeats = true) {
  const auto position = object_position(object);
  if (object.slider == HitObject::kNoSlider)
    return position;
  const auto& path = map.slider_paths[object.slider];
  const auto& slider = map.sliders[object.slider];
  const int spans = path.distance() <= 1e-7 ? 1 : slider.slides;
  return position + slider_position_at(path, include_repeats ? spans % 2 : 1);
}

inline bool within_stack_distance(PathPoint a, PathPoint b) {
  return (a - b).length() < 3;
}

inline void calculate_modern_stacks(Beatmap& map, float threshold) {
  for (size_t i = map.hit_objects.size(); i-- > 1;) {
    size_t current = i;
    if (map.stacking[i].stack_height != 0 || map.hit_objects[i].is_spinner() ||
        map.hit_objects[i].is_hold())
      continue;
    const bool circle = map.hit_objects[i].is_circle();
    if (!circle && !map.hit_objects[i].is_slider())
      continue;
    for (size_t n = i; n-- > 0;) {
      const auto& previous = map.hit_objects[n];
      const auto& object = map.hit_objects[current];
      if (previous.is_spinner() || previous.is_hold())
        continue;
      // Stable truncates circle comparisons to integer timestamps. Use trunc
      // rather than an out-of-range integer conversion on malformed extremes.
      const double elapsed = circle
                                 ? std::trunc(object.time) - std::trunc(previous.end_time)
                                 : object.time - previous.time;
      if (elapsed > threshold)
        break;
      if (circle && previous.is_slider() &&
          within_stack_distance(slider_end_position(map, previous),
                                object_position(object))) {
        const int offset =
            map.stacking[current].stack_height - map.stacking[n].stack_height + 1;
        for (size_t j = n + 1; j <= i; ++j)
          if (within_stack_distance(slider_end_position(map, previous),
                                    object_position(map.hit_objects[j])))
            map.stacking[j].stack_height -= offset;
        break;
      }
      const auto position =
          circle ? object_position(previous) : slider_end_position(map, previous);
      if (within_stack_distance(position, object_position(object))) {
        map.stacking[n].stack_height = map.stacking[current].stack_height + 1;
        current = n;
      }
    }
  }
}

inline void calculate_legacy_stacks(Beatmap& map, float threshold) {
  for (size_t i = 0; i < map.hit_objects.size(); ++i) {
    const auto& object = map.hit_objects[i];
    if (map.stacking[i].stack_height != 0 && !object.is_slider())
      continue;
    double end = object.end_time;
    int slider_stack = 0;
    const auto tail = slider_end_position(map, object, false);
    for (size_t j = i + 1; j < map.hit_objects.size(); ++j) {
      const auto& next = map.hit_objects[j];
      if (next.time - threshold > end)
        break;
      if (within_stack_distance(object_position(next), object_position(object))) {
        ++map.stacking[i].stack_height;
        end = next.time;
      } else if (within_stack_distance(object_position(next), tail)) {
        map.stacking[j].stack_height -= ++slider_stack;
        // Intentional legacy behavior: the later object's start, not end.
        end = next.time;
      }
    }
  }
}

inline bool set_stacking(Beatmap& map, Arena* arena) {
  if (map.mode != 0 || map.hit_objects.empty())
    return true;
  auto* stacking = arena_push_array<Stacking>(arena, map.hit_objects.size());
  if (!stacking)
    return false;
  std::fill_n(stacking, map.hit_objects.size(), Stacking{});
  map.stacking = {stacking, map.hit_objects.size()};
  // Difficulty is finalized and unmodded, so these are constant for the map.
  const double ar = static_cast<float>(map.ar);
  const double preempt = 1200 + (ar > 5 ? -750 : -600) * ((ar - 5) / 5);
  const float threshold =
      static_cast<int>(preempt) * static_cast<float>(map.stack_leniency);
  if (map.format_version >= 6)
    calculate_modern_stacks(map, threshold);
  else
    calculate_legacy_stacks(map, threshold);
  const double cs = static_cast<float>(map.cs);
  const float scale = static_cast<float>(1.0f - 0.7f * ((cs - 5) / 5)) / 2 * 1.00041f;
  for (size_t i = 0; i < map.stacking.size(); ++i) {
    auto& stack = map.stacking[i];
    // The standard ruleset's spinner overrides StackOffset with Vector2.Zero,
    // even when legacy stacking assigned it a nonzero height.
    if (map.hit_objects[i].is_spinner() || map.hit_objects[i].is_hold())
      continue;
    const float offset = stack.stack_height * scale * -6.4f;
    stack.stack_offset = {offset, offset};
  }
  return true;
}

}  // namespace fosu::internal
