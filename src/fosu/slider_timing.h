#pragma once

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/parse_options.h>
#include <fosu/slider_geometry.h>
#include <fosu/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace fosu::internal {

FOSU_FP_CONTRACT_OFF_BEGIN

struct SliderTimingChange {
  f64    time;
  f64    beat_length;  // NaN means this group does not change the beat length.
  f64    velocity;
  size_t input_order;
  bool   generate_ticks;
};

struct SliderTiming {
  f64 velocity;
  f64 tick_distance;
  f64 span_duration;
  i32 spans;
};

inline bool set_slider_end_times(Beatmap&                map,
                                 Client                  client,
                                 Arena*                  result_arena,
                                 Arena*                  scratch_arena,
                                 std::span<SliderTiming> timings = {}) {
  if (map.sliders.empty())
    return true;
  auto* velocities = arena_push_array<f64>(result_arena, map.sliders.size());
  if (!velocities)
    return false;
  map.slider_velocities = {velocities, map.sliders.size()};
  const TempArena temp{scratch_arena};
  auto*           changes = arena_push_array<SliderTimingChange>(
      scratch_arena, map.timing_points.size());
  if (!changes && !map.timing_points.empty())
    return false;
  size_t count = 0;
  for (size_t i = 0; i < map.timing_points.size();) {
    const f64          time = map.timing_points[i].time;
    const TimingPoint* red = nullptr;
    const TimingPoint* green = nullptr;
    do {
      const auto& point = map.timing_points[i++];
      if (point.uninherited) {
        if (!red)
          red = &point;
      } else {
        green = &point;
      }
    } while (i < map.timing_points.size() && map.timing_points[i].time == time);
    const auto& speed = green ? *green : *red;
    changes[count++] = {
        time,
        red ? std::clamp(red->beat_length, 6.0, 60000.0)
            : std::numeric_limits<f64>::quiet_NaN(),
        speed.beat_length < 0
            ? std::clamp(100.0 / -speed.beat_length, 0.1, 10.0)
            : 1,
        i,
        !std::isnan(speed.beat_length),
    };
  }
  const bool ordered = std::is_sorted(changes, changes + count,
                                      [](const auto& a, const auto& b) {
                                        return a.time < b.time;
                                      });
  if (!ordered) {
    // osu! discards redundant velocity changes as it reads them. Sorting first
    // would revive a discarded point if a later line changes an earlier time.
    // Keep this input-order resolution off the usual ordered-map path.
    for (size_t i = 0; i < count; ++i) {
      f64  latest = -std::numeric_limits<f64>::infinity();
      f64  velocity = 1;
      bool ticks = true;
      for (size_t j = 0; j < i; ++j) {
        if (!std::isnan(changes[j].velocity) &&
            changes[j].time <= changes[i].time && changes[j].time >= latest) {
          latest = changes[j].time;
          velocity = changes[j].velocity;
          ticks = changes[j].generate_ticks;
        }
      }
      if (changes[i].velocity == velocity && changes[i].generate_ticks == ticks)
        changes[i].velocity = std::numeric_limits<f64>::quiet_NaN();
    }
    std::sort(changes, changes + count, [](const auto& a, const auto& b) {
      return a.time < b.time ||
             (a.time == b.time && a.input_order < b.input_order);
    });
  }
  f64  beat_length = 1000, velocity = 1;
  bool generate_ticks = true;
  // The first red point supplies BPM even to objects preceding it. Green
  // points, unlike red points, never apply before their timestamp.
  for (size_t i = 0; i < count; ++i) {
    if (!std::isnan(changes[i].beat_length)) {
      beat_length = changes[i].beat_length;
      for (size_t j = i + 1; j < count && changes[j].time == changes[i].time;
           ++j)
        if (!std::isnan(changes[j].beat_length))
          beat_length = changes[j].beat_length;
      break;
    }
  }
  const bool standard = uses_lazer_standard_sliders(map, client);
  size_t     next = 0;
  for (auto& object : map.hit_objects) {
    if (object.slider == HitObject::kNoSlider)
      continue;
    while (next < count && changes[next].time <= object.time) {
      if (!std::isnan(changes[next].beat_length))
        beat_length = changes[next].beat_length;
      if (!std::isnan(changes[next].velocity)) {
        velocity = changes[next].velocity;
        generate_ticks = changes[next].generate_ticks;
      }
      ++next;
    }
    const auto& slider = map.sliders[object.slider];
    f64         distance;
    if (!map.slider_paths.empty()) {
      distance = map.slider_paths[object.slider].distance();
    } else if (standard && has_catmull_segment(map, slider) &&
               !reaches_declared_length(map, object)) {
      // osu! counts what Catmull simplification leaves out before the rest
      // of the path, so the distance comes from the path, released again.
      const size_t mark = arena_pos(result_arena);
      const auto path = calculate_slider_path(map, client, object, result_arena,
                                              scratch_arena);
      if (!path)
        return false;
      distance = path->distance();
      arena_pop_to(result_arena, mark);
    } else {
      distance = slider_distance(map, client, object, scratch_arena);
    }
    f64 pixels_per_millisecond =
        100 * map.slider_multiplier * velocity / beat_length;
    if (standard) {
      // osu!standard's Slider takes its velocity from the beat length with
      // the slider velocity rounded to single precision, as osu!stable does.
      const f64 bpm_multiplier =
          std::clamp(static_cast<f32>(100 / velocity), 10.0f, 1000.0f) / 100.0;
      pixels_per_millisecond =
          100 * map.slider_multiplier / (beat_length * bpm_multiplier);
    }
    // osu! suppresses repeats on effectively zero-length paths. Keep the
    // encoded span count on Slider, but use the effective count for duration.
    const i32 spans = distance <= 1e-7 ? 1 : slider.slides;
    object.end_time = object.time + spans * distance / pixels_per_millisecond;
    velocities[object.slider] = pixels_per_millisecond;
    if (!timings.empty()) {
      const f64 tick_distance =
          generate_ticks
              ? pixels_per_millisecond * beat_length / map.slider_tick_rate *
                    (map.format_version < 8 ? 1 / velocity : 1)
              : std::numeric_limits<f64>::infinity();
      // osu!standard's Slider spans its duration as rounded into the end time.
      const f64 duration = standard ? object.end_time - object.time
                                    : spans * distance / pixels_per_millisecond;
      timings[object.slider] = {pixels_per_millisecond, tick_distance,
                                duration / spans, spans};
    }
  }
  return true;
}

FOSU_FP_CONTRACT_OFF_END

}  // namespace fosu::internal
