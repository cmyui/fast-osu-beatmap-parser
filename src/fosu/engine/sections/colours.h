#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/field_values.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace fosu::internal {

inline std::optional<u32> parse_colour(std::string_view input) {
  u32 rgb = 0;
  for (i32 i = 0; i < 3; ++i) {
    const auto comma = input.find(',');
    const auto component = parse_field_integer(input.substr(0, comma));
    if (!component || *component < 0 || *component > 255)
      return std::nullopt;
    rgb = (rgb << 8) | static_cast<u32>(*component);
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

// stable's colours for the keys Combo1 to Combo8. Lazer instead lists every
// combo colour in file order.
using ComboColourSlots = std::array<std::optional<u32>, 8>;

template <Client C>
const char* parse_colours_section(Beatmap&          beatmap,
                                  size_t&           colour_count,
                                  ComboColourSlots& slots,
                                  const char*       p,
                                  const char*       end) {
  return for_each_section_line<C>(p, end, [&](std::string_view line) {
    // Like storyboard commands, stable skips lines indented with ' ' or '_'.
    if (C == Client::Stable && (line.front() == ' ' || line.front() == '_'))
      return;
    if (const auto comment = line.find("//"); comment != std::string_view::npos)
      line = line.substr(0, comment);
    const char* line_end = line.data() + line.size();
    const char* colon = find_byte<':'>(line.data(), line_end);
    const auto  colour = colon == line_end
                             ? std::nullopt
                             : parse_colour(trim(colon + 1, line_end));
    if (!colour) {
      ++beatmap.stats.malformed_lines;
      return;
    }
    const auto key = trim(line.data(), colon);
    if (key.substr(0, 5) != "Combo")
      return;
    if constexpr (C == Client::Stable) {
      // stable stores each colour under its key, so a repeated key keeps its
      // last colour, and reads back exactly Combo1 to Combo8.
      if (key.size() == 6 && key[5] >= '1' && key[5] <= '8')
        slots[key[5] - '1'] = *colour;
    } else {
      const auto index = parse_field_integer(key.substr(5));
      if (index && *index >= 1 && *index <= 8)
        beatmap.combo_colours[colour_count++] = *colour;
    }
  });
}

// stable lists the colours of Combo1 to Combo8 in that order, skipping keys
// the map does not name.
inline size_t list_stable_combo_colours(Beatmap&                beatmap,
                                        const ComboColourSlots& slots) {
  size_t count = 0;
  for (const auto& colour : slots)
    if (colour)
      beatmap.combo_colours[count++] = *colour;
  return count;
}

}  // namespace fosu::internal
