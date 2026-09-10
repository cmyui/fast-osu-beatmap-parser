#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/chunk_list.h>
#include <fosu/engine/parsing/field_values.h>
#include <fosu/engine/parsing/lines.h>

namespace fosu::internal {

inline std::optional<uint32_t> parse_colour(std::string_view input) {
  uint32_t rgb = 0;
  for (int i = 0; i < 3; ++i) {
    const auto comma = input.find(',');
    const auto component = parse_field_integer(input.substr(0, comma));
    if (!component || *component < 0 || *component > 255)
      return std::nullopt;
    rgb = (rgb << 8) | static_cast<uint32_t>(*component);
    if (i < 2) {
      if (comma == std::string_view::npos)
        return std::nullopt;
      input.remove_prefix(comma + 1);
    } else if (comma != std::string_view::npos &&
               input.find(',', comma + 1) != std::string_view::npos) {
      return std::nullopt;
    }
  }
  // The legacy decoder accepts a fourth component but ignores alpha.
  return rgb;
}

inline const char* parse_colours_section(Beatmap& beatmap,
                                         Arena* arena,
                                         ChunkList<uint32_t>& colours,
                                         const char* p,
                                         const char* end) {
  return for_each_section_line_until(p, end, [&](std::string_view line) {
    if (const auto comment = line.find("//"); comment != std::string_view::npos)
      line = line.substr(0, comment);
    const char* line_end = line.data() + line.size();
    const char* colon = find_byte<':'>(line.data(), line_end);
    const auto colour =
        colon == line_end ? std::nullopt : parse_colour(trim(colon + 1, line_end));
    if (!colour) {
      ++beatmap.stats.malformed_lines;
      return true;
    }
    const auto key = trim(line.data(), colon);
    if (key.substr(0, 5) != "Combo")
      return true;
    const auto index = parse_field_integer(key.substr(5));
    if (index && *index >= 1 && *index <= 8)
      return chunk_list_push(arena, colours, *colour) != nullptr;
    return true;
  });
}

}  // namespace fosu::internal
