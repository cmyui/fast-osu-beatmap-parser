#pragma once

#include <fosu/engine/parsing/numbers.h>
#include <fosu/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

inline std::optional<i32> parse_field_integer(std::string_view input) {
  if (input.empty())
    return std::nullopt;
  i64 value;
  if (!consumed_field_value(
          input,
          parse_osu_int(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  return static_cast<i32>(value);
}

inline constexpr size_t kMaxGroupedLength = 128;
using UngroupBuffer = std::array<char, 2 * kMaxGroupedLength>;

// `input` without the ',' group separators .NET's AllowThousands accepts: any
// number of them anywhere in the integer part after its first digit. The
// result is followed by readable zero bytes, as the numeric parsers require.
// Values over kMaxGroupedLength bytes keep their commas, and so fail.
inline std::string_view without_group_separators(std::string_view input,
                                                 UngroupBuffer&   buffer) {
  if (input.find(',') == std::string_view::npos ||
      input.size() > kMaxGroupedLength)
    return input;
  size_t length = 0;
  bool   digits = false, integer_part = true;
  for (const char c : input) {
    if (is_digit(c))
      digits = true;
    else if (c == '.' || c == 'e' || c == 'E')
      integer_part = false;
    else if (c == ',' && digits && integer_part)
      continue;
    buffer[length++] = c;
  }
  std::fill(buffer.begin() + length, buffer.end(), '\0');
  return {buffer.data(), length};
}

// Decode directly to float32, as osu! does, then widen for the public storage.
inline std::optional<f64> parse_float_value(std::string_view input) {
  if (input.empty())
    return std::nullopt;
  f32 value;
  if (!consumed_field_value(
          input,
          parse_osu_float(input.data(), input.data() + input.size(), value)))
    return std::nullopt;
  return value;
}

inline std::optional<f64> parse_double_value(std::string_view input) {
  if (input.empty())
    return std::nullopt;
  f64 value;
  if (!consumed_field_value(
          input,
          parse_double(input.data(), input.data() + input.size(), value)) ||
      value < -INT32_MAX || value > INT32_MAX)
    return std::nullopt;
  return value;
}

// Both clients' .NET parsers accept group separators in float and double
// fields. Maps almost never use them, so only a value that fails to parse
// without them pays to remove them.
template <auto ParseValue>
std::optional<f64> parse_grouped_value(std::string_view input) {
  if (const auto value = ParseValue(input))
    return value;
  UngroupBuffer buffer;
  const auto    ungrouped = without_group_separators(input, buffer);
  return ungrouped.data() == input.data() ? std::nullopt
                                          : ParseValue(ungrouped);
}

inline std::optional<f64> parse_field_float(std::string_view input) {
  return parse_grouped_value<parse_float_value>(input);
}

inline std::optional<f64> parse_field_double(std::string_view input) {
  return parse_grouped_value<parse_double_value>(input);
}

inline std::optional<bool> parse_field_boolean(std::string_view input) {
  if (const auto value = parse_field_integer(input))
    return *value == 1;
  return std::nullopt;
}

}  // namespace fosu::internal
