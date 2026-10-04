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
  u32 time_digits = 1;
  while (p < file_end) {
    TimingPoint point;
    if (const char* next =
            parse_common_timing_point<F>(p, file_end, time_digits, point)) {
      beatmap.timing_points[point_count++] = point;
      p = next;
      continue;
    }
    // Blank lines, comments, a section header, or another spelling.
    if (*p == '\r' || *p == '\n') {
      ++p;
      continue;
    }
    const auto  line = read_line(p, file_end);
    const char* line_end = p + line.text.size();
    if (section_header_line(p, line_end))
      break;
    if (!ignored_line(p, line_end)) {
      if (const auto general = parse_timing_point<F>(p, line_end))
        beatmap.timing_points[point_count++] = *general;
      else [[unlikely]]
        ++beatmap.stats.malformed_lines;
    }
    p = line.next;
  }
  return p;
}

}  // namespace fosu::internal
