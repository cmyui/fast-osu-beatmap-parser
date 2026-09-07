#pragma once
// [HitObjects] section parsing over a compile-time storage policy (the
// "sink"). The library writes records straight into vector capacity through
// raw cursors; the one-shot executable streams them to its output. Both use
// the same framing, prefix, slider and tail logic below.
//
// Sink hooks:
//   using HitObject, Point, SliderRecord;
//   HitObject& begin(size_t len)                 next uncommitted record
//   void commit(HitObject&) / rollback(HitObject&)
//   void sample(HitObject&, const char* s, size_t n)   trailing hit sample
//   Point* point_slot(size_t bound)              storage for `bound` points
//   uint32_t point_index(Point*)                 pool index of a point slot
//   SliderRecord& slider_slot()                  uncommitted slider fields
//   StringRef view(const char* s, size_t n)      string representation
//   void slider_commit(HitObject&, SliderRecord&, Point* end, const char* hs, size_t hs_len)
//   void slider_rollback(Point* first, Point* end, bool keep_points)
//   Stats& stats()                               fast/slow/malformed counters
#include <cstring>
#include <string_view>

#include "object_tail.hpp"
#include "prefix.hpp"
#include "slider.hpp"

namespace fosu::detail {

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
// Returns false to reject the line. Points written before a later field
// fails stay in the pool (keep_points), as the official decoder's per-line
// rejection leaves earlier state intact; a failed point list is dropped.
template <typename Sink>
__attribute__((noinline))
inline bool parse_slider(Sink& sink, typename Sink::HitObject& h, const char* p,
                         const char* end, const HitConsts& k) {
    using Point = typename Sink::Point;
    if (p >= end) [[unlikely]] return false;
    const char curve_type = *p++;
    // A point costs at least four bytes ("|x:y"), which bounds the count;
    // the pair fast path also needs a second slot.
    Point* const w0 = sink.point_slot(static_cast<size_t>(end - p) / 4 + 2);
    Point* w = w0;
    if (!parse_slider_points(p, end, w, k)) [[unlikely]] {
        sink.slider_rollback(w0, w, false);
        return false;
    }
    if (p >= end || *p != ',') [[unlikely]] {
        sink.slider_rollback(w0, w, true);
        return false;
    }
    ++p;
    // slides: editor files write one or two bare digits.
    int32_t slides;
    const uint32_t s0 = static_cast<uint8_t>(p[0] - '0');
    const uint32_t s1 = static_cast<uint8_t>(p[1] - '0');
    if (s0 <= 9 && p[1] == ',') {
        slides = static_cast<int32_t>(s0);
        p += 1;
    } else if (s0 <= 9 && s1 <= 9 && p[2] == ',') {
        slides = static_cast<int32_t>(s0 * 10 + s1);
        p += 2;
    } else {
        const uint32_t srun = digit_run8(p);
        if (srun - 1 <= 6) {
            slides = static_cast<int32_t>(swar_parse_u64(p, srun));
            p = skip_numeric_space(p + srun, end);
        } else {
            int64_t wide;
            const char* next = parse_osu_int(p, end, wide);
            if (next == p) [[unlikely]] {
                sink.slider_rollback(w0, w, true);
                return false;
            }
            slides = clamp_i32(wide);
            p = next;
        }
    }
    if (slides > 9000 || (p < end && *p != ',')) [[unlikely]] {
        sink.slider_rollback(w0, w, true);
        return false;
    }
    double length = 0;
    if (p < end) {
#if FOSU_SIMD_X86
        const char* q = parse_slider_length(p + 1, length, k);
        if (!q) q = parse_osu_double(p + 1, end, length, 131072);
#else
        const char* q = parse_osu_double(p + 1, end, length, 131072);
#endif
        if (q != p + 1) q = skip_numeric_space(q, end);
        // Both numeric paths have already enforced the official length bound.
        if (q == p + 1 || (q < end && *q != ',')) [[unlikely]] {
            sink.slider_rollback(w0, w, true);
            return false;
        }
        p = q;
    }
    // Optional: edgeSounds, edgeSets, hitSample, assigned positionally;
    // absent fields are empty, and the sample ends at the next comma as in
    // the official decoder's field split. (An unconditional scan measured
    // slower than this branch when the input is cache-resident.)
    const char* es = p;
    size_t es_len = 0;
    const char* eb = p;
    size_t eb_len = 0;
    const char* hs = p;
    size_t hs_len = 0;
    if (p < end && *p == ',') {
        ++p;
#if FOSU_SIMD_X86
        const auto span = static_cast<size_t>(end - p);
        if (span <= 32) {
            // The remaining comma positions from one 32-byte scan.
            const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
            const auto cm = static_cast<uint32_t>(_mm256_movemask_epi8(
                                _mm256_cmpeq_epi8(v, k.comma))) &
                            static_cast<uint32_t>((1ull << span) - 1);
            const uint32_t c0 = _tzcnt_u32(cm);
            const uint32_t c1 = _tzcnt_u32(_blsr_u32(cm));
            const uint32_t c2 = _tzcnt_u32(_blsr_u32(_blsr_u32(cm)));
            es = p;
            if (c0 >= span) {
                es_len = span;
            } else if (c1 >= span) {
                es_len = c0;
                eb = p + c0 + 1;
                eb_len = span - c0 - 1;
            } else {
                es_len = c0;
                eb = p + c0 + 1;
                eb_len = c1 - c0 - 1;
                hs = p + c1 + 1;
                hs_len = (c2 < span ? c2 : static_cast<uint32_t>(span)) - c1 - 1;
            }
        } else
#endif
        {
            std::string_view extra[3];
            int n = 0;
            while (n < 3 && p < end) {
                const auto* c = static_cast<const char*>(memchr(p, ',', static_cast<size_t>(end - p)));
                const char* fend = c ? c : end;
                extra[n++] = {p, static_cast<size_t>(fend - p)};
                p = fend + 1;
            }
            es = extra[0].data(); es_len = extra[0].size();
            eb = extra[1].data(); eb_len = extra[1].size();
            hs = extra[2].data(); hs_len = extra[2].size();
        }
    }
    if (!valid_sample({hs, hs_len}, true) || !valid_edge_sets({eb, eb_len}, slides)) [[unlikely]] {
        sink.slider_rollback(w0, w, true);
        return false;
    }
    auto& s = sink.slider_slot();
    s.point_begin = sink.point_index(w0);
    s.point_count = static_cast<uint32_t>(w - w0);
    s.slides = slides;
    s.curve_type = curve_type;
    s.length = length;
    s.edge_sounds = sink.view(es, es_len);
    s.edge_sets = sink.view(eb, eb_len);
    sink.slider_commit(h, s, w, hs, hs_len);
    return true;
}

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, trailing hitSample.
template <typename Sink>
inline bool finish_hitobject(Sink& sink, typename Sink::HitObject& h, const char* p,
                             const char* end, const HitConsts& k) {
    if (!(h.type & (1 | 2 | 8 | 128))) return false;
    if (p < end && *p != ',') return false;
    if (!(h.type & 1) && (h.type & 2)) {  // slider
        if (p >= end || *p != ',') return false;
        return parse_slider(sink, h, p + 1, end, k);
    }
    std::string_view sample;
    if (!parse_object_tail(h, p, end, sample)) return false;
    sink.sample(h, sample.data(), sample.size());
    return true;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
// Kept out of line so the hot loop keeps its registers.
template <typename Sink>
__attribute__((noinline))
inline bool slow_hitobject_line(Sink& sink, typename Sink::HitObject& h, const char* p,
                                const char* line_end, const HitConsts& k) {
    h.end_time = 0;
    h.slider = Sink::HitObject::kNoSlider;
    const int sn = scalar_parse_prefix(p, static_cast<size_t>(line_end - p), h);
    if (sn < 0) return false;
    ++sink.stats().slow_path_lines;
    return finish_hitobject(sink, h, p + sn, line_end, k);
}

// Scalar section loop (non-x86 builds and ParseOptions::use_simd = false).
// Returns the position after the section.
template <typename Sink>
inline const char* parse_hitobject_lines_scalar(Sink& sink, const char* p,
                                                const char* file_end, const HitConsts& k) {
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        const auto* nl = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(file_end - p)));
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const char* next_line = nl ? nl + 1 : file_end;
        if (!ignored_line(p, line_end)) {
            auto& h = sink.begin(static_cast<size_t>(line_end - p));
            if (slow_hitobject_line(sink, h, p, line_end, k)) sink.commit(h);
            else [[unlikely]] { sink.rollback(h); ++sink.stats().malformed_lines; }
        }
        p = next_line;
    }
    return p;
}

#if FOSU_SIMD_X86
// Newline search past the first window, 32 bytes per step. Bytes beyond
// `end` are zero padding, so a hit is always inside the input. Returns `end`
// when the remaining bytes hold no newline.
inline const char* find_newline32(const char* p, const char* end, __m256i nl) {
    while (p < end) {
        const auto m = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), nl)));
        if (m) return p + _tzcnt_u32(m);
        p += 32;
    }
    return end;
}

// SIMD section loop. One 32-byte load per line yields the newline, comma and
// non-digit masks. The prefix shape is validated arithmetically; only lines
// that fail it (blank, comments, headers, signed/decimal/wide fields) take
// the scalar route, so no per-line whitespace or comment scan runs on the
// fast path. Returns the position after the section.
template <typename Sink>
inline const char* parse_hitobject_lines(Sink& sink, const char* p, const char* file_end,
                                         const HitConsts& k) {
    const __m256i k_nl = k.nl, k_comma = k.comma, k_bias = k.bias, k_thr = k.thr, k_zero = k.zero;
    uint32_t fast_lines = 0, malformed = 0;
    while (p < file_end) {
        const __m256i ascii = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const auto nl_mask = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(ascii, k_nl)));
        const auto commas = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(ascii, k_comma)));
        const uint32_t nondig = nondigit_mask32(ascii, k_bias, k_thr);
        const char* nl = nl_mask ? p + _tzcnt_u32(nl_mask) : find_newline32(p + 32, file_end, k_nl);
        const char* next_line = nl + (nl < file_end);
        const char* line_end = nl - (nl > p && nl[-1] == '\r');
        const auto len = static_cast<size_t>(line_end - p);
        const PrefixShape sh = classify_prefix(nondig, commas);
        const char after = p[sh.p4];  // p4 <= 32: inside the window or its padding
        if (sh.ok && (sh.p4 == len || after == ',' || after == '\0')) [[likely]] {
            auto& h = sink.begin(len);
            uint32_t type;
            bool ok;
            if (convert_prefix(ascii, k_zero, sh, p, h, type)) [[likely]] {
                ++fast_lines;
                if (type & 1) {
                    // Circle precedence: the tail is empty or ",sample".
                    if (sh.p4 == len) {
                        sink.sample(h, line_end, 0);
                        ok = true;
                    } else if (len - sh.p4 == 9 && after == ',' && short_sample(p + sh.p4 + 1)) {
                        sink.sample(h, p + sh.p4 + 1, 8);
                        ok = true;
                    } else {
                        ok = finish_hitobject(sink, h, p + sh.p4, line_end, k);
                    }
                } else if (type & 2) {
                    ok = sh.p4 < len && after == ',' &&
                         parse_slider(sink, h, p + sh.p4 + 1, line_end, k);
                } else {
                    ok = finish_hitobject(sink, h, p + sh.p4, line_end, k);
                }
            } else {
                ok = slow_hitobject_line(sink, h, p, line_end, k);
            }
            if (ok) sink.commit(h);
            else [[unlikely]] { sink.rollback(h); ++malformed; }
        } else {
            const char c = *p;
            if (c == '\r' || c == '\n') { ++p; continue; }
            if (c == '[') break;
            if (!ignored_line(p, line_end)) {
                auto& h = sink.begin(len);
                if (slow_hitobject_line(sink, h, p, line_end, k)) sink.commit(h);
                else [[unlikely]] { sink.rollback(h); ++malformed; }
            }
        }
        p = next_line;
    }
    auto& stats = sink.stats();
    stats.fast_path_lines += fast_lines;
    stats.malformed_lines += malformed;
    return p;
}
#endif

}  // namespace fosu::detail
