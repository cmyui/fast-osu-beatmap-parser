#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/field_values.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/string_lookup.h>

namespace fosu::internal {

struct KeyValue {
  std::string_view key;
  std::string_view value;
};

inline std::optional<KeyValue> split_key_value(std::string_view line) {
  const char* p = line.data();
  const char* end = p + line.size();
  const char* colon = find_byte<':'>(p, end);
  if (colon == end)
    return std::nullopt;
  const char* key_end = colon;
  while (key_end > p && (key_end[-1] == ' ' || key_end[-1] == '\t'))
    --key_end;
  const char* value = colon + 1;
  // Only one separating space is removed. String values retain other whitespace.
  if (value < end && *value == ' ')
    ++value;
  return KeyValue{{p, static_cast<size_t>(key_end - p)},
                  {value, static_cast<size_t>(end - value)}};
}

using FieldParser = bool (*)(BeatmapHeader&, std::string_view);

template <auto Member, auto ParseValue>
inline bool assign_field_value(BeatmapHeader& header, std::string_view input) {
  const auto value = ParseValue(input);
  if (!value)
    return false;
  header.*Member = *value;
  return true;
}

template <auto Member>
inline bool assign_field_text(BeatmapHeader& header, std::string_view input) {
  header.*Member = input;
  return true;
}

template <size_t N>
inline void parse_key_value(Beatmap& beatmap,
                            const StringLookup<FieldParser, N>& fields,
                            const KeyValue& field) {
  if (const auto* parse = fields.find(field.key))
    if (!(*parse)(beatmap, field.value))
      ++beatmap.stats.malformed_lines;
}

template <size_t N>
inline const char* parse_key_value_section(Beatmap& beatmap,
                                           const StringLookup<FieldParser, N>& fields,
                                           const char* p,
                                           const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    if (const auto field = split_key_value(line))
      parse_key_value(beatmap, fields, *field);
  });
}

}  // namespace fosu::internal
