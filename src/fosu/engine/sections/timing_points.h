#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/engine/timing_points/point.h>
#include <fosu/types.h>

#include <cstddef>
#include <string_view>

namespace fosu::internal {

inline const char* parse_timing_points_section(Beatmap&    beatmap,
                                               size_t&     point_count,
                                               const char* p,
                                               const char* file_end,
                                               i32         time_offset = 0) {
  return for_each_section_line(p, file_end, [&](std::string_view line) {
    const char* begin = line.data();
    const char* end = begin + line.size();
    auto        point = parse_common_timing_point(begin, end, time_offset);
    if (!point) [[unlikely]]
      point = parse_timing_point(begin, end, time_offset);
    if (point)
      beatmap.timing_points[point_count++] = *point;
    else [[unlikely]]
      ++beatmap.stats.malformed_lines;
  });
}

}  // namespace fosu::internal
