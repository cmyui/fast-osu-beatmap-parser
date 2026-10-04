#pragma once

#include "fosu/format.h"

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/engine/timing_points/point.h>
#include <fosu/types.h>

#include <cstddef>
#include <string_view>

namespace fosu::internal {

template <Format F>
const char* parse_timing_points_section(Beatmap&    beatmap,
                                        size_t&     point_count,
                                        const char* p,
                                        const char* file_end) {
  return for_each_section_line(p, file_end, [&](std::string_view line) {
    const char* begin = line.data();
    const char* end = begin + line.size();
    auto        point = parse_common_timing_point<F>(begin, end);
    if (!point) [[unlikely]]
      point = parse_timing_point<F>(begin, end);
    if (point)
      beatmap.timing_points[point_count++] = *point;
    else [[unlikely]]
      ++beatmap.stats.malformed_lines;
  });
}

}  // namespace fosu::internal
