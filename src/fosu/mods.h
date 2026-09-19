#pragma once

#include <fosu/beatmap.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstdint>

namespace fosu {

enum class Mods : u32 {
  None = 0,
  NoFail = 1u << 0,
  Easy = 1u << 1,
  TouchDevice = 1u << 2,  // previously NoVideo
  Hidden = 1u << 3,
  HardRock = 1u << 4,
  SuddenDeath = 1u << 5,
  DoubleTime = 1u << 6,
  Relax = 1u << 7,
  HalfTime = 1u << 8,
  Nightcore = 1u << 9,
  Flashlight = 1u << 10,
  Autoplay = 1u << 11,
  SpunOut = 1u << 12,
  AutoPilot = 1u << 13,
  Perfect = 1u << 14,
  Key4 = 1u << 15,
  Key5 = 1u << 16,
  Key6 = 1u << 17,
  Key7 = 1u << 18,
  Key8 = 1u << 19,
  FadeIn = 1u << 20,
  Random = 1u << 21,
  Cinema = 1u << 22,
  TargetPractice = 1u << 23,
  Key9 = 1u << 24,
  KeyCoop = 1u << 25,
  Key1 = 1u << 26,
  Key3 = 1u << 27,
  Key2 = 1u << 28,
  ScoreV2 = 1u << 29,
  Mirror = 1u << 30,
};

constexpr Mods operator|(Mods left, Mods right) {
  return static_cast<Mods>(static_cast<u32>(left) | static_cast<u32>(right));
}

constexpr bool has_mod(Mods mods, Mods mod) {
  return (static_cast<u32>(mods) & static_cast<u32>(mod)) != 0;
}

namespace internal {

inline constexpr u32 kKnownMods =
    static_cast<u32>(Mods::Easy) | static_cast<u32>(Mods::HardRock) |
    static_cast<u32>(Mods::DoubleTime) | static_cast<u32>(Mods::HalfTime) |
    static_cast<u32>(Mods::Nightcore);

constexpr bool invalid_mods(Mods mods) {
  const u32  value = static_cast<u32>(mods);
  const bool speed_up =
      has_mod(mods, Mods::DoubleTime) || has_mod(mods, Mods::Nightcore);
  return (value & ~kKnownMods) ||
         (has_mod(mods, Mods::Easy) && has_mod(mods, Mods::HardRock)) ||
         (speed_up && has_mod(mods, Mods::HalfTime));
}

constexpr bool has_difficulty_mod(Mods mods) {
  return has_mod(mods, Mods::Easy) || has_mod(mods, Mods::HardRock);
}

constexpr f64 clock_rate(Mods mods) {
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
  const f64 rate = clock_rate(mods);
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
