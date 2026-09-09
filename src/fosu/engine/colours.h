#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/section_lines.h>
#include <fosu/engine/text.h>

namespace fosu::internal {

inline const char* parse_colours_section(Beatmap& beatmap,
                                         size_t& colour_count,
                                         const char* p,
                                         const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    std::string_view key, value;
    if (!split_kv(line.data(), line.size(), key, value) || key.substr(0, 5) != "Combo")
      return;
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
