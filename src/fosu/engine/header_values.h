#pragma once

#include <fosu/engine/byte_scan.h>
#include <fosu/engine/enum_parse.h>
#include <fosu/engine/scalar_parse.h>
#include <fosu/engine/string_lookup.h>
#include <optional>

namespace fosu::internal {

inline bool consumed_header_value(std::string_view input, const char* next) {
  if (next == input.data())
    return false;
  const char* end = input.data() + input.size();
  while (next < end && (*next == ' ' || *next == '\t'))
    ++next;
  return next == end;
}

inline std::optional<int32_t> parse_header_integer(std::string_view input) {
  int64_t value;
  if (!consumed_header_value(
          input, parse_osu_int(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  return static_cast<int32_t>(value);
}

// Preserve the encoded double precision, while validating the legacy float
// domain used by difficulty settings and StackLeniency.
inline std::optional<double> parse_header_float(std::string_view input) {
  double value;
  if (!consumed_header_value(
          input, parse_double(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  if (value < -2147483520.0 || value > 2147483520.0) {
    float checked;
    if (!consumed_header_value(
            input, parse_osu_float(input.data(), input.data() + input.size(), checked)))
      return std::nullopt;
  }
  return value;
}

inline std::optional<double> parse_header_double(std::string_view input) {
  double value;
  if (!consumed_header_value(
          input, parse_double(input.data(), input.data() + input.size(), value)) ||
      value < -INT32_MAX || value > INT32_MAX)
    return std::nullopt;
  return value;
}

inline std::optional<bool> parse_header_boolean(std::string_view input) {
  if (const auto value = parse_header_integer(input))
    return *value == 1;
  return std::nullopt;
}

inline std::optional<int32_t> parse_mode(std::string_view input) {
  const auto value = parse_header_integer(input);
  if (!value || *value < 0 || *value > 3)
    return std::nullopt;
  return value;
}

// Legacy Enum.Parse accepts names, comma-separated combinations, and the full
// int32 range. Individual domains can impose stricter rules on that spelling.
inline std::optional<int32_t> parse_legacy_enum(std::string_view input,
                                                const StringLookup<int32_t, 4>& names) {
  const char* p = skip_numeric_space(input.data(), input.data() + input.size());
  const char* end = input.data() + input.size();
  while (end > p && skip_numeric_space(end - 1, end) == end)
    --end;
  int64_t number;
  const char* q = parse_i64(p, end, number);
  if (q != p && q == end && number >= INT32_MIN && number <= INT32_MAX)
    return static_cast<int32_t>(number);
  int32_t value = 0;
  do {
    const auto* comma = find_byte<','>(p, end);
    const char* part_end = comma;
    while (part_end > p && skip_numeric_space(part_end - 1, part_end) == part_end)
      --part_end;
    const auto* named_value = names.find({p, static_cast<size_t>(part_end - p)});
    if (!named_value)
      return std::nullopt;
    value |= *named_value;
    if (comma == end)
      return value;
    p = skip_numeric_space(comma + 1, end);
  } while (p < end);
  return std::nullopt;
}

inline std::optional<int32_t> parse_countdown(std::string_view input) {
  static constexpr auto names = make_string_lookup<int32_t>({
      {"None", 0},
      {"Normal", 1},
      {"HalfSpeed", 2},
      {"DoubleSpeed", 3},
  });
  return parse_legacy_enum(input, names);
}

inline std::optional<SampleSet> parse_header_sample_set(std::string_view input) {
  static constexpr auto names = make_string_lookup<int32_t>({
      {"None", 0},
      {"Normal", 1},
      {"Soft", 2},
      {"Drum", 3},
  });
  // Sample sets are choices, not flags.
  if (input.find(',') != std::string_view::npos)
    return std::nullopt;
  const auto value = parse_legacy_enum(input, names);
  return value ? parse_sample_set(*value) : std::nullopt;
}

}  // namespace fosu::internal
