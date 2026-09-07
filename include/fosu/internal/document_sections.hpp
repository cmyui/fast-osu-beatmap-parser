#pragma once
#include "text.hpp"
#include "metadata.hpp"

namespace fosu::internal {

template <typename Map>
inline void parse_general_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kGeneral, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_editor_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kEditor, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_metadata_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kMetadata, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_difficulty_line(Map& bm, const char* p, size_t len, bool& ar_specified) {
    ar_specified |= parse_kv_line<parse_double>(bm, kDifficulty, p, len, &bm.stats.malformed_lines);
}

template <typename Map>
inline void parse_colour_kv(Map& bm, std::string_view k, std::string_view v) {
    if (k.substr(0, 5) != "Combo") return;
    const char* p = v.data();
    const char* end = p + v.size();
    uint32_t rgb = 0;
    for (int i = 0; i < 3; ++i) {
        int64_t c;
        const char* q = parse_i64(p, end, c);
        if (q == p) return;
        p = q;
        if (i < 2) {
            if (p >= end || *p != ',') return;
            ++p;
            while (p < end && *p == ' ') ++p;
        }
        rgb = (rgb << 8) | (static_cast<uint32_t>(c) & 0xFF);
    }
    bm.combo_colours.push_back(rgb);
}

}  // namespace fosu::internal
