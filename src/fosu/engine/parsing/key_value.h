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

template <auto& Fields>
inline void parse_key_value(Beatmap& beatmap, const KeyValue& field) {
  // A switch exposes constant keys and handlers to the compiler, allowing
  // equality checks and field assignments to inline without an indirect call.
  static_assert(Fields.size <= 16, "Extend the field dispatch cases for larger sections");
  bool valid = true;
#define FOSU_FIELD_CASE(index)                                \
  case Fields.slot_at(index):                                 \
    if constexpr (Fields.size > index) {                      \
      if (field.key == Fields.key_at(index))                  \
        valid = Fields.value_at(index)(beatmap, field.value); \
    }                                                         \
    break
  switch (Fields.slot_for(field.key)) {
    FOSU_FIELD_CASE(0);
    FOSU_FIELD_CASE(1);
    FOSU_FIELD_CASE(2);
    FOSU_FIELD_CASE(3);
    FOSU_FIELD_CASE(4);
    FOSU_FIELD_CASE(5);
    FOSU_FIELD_CASE(6);
    FOSU_FIELD_CASE(7);
    FOSU_FIELD_CASE(8);
    FOSU_FIELD_CASE(9);
    FOSU_FIELD_CASE(10);
    FOSU_FIELD_CASE(11);
    FOSU_FIELD_CASE(12);
    FOSU_FIELD_CASE(13);
    FOSU_FIELD_CASE(14);
    FOSU_FIELD_CASE(15);
    default:
      break;
  }
#undef FOSU_FIELD_CASE
  if (!valid)
    ++beatmap.stats.malformed_lines;
}

template <auto& Fields>
inline const char* parse_key_value_section(Beatmap& beatmap,
                                           const char* p,
                                           const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    if (const auto field = split_key_value(line))
      parse_key_value<Fields>(beatmap, *field);
  });
}

}  // namespace fosu::internal
