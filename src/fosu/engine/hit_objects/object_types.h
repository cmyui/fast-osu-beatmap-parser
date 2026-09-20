#pragma once

#include <fosu/engine/hit_objects/samples.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/packed_digits.h>
#include <fosu/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fosu::internal {

enum class HitObjectKind {
  Circle,
  Slider,
  Spinner,
  Hold,
  Invalid,
};

// A type value may contain several kind bits. The official decoder resolves
// them in this order while leaving combo and colour-skip bits untouched.
inline HitObjectKind classify_hitobject_kind(u32 type) {
  if (type & 1)
    return HitObjectKind::Circle;
  if (type & 2)
    return HitObjectKind::Slider;
  if (type & 8)
    return HitObjectKind::Spinner;
  if (type & 128)
    return HitObjectKind::Hold;
  return HitObjectKind::Invalid;
}

struct CircleDetails {
  std::string_view hit_sample;
};

inline std::optional<CircleDetails> parse_circle_details(const char* p,
                                                         const char* end) {
  if (p == end)
    return CircleDetails{};
  if (*p != ',')
    return std::nullopt;
  const auto sample = parse_hit_sample(p + 1, end);
  if (!sample)
    return std::nullopt;
  return CircleDetails{*sample};
}

struct TimedHitObjectDetails {
  f64              end_time;
  std::string_view hit_sample;
};

// Raw timestamps remain unshifted and unclamped.
inline std::optional<TimedHitObjectDetails> parse_spinner_details(
    const char* p,
    const char* end) {
  if (p == end || *p != ',')
    return std::nullopt;
  f64         end_time;
  const char* next = parse_osu_double(p + 1, end, end_time);
  if (next == p + 1 || (next < end && *next != ','))
    return std::nullopt;
  const auto sample = parse_hit_sample(next < end ? next + 1 : end, end);
  if (!sample)
    return std::nullopt;
  return TimedHitObjectDetails{end_time, *sample};
}

// Omitted endpoints and the ':' separator follow ConvertHitObjectParser.
inline std::optional<TimedHitObjectDetails>
parse_hold_details(f64 start_time, const char* p, const char* end) {
  if (p == end || (p + 1 == end && *p == ','))
    return TimedHitObjectDetails{start_time, {}};
  if (*p != ',')
    return std::nullopt;
  f64         end_time;
  const char* field = p + 1;
  const char* next = field;
  const u32   digits = digit_run8(field);
  if (digits && digits <= static_cast<size_t>(end - field) &&
      (field + digits == end || field[digits] == ',' || field[digits] == ':')) {
    const u64 value = swar_parse_u64(field, digits);
    if (value <= INT32_MAX) {
      end_time = static_cast<f64>(value);
      next = field + digits;
    }
  }
  if (next == field)
    next = parse_osu_double(field, end, end_time);
  if (next == p + 1 || (next < end && *next != ',' && *next != ':'))
    return std::nullopt;
  // The official decoder ignores a comma-separated value here; a hold's
  // hit sample belongs after the ':' in objectParams.
  if (next < end && *next == ',')
    return TimedHitObjectDetails{end_time, {}};
  const auto sample = parse_hit_sample(next < end ? next + 1 : end, end);
  if (!sample)
    return std::nullopt;
  return TimedHitObjectDetails{end_time, *sample};
}

}  // namespace fosu::internal
