#pragma once

// Event ordering and endpoint exclusion follow ppy's SliderEventGenerator.
// Copyright (c) ppy Pty Ltd <contact@ppy.sh>; MIT licence in slider_geometry.h.
#include <fosu/slider_timing.h>

namespace fosu::internal {

inline bool set_slider_events(Beatmap& map, Arena* arena) {
  if (map.sliders.empty())
    return true;
  auto* timings = arena_push_array<SliderTiming>(arena, map.sliders.size());
  auto* ranges = arena_push_array<std::span<SliderEvent>>(arena, map.sliders.size());
  if (!timings || !ranges ||
      !set_slider_end_times(map, arena, {timings, map.sliders.size()}))
    return false;
  size_t remaining = 1u << 20;  // Bound pathological repeat/tick expansion.
  for (const auto& object : map.hit_objects) {
    if (object.slider == HitObject::kNoSlider)
      continue;
    const auto& path = map.slider_paths[object.slider];
    const auto& timing = timings[object.slider];
    const double length = std::min(100000.0, path.distance());
    const double tick_distance = std::clamp(timing.tick_distance, 0.0, length);
    const double minimum_from_end = timing.velocity * 10;
    size_t tick_count = 0;
    // Count with the same repeated addition used to place ticks, avoiding a
    // division/rounding disagreement at an exact tick or exclusion boundary.
    if (tick_distance > 0)
      for (double d = tick_distance; d <= length && d < length - minimum_from_end;
           d += tick_distance)
        ++tick_count;
    const size_t count = 2 + static_cast<size_t>(timing.spans) * (tick_count + 1);
    if (count > remaining)
      return false;
    remaining -= count;
    auto* events = arena_push_array<SliderEvent>(arena, count);
    if (!events)
      return false;
    size_t next = 0;
    auto event = [&](SliderEventType type, double time, int span, double progress) {
      return SliderEvent{type,     time,
                         span,     object.time + span * timing.span_duration,
                         progress, slider_position_at(path, progress)};
    };
    events[next++] = event(SliderEventType::Head, object.time, 0, 0);
    for (int span = 0; span < timing.spans; ++span) {
      const double start = object.time + span * timing.span_duration;
      double d = tick_distance;
      for (size_t tick = 0; tick < tick_count; ++tick, d += tick_distance) {
        const double progress = d / length;
        const double time_progress = span % 2 ? 1 - progress : progress;
        const size_t index = span % 2 ? tick_count - tick - 1 : tick;
        events[next + index] =
            event(SliderEventType::Tick, start + time_progress * timing.span_duration,
                  span, progress);
      }
      next += tick_count;
      if (span < timing.spans - 1)
        events[next++] = event(SliderEventType::Repeat, start + timing.span_duration,
                               span, (span + 1) % 2);
    }
    const int final_span = timing.spans - 1;
    const double final_span_start = object.time + final_span * timing.span_duration;
    const double duration = timing.spans * timing.span_duration;
    const double legacy_time = std::max(object.time + duration / 2,
                                        final_span_start + timing.span_duration - 36);
    double legacy_progress;
    if (timing.span_duration == 0) {
      legacy_progress = timing.spans % 2;
    } else {
      legacy_progress = (legacy_time - final_span_start) / timing.span_duration;
      if (timing.spans % 2 == 0)
        legacy_progress = 1 - legacy_progress;
    }
    events[next++] =
        event(SliderEventType::LegacyLastTick, legacy_time, final_span, legacy_progress);
    events[next++] =
        event(SliderEventType::Tail, object.end_time, final_span, timing.spans % 2);
    ranges[object.slider] = {events, count};
  }
  map.slider_events = {ranges, map.sliders.size()};
  return true;
}

}  // namespace fosu::internal
