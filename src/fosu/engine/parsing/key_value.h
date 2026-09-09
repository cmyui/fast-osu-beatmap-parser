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

// UTF-8 spellings of the whitespace trimmed by .NET String.Trim. The common
// ASCII case does not decode Unicode or allocate a replacement string.
inline size_t field_space_width(std::string_view text) {
  if (text.empty())
    return 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.data());
  if (p[0] == ' ' || (p[0] >= 9 && p[0] <= 13))
    return 1;
  if (text.size() >= 2 && p[0] == 0xC2 && (p[1] == 0x85 || p[1] == 0xA0))
    return 2;
  if (text.size() >= 3 && ((p[0] == 0xE1 && p[1] == 0x9A && p[2] == 0x80) ||
                           (p[0] == 0xE2 && p[1] == 0x80 &&
                            ((p[2] <= 0x8A && p[2] >= 0x80) || p[2] == 0xA8 ||
                             p[2] == 0xA9 || p[2] == 0xAF)) ||
                           (p[0] == 0xE2 && p[1] == 0x81 && p[2] == 0x9F) ||
                           (p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80)))
    return 3;
  return 0;
}

inline std::string_view trim_field(std::string_view text) {
  while (const auto width = field_space_width(text))
    text.remove_prefix(width);
  while (!text.empty()) {
    size_t last = text.size() - 1;
    while (last && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80)
      --last;
    if (field_space_width({text.data() + last, text.size() - last}) != text.size() - last)
      break;
    text = {text.data(), last};
  }
  return text;
}

inline std::optional<KeyValue> split_key_value(std::string_view line) {
  const char* p = line.data();
  const char* end = p + line.size();
  const char* colon = find_byte<':'>(p, end);
  if (colon == end)
    return std::nullopt;
  return KeyValue{trim_field({p, static_cast<size_t>(colon - p)}),
                  trim_field({colon + 1, static_cast<size_t>(end - colon - 1)})};
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
