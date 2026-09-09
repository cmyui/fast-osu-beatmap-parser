#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/timing_points/point.h>

namespace fosu::internal {

#if FOSU_SIMD
// Each point is parsed into a local value and then copied into the beatmap arena.
inline const char* parse_timing_points_section_simd(Beatmap& beatmap,
                                                    size_t& point_count,
                                                    const char* p,
                                                    const char* file_end,
                                                    int time_offset) {
  const ByteVector newline_value = broadcast_byte<'\n'>();
  const ByteVector comma_value = broadcast_byte<','>();
  uint32_t malformed = 0;

  while (p < file_end) {
    const Bytes32 first = load32(p);
    const Bytes32 second = load32(p + 32);
    const uint64_t newline_mask =
        equal_mask32(first, newline_value) |
        static_cast<uint64_t>(equal_mask32(second, newline_value)) << 32;
    const char* newline = newline_mask ? p + trailing_zeros(newline_mask)
                                       : find_byte<'\n'>(p + 64, file_end);
    const char* next_line = newline + (newline < file_end);
    const char* line_end = newline - (newline > p && newline[-1] == '\r');
    const auto length = static_cast<size_t>(line_end - p);
    const uint64_t line_mask = length >= 64 ? ~0ull : ((1ull << length) - 1);
    const uint64_t commas =
        (equal_mask32(first, comma_value) |
         static_cast<uint64_t>(equal_mask32(second, comma_value)) << 32) &
        line_mask;

    if (length - 15 <= 64 - 15) [[likely]] {
      const uint64_t nondigits = (nondigit_mask32(first) |
                                  static_cast<uint64_t>(nondigit_mask32(second)) << 32) &
                                 line_mask;
      if (const auto point = try_parse_timing_point_fast_masked(commas, nondigits, p,
                                                                length, time_offset)) {
        beatmap.timing_points[point_count++] = *point;
        p = next_line;
        continue;
      }
    }

    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    if (c == '[')
      break;
    if (!ignored_line(p, line_end)) {
      const auto point = length <= 64
                             ? parse_timing_point<true>(p, line_end, commas, time_offset)
                             : parse_timing_point(p, line_end, 0, time_offset);
      if (point) {
        beatmap.timing_points[point_count++] = *point;
      } else
        ++malformed;
    }
    p = next_line;
  }

  beatmap.stats.malformed_lines += malformed;
  return p;
}
#endif

inline const char* parse_timing_points_section_scalar(Beatmap& beatmap,
                                                      size_t& point_count,
                                                      const char* p,
                                                      const char* file_end,
                                                      int time_offset) {
  return for_each_section_line(p, file_end, [&](std::string_view line) {
    if (const auto point =
            parse_timing_point(line.data(), line.data() + line.size(), 0, time_offset)) {
      beatmap.timing_points[point_count++] = *point;
    } else
      ++beatmap.stats.malformed_lines;
  });
}

inline const char* parse_timing_points_section(Beatmap& beatmap,
                                               size_t& point_count,
                                               const char* p,
                                               const char* file_end,
                                               int time_offset = 0) {
#if FOSU_SIMD
  return parse_timing_points_section_simd(beatmap, point_count, p, file_end, time_offset);
#else
  return parse_timing_points_section_scalar(beatmap, point_count, p, file_end,
                                            time_offset);
#endif
}

}  // namespace fosu::internal
