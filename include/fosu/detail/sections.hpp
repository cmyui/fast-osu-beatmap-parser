#pragma once
#include "prefix.hpp"

namespace fosu::detail {
#if FOSU_SIMD_X86
// Shared framing and prefix dispatch. The sink is selected at compile time;
// its four hooks retain each representation's allocation and rollback rules.
template <typename Sink>
inline const char* parse_hitobject_lines(Sink& sink, const char* p, const char* file_end) {
    uint32_t fast_lines = 0;
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        const __m256i ascii = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const auto nl_mask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(ascii, _mm256_set1_epi8('\n'))));
        const char* nl;
        if (nl_mask) {
            nl = p + _tzcnt_u32(nl_mask);
        } else {
            const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
            const auto nl2 = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n'))));
            nl = nl2 ? p + 32 + _tzcnt_u32(nl2)
                     : static_cast<const char*>(memchr(p + 64, '\n', file_end - p > 64 ? static_cast<size_t>(file_end - p) - 64 : 0));
        }
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const char* next_line = nl ? nl + 1 : file_end;
        const auto len = static_cast<size_t>(line_end - p);
        auto& h = sink.begin(len);
        const int next = fast_parse_prefix(ascii, p, h);
        bool ok;
        if (next >= 0) {
            ++fast_lines;
            ok = sink.finish(h, p + next, line_end, static_cast<size_t>(file_end - p));
        } else {
            h.end_time = 0;
            h.slider = Sink::HitObject::kNoSlider;
            const int sn = scalar_parse_prefix(p, len, h);
            if (sn >= 0) {
                ++sink.stats().slow_path_lines;
                ok = sink.finish(h, p + sn, line_end, static_cast<size_t>(file_end - p));
            } else {
                ok = false;
            }
        }
        if (ok) {
            sink.commit(h);
        } else {
            sink.rollback(h);
            ++sink.stats().malformed_lines;
        }
        p = next_line;
    }
    sink.stats().fast_path_lines += fast_lines;
    return p;
}

#endif
}  // namespace fosu::detail
