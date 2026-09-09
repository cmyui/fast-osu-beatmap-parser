#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/lines.h>

namespace fosu::internal {

inline const char* parse_colours_section(Beatmap& beatmap,
                                         size_t& colour_count,
                                         const char* p,
                                         const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    const char* line_end = line.data() + line.size();
    const char* colon = find_byte<':'>(line.data(), line_end);
    if (colon == line_end)
      return;
    const auto key = trim(line.data(), colon);
    if (key.substr(0, 5) != "Combo")
      return;
    const auto value = trim(colon + 1, line_end);
    const char* p = value.data();
    const char* end = p + value.size();
    uint32_t rgb = 0;
    for (int i = 0; i < 3; ++i) {
      int64_t component;
      const char* next = parse_i64(p, end, component);
      if (next == p)
        return;
      p = next;
      if (i < 2) {
        if (p >= end || *p != ',')
          return;
        ++p;
        while (p < end && *p == ' ')
          ++p;
      }
      rgb = (rgb << 8) | (static_cast<uint32_t>(component) & 0xFF);
    }
    beatmap.combo_colours[colour_count++] = rgb;
  });
}

}  // namespace fosu::internal
