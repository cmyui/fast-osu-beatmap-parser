#pragma once
#include "line_scan.hpp"
#include "timing.hpp"
#include "vector_storage.hpp"

namespace fosu::internal {

template <typename Map>
inline void parse_timing_point_line(Map& bm, const char* p, size_t len) {
    typename Map::TimingPoint tp;
    if (parse_timing_fields(p, p + len, tp)) bm.timing_points.push_back(tp);
    else ++bm.stats.malformed_lines;
}

#if FOSU_SIMD

// Fused [TimingPoints] section loop: the same two loads serve the newline
// scan and the parser, and the section is sized exactly once — the next
// '[' bounds it, so reserve never over-allocates for short sections nor
// grows for marathon ones. Points are written straight into the reserved
// capacity and published at the end; blank, comment and header lines are
// only examined when the editor shape fails. Returns the position after
// the section.
template <typename Map>
inline const char* parse_timing_points_section(Map& bm, const char* p,
                                               const char* file_end) {
    using TP = typename Map::TimingPoint;
    auto& tps = bm.timing_points;
    constexpr bool kDirect = direct_vector_writes_v<decltype(Map::timing_points)>;
    const auto* bracket = static_cast<const char*>(
        memchr(p, '[', static_cast<size_t>(file_end - p)));
    const char* section_end = bracket ? bracket : file_end;
    tps.reserve(tps.size() + static_cast<size_t>(section_end - p) / 17 + 4);
    TpShapeCache cache{};
#if FOSU_SIMD_X86
    const ByteVector k_nl = bcast256(kByteNewline), k_comma = bcast256(kByteComma),
                     k_bias = bcast256(kByteBias), k_thr = bcast256(kByteThreshold);
#else
    const ByteVector k_nl = broadcast_byte('\n'), k_comma = broadcast_byte(','),
                     k_bias = broadcast_byte(80), k_thr = broadcast_byte(-119);
#endif
    // Direct mode writes each point into the reserved capacity and publishes
    // the count at the end; the portable mode falls back to push_back.
    TP* w = tps.data() + tps.size();
    TP* wend = tps.data() + tps.capacity();
    TP local;
    uint32_t malformed = 0;
    while (p < file_end) {
        const Bytes32 a = load32(p);
        const Bytes32 b = load32(p + 32);
        const uint64_t nl =
            equal_mask32(a, k_nl) |
            static_cast<uint64_t>(equal_mask32(b, k_nl)) << 32;
        const char* nlp = nl ? p + trailing_zeros(nl) : find_newline32(p + 64, file_end, k_nl);
        const char* next_line = nlp + (nlp < file_end);
        const char* line_end = nlp - (nlp > p && nlp[-1] == '\r');
        const auto len = static_cast<size_t>(line_end - p);
        TP* tp;
        if constexpr (kDirect) {
            if (w == wend) [[unlikely]] {
                publish_size(tps, static_cast<size_t>(w - tps.data()));
                tps.reserve(tps.capacity() * 2 + 16);
                w = tps.data() + tps.size();
                wend = tps.data() + tps.capacity();
            }
            tp = construct_record(w);
        } else {
            tp = &local;
        }
        if (len - 15 <= 64 - 15) [[likely]] {
            const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
            const uint64_t commas =
                (equal_mask32(a, k_comma) |
                 static_cast<uint64_t>(equal_mask32(b, k_comma)) << 32) &
                line_mask;
            const uint64_t nondig =
                (nondigit_mask32(a, k_bias, k_thr) |
                 static_cast<uint64_t>(nondigit_mask32(b, k_bias, k_thr)) << 32) &
                line_mask;
            const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
            bool accepted;
            if (tp_shape_match(row, commas, nondig, len, p)) {
                tp_shape_convert(row, p, *tp);
                accepted = true;
            } else {
                TpGeom geom;
                accepted = fast_parse_timing_point_masked(commas, nondig, p, len, *tp, &geom);
                if (accepted) tp_shape_insert(cache, commas, nondig, len, geom);
            }
            if (accepted) {
                if constexpr (kDirect) ++w;
                else tps.push_back(local);
                p = next_line;
                continue;
            }
        }
        // Unusual line: blank, comment, header, old field layouts or bytes
        // outside the editor shape.
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        if (!ignored_line(p, line_end)) {
            if (parse_timing_fields(p, line_end, *tp)) {
                if constexpr (kDirect) ++w;
                else tps.push_back(local);
            } else {
                ++malformed;
            }
        }
        p = next_line;
    }
    if constexpr (kDirect) publish_size(tps, static_cast<size_t>(w - tps.data()));
    bm.stats.malformed_lines += malformed;
    return p;
}
#endif  // FOSU_SIMD

}  // namespace fosu::internal
