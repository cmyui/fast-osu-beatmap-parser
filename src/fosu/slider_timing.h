#pragma once

#include <fosu/slider_geometry.h>
#include <limits>

namespace fosu::internal {

struct SliderTimingChange {
  double time;
  double beat_length;  // NaN means this group does not change the beat length.
  double velocity;
  size_t input_order;
  bool generate_ticks;
};

inline bool set_slider_end_times(Beatmap& map, Arena* arena) {
  if (map.sliders.empty())
    return true;
  const auto temp = temp_begin(arena);
  auto* changes = arena_push_array<SliderTimingChange>(arena, map.timing_points.size());
  if (!changes && !map.timing_points.empty())
    return false;
  size_t count = 0;
  for (size_t i = 0; i < map.timing_points.size();) {
    const double time = map.timing_points[i].time;
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
            : std::numeric_limits<double>::quiet_NaN(),
        speed.beat_length < 0 ? std::clamp(100.0 / -speed.beat_length, 0.1, 10.0) : 1,
        i,
        !std::isnan(speed.beat_length),
    };
  }
  const bool ordered =
      std::is_sorted(changes, changes + count, [](const auto& a, const auto& b) {
        return a.time < b.time;
      });
  if (!ordered) {
    // osu! discards redundant velocity changes as it reads them. Sorting first
    // would revive a discarded point if a later line changes an earlier time.
    // Keep this input-order resolution off the usual ordered-map path.
    for (size_t i = 0; i < count; ++i) {
      double latest = -std::numeric_limits<double>::infinity();
      double velocity = 1;
      bool ticks = true;
      for (size_t j = 0; j < i; ++j) {
        if (!std::isnan(changes[j].velocity) && changes[j].time <= changes[i].time &&
            changes[j].time >= latest) {
          latest = changes[j].time;
          velocity = changes[j].velocity;
          ticks = changes[j].generate_ticks;
        }
      }
      if (changes[i].velocity == velocity && changes[i].generate_ticks == ticks)
        changes[i].velocity = std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(changes, changes + count, [](const auto& a, const auto& b) {
      return a.time < b.time || (a.time == b.time && a.input_order < b.input_order);
    });
  }
  double beat_length = 1000, velocity = 1;
  // The first red point supplies BPM even to objects preceding it. Green
  // points, unlike red points, never apply before their timestamp.
  for (size_t i = 0; i < count; ++i) {
    if (!std::isnan(changes[i].beat_length)) {
      beat_length = changes[i].beat_length;
      for (size_t j = i + 1; j < count && changes[j].time == changes[i].time; ++j)
        if (!std::isnan(changes[j].beat_length))
          beat_length = changes[j].beat_length;
      break;
    }
  }
  size_t next = 0;
  for (auto& object : map.hit_objects) {
    if (object.slider == HitObject::kNoSlider)
      continue;
    while (next < count && changes[next].time <= object.time) {
      if (!std::isnan(changes[next].beat_length))
        beat_length = changes[next].beat_length;
      if (!std::isnan(changes[next].velocity))
        velocity = changes[next].velocity;
      ++next;
    }
    const auto& slider = map.sliders[object.slider];
    auto distance = !map.slider_paths.empty()
                        ? Result<double>{map.slider_paths[object.slider].distance()}
                        : slider_distance(object, slider,
                                          map.slider_points.subspan(slider.point_begin,
                                                                    slider.point_count),
                                          arena);
    if (!distance) {
      temp_end(temp);
      return false;
    }
    const double pixels_per_millisecond =
        100 * map.slider_multiplier * velocity / beat_length;
    // osu! suppresses repeats on effectively zero-length paths. Keep the
    // encoded span count on Slider, but use the effective count for duration.
    const int spans = distance.value() <= 1e-7 ? 1 : slider.slides;
    object.end_time = object.time + spans * distance.value() / pixels_per_millisecond;
  }
  temp_end(temp);
  return true;
}

}  // namespace fosu::internal
