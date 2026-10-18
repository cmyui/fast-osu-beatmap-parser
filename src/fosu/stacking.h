#pragma once

// Copyright (c) ppy Pty Ltd <contact@ppy.sh>; MIT licence in slider_geometry.h.

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/slider_path.h>
#include <fosu/stable_sliders.h>
#include <fosu/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace fosu::internal {

FOSU_FP_CONTRACT_OFF_BEGIN

inline PathPoint object_position(const HitObject& object) {
  return {static_cast<f32>(object.x), static_cast<f32>(object.y)};
}

inline PathPoint slider_end_position(const Beatmap&   map,
                                     const HitObject& object,
                                     bool             include_repeats = true) {
  const auto position = object_position(object);
  if (object.slider == HitObject::kNoSlider)
    return position;
  const auto& path = map.slider_paths[object.slider];
  const auto& slider = map.sliders[object.slider];
  const i32   spans = path.distance() <= 1e-7 ? 1 : slider.slides;
  return position + slider_position_at(path, include_repeats ? spans % 2 : 1);
}

inline bool within_stack_distance(PathPoint a, PathPoint b) {
  return (a - b).length() < 3;
}

inline f64 stacking_end_time(const HitObject&     object,
                             std::span<const f64> slider_end_times) {
  return object.slider == HitObject::kNoSlider
             ? object.end_time
             : slider_end_times[object.slider];
}

inline void calculate_modern_stacks(Beatmap&             map,
                                    f32                  threshold,
                                    std::span<const f64> slider_end_times) {
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
      if (previous.is_spinner() || previous.is_hold()) [[unlikely]]
        continue;
      // Stable truncates circle comparisons to integer timestamps. Use trunc
      // rather than an out-of-range integer conversion on malformed extremes.
      const f64 elapsed =
          circle ? std::trunc(object.time) -
                       std::trunc(stacking_end_time(previous, slider_end_times))
                 : object.time - previous.time;
      if (elapsed > threshold)
        break;
      if (circle && previous.is_slider() &&
          within_stack_distance(slider_end_position(map, previous),
                                object_position(object))) {
        const i32 offset = map.stacking[current].stack_height -
                           map.stacking[n].stack_height + 1;
        for (size_t j = n + 1; j <= i; ++j)
          if (within_stack_distance(slider_end_position(map, previous),
                                    object_position(map.hit_objects[j])))
            map.stacking[j].stack_height -= offset;
        break;
      }
      const auto position = circle ? object_position(previous)
                                   : slider_end_position(map, previous);
      if (within_stack_distance(position, object_position(object))) {
        map.stacking[n].stack_height = map.stacking[current].stack_height + 1;
        current = n;
      }
    }
  }
}

inline void calculate_legacy_stacks(Beatmap&             map,
                                    f32                  threshold,
                                    std::span<const f64> slider_end_times) {
  for (size_t i = 0; i < map.hit_objects.size(); ++i) {
    const auto& object = map.hit_objects[i];
    if (map.stacking[i].stack_height != 0 && !object.is_slider())
      continue;
    f64        end = stacking_end_time(object, slider_end_times);
    i32        slider_stack = 0;
    const auto tail = slider_end_position(map, object, false);
    for (size_t j = i + 1; j < map.hit_objects.size(); ++j) {
      const auto& next = map.hit_objects[j];
      if (next.time - threshold > end)
        break;
      if (within_stack_distance(object_position(next),
                                object_position(object))) {
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

inline void move_stacked_objects(Beatmap& map) {
  for (size_t i = 0; i < map.hit_objects.size(); ++i) {
    auto&      object = map.hit_objects[i];
    const auto offset = map.stacking[i].stack_offset;
    object.x += offset.x;
    object.y += offset.y;
    if (object.slider == HitObject::kNoSlider)
      continue;
    const auto& slider = map.sliders[object.slider];
    for (auto& point :
         map.slider_points.subspan(slider.point_begin, slider.point_count)) {
      point.x += offset.x;
      point.y += offset.y;
    }
  }
}

inline bool apply_stacking(Beatmap&             map,
                           Arena*               result_arena,
                           std::span<const f64> slider_end_times) {
  if (map.mode != 0 || map.hit_objects.empty())
    return true;
  auto* stacking =
      arena_push_array<Stacking>(result_arena, map.hit_objects.size());
  if (!stacking)
    return false;
  std::fill_n(stacking, map.hit_objects.size(), Stacking{});
  map.stacking = {stacking, map.hit_objects.size()};
  // Difficulty is finalized and unmodded, so these are constant for the map.
  const f64 ar = static_cast<f32>(map.ar);
  const f64 preempt = 1200 + (ar > 5 ? -750 : -600) * ((ar - 5) / 5);
  const f32 threshold =
      static_cast<i32>(preempt) * static_cast<f32>(map.stack_leniency);
  if (map.format_version >= 6)
    calculate_modern_stacks(map, threshold, slider_end_times);
  else
    calculate_legacy_stacks(map, threshold, slider_end_times);
  const f64 cs = static_cast<f32>(map.cs);
  const f32 scale =
      static_cast<f32>(1.0f - 0.7f * ((cs - 5) / 5)) / 2 * 1.00041f;
  for (size_t i = 0; i < map.stacking.size(); ++i) {
    auto& stack = map.stacking[i];
    // The standard ruleset's spinner overrides StackOffset with Vector2.Zero,
    // even when legacy stacking assigned it a nonzero height.
    if (map.hit_objects[i].is_spinner() || map.hit_objects[i].is_hold())
        [[unlikely]]
      continue;
    const f32 offset = stack.stack_height * scale * -6.4f;
    stack.stack_offset = {offset, offset};
  }
  // Calculate every height against unstacked geometry before moving anything.
  move_stacked_objects(map);
  return true;
}

// osu!stable's stacking (UpdateStacking from v6, ApplyOldStacking before),
// on its whole-millisecond times and its own slider ends. Stable plays holds
// in an osu!standard map as circles.
struct StableStackObject {
  enum Kind : u8 { Circle, Slider, Spinner } kind;
  i32       time;      // without the pre-v5 offset
  i32       end_time;  // the same
  PathPoint position;
  PathPoint end_position;  // after the last span
  PathPoint path_end;      // the far end of a slider's path
};

inline void calculate_stable_stacks(std::span<const StableStackObject> objects,
                                    std::span<Stacking>                stacks,
                                    f32 threshold) {
  const auto count = static_cast<ptrdiff_t>(objects.size());
  const auto is = [&](ptrdiff_t i, StableStackObject::Kind kind) {
    return objects[i].kind == kind;
  };
  const auto beyond = [&](i32 later, i32 earlier) {
    return static_cast<f32>(wrapping_sub(later, earlier)) > threshold;
  };
  // Stable first reaches forward to include objects stacked on the range it
  // updates. Over a whole map that changes nothing, so only its backward pass
  // remains: each stack built up from its last object.
  for (ptrdiff_t i = count - 1; i > 0; --i) {
    ptrdiff_t n = i;
    ptrdiff_t top = i;
    if (stacks[top].stack_height != 0 || is(top, StableStackObject::Spinner))
      continue;
    if (is(top, StableStackObject::Circle)) {
      while (--n >= 0) {
        if (is(n, StableStackObject::Spinner))
          continue;
        if (beyond(objects[top].time, objects[n].end_time))
          break;
        // Circles under the end of the last slider in a stack go down and
        // right of it instead.
        if (is(n, StableStackObject::Slider) &&
            within_stack_distance(objects[n].end_position,
                                  objects[top].position)) {
          const i32 offset =
              stacks[top].stack_height - stacks[n].stack_height + 1;
          for (ptrdiff_t j = n + 1; j <= i; ++j)
            if (within_stack_distance(objects[n].end_position,
                                      objects[j].position))
              stacks[j].stack_height -= offset;
          break;
        }
        if (within_stack_distance(objects[n].position, objects[top].position)) {
          stacks[n].stack_height = stacks[top].stack_height + 1;
          top = n;
        }
      }
    } else if (is(top, StableStackObject::Slider)) {
      while (--n >= 0) {
        if (is(n, StableStackObject::Spinner))
          continue;
        if (beyond(objects[top].time, objects[n].time))
          break;
        if (within_stack_distance(objects[n].end_position,
                                  objects[top].position)) {
          stacks[n].stack_height = stacks[top].stack_height + 1;
          top = n;
        }
      }
    }
  }
}

inline void calculate_stable_old_stacks(
    std::span<const StableStackObject> objects,
    std::span<Stacking>                stacks,
    f32                                threshold) {
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto& current = objects[i];
    i32         end = current.end_time;
    if (stacks[i].stack_height != 0 &&
        current.kind != StableStackObject::Slider)
      continue;
    i32 slider_stack = 0;
    for (size_t j = i + 1; j < objects.size(); ++j) {
      if (!(static_cast<f32>(objects[j].time) - threshold <= end))
        break;
      // Stable works out each slider's path as it reaches it, so later
      // sliders still end when they start.
      const i32 later_end = objects[j].kind == StableStackObject::Slider
                                ? objects[j].time
                                : objects[j].end_time;
      if (within_stack_distance(objects[j].position, current.position)) {
        ++stacks[i].stack_height;
        end = later_end;
      } else if (within_stack_distance(objects[j].position, current.path_end)) {
        stacks[j].stack_height -= ++slider_stack;
        end = later_end;
      }
    }
  }
}

inline bool apply_stable_stacking(Beatmap&                         map,
                                  std::span<const StableSliderEnd> slider_ends,
                                  Arena*                           result_arena,
                                  Arena* scratch_arena) {
  if (map.mode != 0 || map.hit_objects.empty())
    return true;
  const size_t count = map.hit_objects.size();
  auto*        stacking = arena_push_array<Stacking>(result_arena, count);
  if (!stacking)
    return false;
  std::fill_n(stacking, count, Stacking{});
  map.stacking = {stacking, count};
  const TempArena temp{scratch_arena};
  auto* objects = arena_push_array<StableStackObject>(scratch_arena, count);
  if (!objects)
    return false;
  const f64 shift = map.format_version < 5 ? 24 : 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& object = map.hit_objects[i];
    const auto  position = object_position(object);
    const auto  time = stable_int(object.time - shift);
    if (object.is_slider()) {
      const auto& end = slider_ends[object.slider];
      objects[i] = {StableStackObject::Slider,
                    time,
                    stable_int(object.end_time - shift),
                    position,
                    end.end_position,
                    end.path_end};
    } else if (object.is_spinner()) {
      objects[i] = {StableStackObject::Spinner,
                    time,
                    stable_int(object.end_time - shift),
                    position,
                    position,
                    position};
    } else {
      objects[i] = {
          StableStackObject::Circle, time, time, position, position, position};
    }
  }
  // Difficulty is final, with any mods applied.
  const i32 preempt =
      stable_int(map.ar > 5   ? 1200 + (450 - 1200) * (map.ar - 5) / 5
                 : map.ar < 5 ? 1200 - (1200 - 1800) * (5 - map.ar) / 5
                              : 1200);
  const f32 threshold =
      static_cast<f32>(preempt) * static_cast<f32>(map.stack_leniency);
  const std::span<const StableStackObject> stack_objects{objects, count};
  if (map.format_version >= 6)
    calculate_stable_stacks(stack_objects, map.stacking, threshold);
  else
    calculate_stable_old_stacks(stack_objects, map.stacking, threshold);
  // Stable's stack offset is a tenth of its circle radius, which it works
  // out in window pixels; this is a 640x480 window's.
  const f32 size =
      static_cast<f32>(512.0f / 8.0f * (1.0f - 0.7f * ((map.cs - 5) / 5)));
  const f32 offset = size / 2.0f * 1.00041f / 10;
  for (size_t i = 0; i < count; ++i) {
    auto& stack = map.stacking[i];
    // Stable's spinners never move.
    if (map.hit_objects[i].is_spinner()) [[unlikely]]
      continue;
    const f32 shift_by = -(static_cast<f32>(stack.stack_height) * offset);
    stack.stack_offset = {shift_by, shift_by};
  }
  move_stacked_objects(map);
  return true;
}

FOSU_FP_CONTRACT_OFF_END

}  // namespace fosu::internal
