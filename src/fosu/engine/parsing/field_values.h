#pragma once

#include <fosu/engine/parsing/numbers.h>
#include <optional>
#include <string_view>

namespace fosu::internal {

inline bool consumed_field_value(std::string_view input, const char* next) {
  if (next == input.data())
    return false;
  const char* end = input.data() + input.size();
  while (next < end && (*next == ' ' || *next == '\t'))
    ++next;
  return next == end;
}

inline std::optional<int32_t> parse_field_integer(std::string_view input) {
  int64_t value;
  if (!consumed_field_value(
          input, parse_osu_int(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  return static_cast<int32_t>(value);
}

// Decode directly to float32, as osu! does, then widen for the public storage.
inline std::optional<double> parse_field_float(std::string_view input) {
  float value;
  if (!consumed_field_value(
          input, parse_osu_float(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  return value;
}

inline std::optional<double> parse_field_double(std::string_view input) {
  double value;
  if (!consumed_field_value(
          input, parse_double(input.data(), input.data() + input.size(), value)) ||
      value < -INT32_MAX || value > INT32_MAX)
    return std::nullopt;
  return value;
}

inline std::optional<bool> parse_field_boolean(std::string_view input) {
  if (const auto value = parse_field_integer(input))
    return *value == 1;
  return std::nullopt;
}

}  // namespace fosu::internal
