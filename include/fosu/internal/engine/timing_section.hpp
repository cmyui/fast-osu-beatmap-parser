#pragma once

#include "../../beatmap.hpp"
#include "byte_scan.hpp"
#include "timing.hpp"

namespace fosu::internal {

inline void parse_timing_point_line(
    Beatmap& beatmap, size_t& point_count, const char* p, size_t length) {
    TimingPoint point{};
    if (parse_timing_fields(p, p + length, point))
        beatmap.timing_points[point_count++] = point;
    else
        ++beatmap.stats.malformed_lines;
}

#if FOSU_SIMD

// Fused [TimingPoints] section loop. Each point is parsed into a local value
// and then copied into the beatmap arena.
inline const char* parse_timing_points_section(
    Beatmap& beatmap, size_t& point_count, const char* p,
    const char* file_end) {
    TpShapeCache cache{};
#if FOSU_SIMD_X86
    const ByteVector newline_value = bcast256(kByteNewline);
    const ByteVector comma_value = bcast256(kByteComma);
    const ByteVector bias = bcast256(kByteBias);
    const ByteVector threshold = bcast256(kByteThreshold);
#else
    const ByteVector newline_value = broadcast_byte('\n');
    const ByteVector comma_value = broadcast_byte(',');
    const ByteVector bias = broadcast_byte(80);
    const ByteVector threshold = broadcast_byte(-119);
#endif
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
        const char* line_end =
            newline - (newline > p && newline[-1] == '\r');
        const auto length = static_cast<size_t>(line_end - p);
        TimingPoint point{};
        bool accepted = false;
        const uint64_t line_mask = length >= 64 ? ~0ull : ((1ull << length) - 1);
        const uint64_t commas =
            (equal_mask32(first, comma_value) |
             static_cast<uint64_t>(equal_mask32(second, comma_value)) << 32) &
            line_mask;

        if (length - 15 <= 64 - 15) [[likely]] {
          const uint64_t nondigits =
              (nondigit_mask32(first, bias, threshold) |
               static_cast<uint64_t>(nondigit_mask32(second, bias, threshold)) << 32) &
              line_mask;
          const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
          if (tp_shape_match(row, commas, nondigits, length, p)) {
            tp_shape_convert(row, p, point);
            accepted = true;
          } else {
            TpGeom geometry;
            accepted = fast_parse_timing_point_masked(commas, nondigits, p, length, point,
                                                      &geometry);
            if (accepted)
              tp_shape_insert(cache, commas, nondigits, length, geometry);
          }
        }

        if (accepted) {
            beatmap.timing_points[point_count++] = point;
            p = next_line;
            continue;
        }

        const char c = *p;
        if (c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        if (c == '[') break;
        if (!ignored_line(p, line_end)) {
          const bool valid = length <= 64
                                 ? parse_timing_fields<true>(p, line_end, point, commas)
                                 : parse_timing_fields(p, line_end, point);
          if (valid)
            beatmap.timing_points[point_count++] = point;
          else
            ++malformed;
        }
        p = next_line;
    }

    beatmap.stats.malformed_lines += malformed;
    return p;
}
#endif

}  // namespace fosu::internal
