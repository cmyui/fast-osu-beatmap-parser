#pragma once

#include <algorithm>
#include <cstdint>

#include <fosu/beatmap.h>

namespace fosu {

enum class Mods : uint32_t {
  None = 0,
  Easy = 1u << 1,
  HardRock = 1u << 4,
  DoubleTime = 1u << 6,
  HalfTime = 1u << 8,
  Nightcore = 1u << 9,
};

constexpr Mods operator|(Mods left, Mods right) {
  return static_cast<Mods>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool has_mod(Mods mods, Mods mod) {
  return (static_cast<uint32_t>(mods) & static_cast<uint32_t>(mod)) != 0;
}

namespace internal {

inline constexpr uint32_t kKnownMods =
    static_cast<uint32_t>(Mods::Easy) | static_cast<uint32_t>(Mods::HardRock) |
    static_cast<uint32_t>(Mods::DoubleTime) | static_cast<uint32_t>(Mods::HalfTime) |
    static_cast<uint32_t>(Mods::Nightcore);

constexpr bool invalid_mods(Mods mods) {
  const uint32_t value = static_cast<uint32_t>(mods);
  const bool speed_up = has_mod(mods, Mods::DoubleTime) || has_mod(mods, Mods::Nightcore);
  return (value & ~kKnownMods) ||
         (has_mod(mods, Mods::Easy) && has_mod(mods, Mods::HardRock)) ||
         (speed_up && has_mod(mods, Mods::HalfTime));
}

constexpr bool has_difficulty_mod(Mods mods) {
  return has_mod(mods, Mods::Easy) || has_mod(mods, Mods::HardRock);
}

constexpr double clock_rate(Mods mods) {
  if (has_mod(mods, Mods::DoubleTime) || has_mod(mods, Mods::Nightcore))
    return 1.5;
  if (has_mod(mods, Mods::HalfTime))
    return 0.75;
  return 1;
}

inline bool apply_mods_before_calculations(Beatmap& map, Mods mods) {
  const bool easy = has_mod(mods, Mods::Easy);
  const bool hard_rock = has_mod(mods, Mods::HardRock);
  if (!easy && !hard_rock)
    return true;
  if (map.mode == 2 || map.mode == 3)
    return false;

  if (easy) {
    map.hp *= 0.5;
    map.cs *= 0.5;
    map.od *= 0.5;
    map.ar *= 0.5;
    if (map.mode == 1)
      map.slider_multiplier *= 0.8;
    return true;
  }

  map.hp = std::min(map.hp * 1.4, 10.0);
  map.od = std::min(map.od * 1.4, 10.0);
  if (map.mode == 0) {
    map.cs = std::min(map.cs * 1.3, 10.0);
    map.ar = std::min(map.ar * 1.4, 10.0);
    for (auto& object : map.hit_objects)
      object.y = 384 - object.y;
    for (auto& point : map.slider_points)
      point.y = 384 - point.y;
  } else {
    map.slider_multiplier *= 1.4 * 4 / 3;
  }
  return true;
}

inline void apply_clock_rate(Beatmap& map, Mods mods) {
  const double rate = clock_rate(mods);
  if (rate == 1)
    return;
  for (auto& object : map.hit_objects) {
    object.time /= rate;
    object.end_time /= rate;
  }
  for (auto& point : map.timing_points) {
    point.time /= rate;
    if (point.uninherited)
      point.beat_length /= rate;
  }
  for (auto& period : map.breaks) {
    period.start /= rate;
    period.end /= rate;
  }
  for (auto events : map.slider_events)
    for (auto& event : events) {
      event.time /= rate;
      event.span_start_time /= rate;
    }
}

}  // namespace internal
}  // namespace fosu
