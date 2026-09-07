#pragma once
#include "line_scan.hpp"
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
//   Point* reserve_slider_points(size_t bound)   storage for `bound` points
//   uint32_t point_index(Point*)                 pool index of a point slot
//   SliderRecord& begin_slider()                 uncommitted slider fields
//   StringRef view(const char* s, size_t n)      string representation
//   void commit_slider(HitObject&, SliderRecord&, Point* end, const char* hs, size_t hs_len)
//   void reject_slider(Point* first, Point* retained_end)
//   Stats& stats()                               fast/slow/malformed counters
#include <cstring>
#include <string_view>

#include "hitobject_details.hpp"
#include "prefix.hpp"
#include "slider_tail.hpp"

namespace fosu::internal {

// Slider params after "type,hitSound,":
//   curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
// Returns false to reject the line. Points written before a later field
// fails stay in the pool (keep_points), as the official decoder's per-line
// rejection leaves earlier state intact; a failed point list is dropped.
template <typename Sink>
__attribute__((noinline))
inline bool parse_slider(Sink& sink, typename Sink::HitObject& object, const char* p,
                         const char* end,
                         const HitObjectParseConstants& k) {
    using SliderPoint = typename Sink::Point;
    if (p >= end) [[unlikely]] return false;
    const char curve_type = *p++;
    // A point costs at least four bytes ("|x:y"), which bounds the count;
    // the pair fast path also needs a second slot.
    SliderPoint* const first_point =
        sink.reserve_slider_points(static_cast<size_t>(end - p) / 4 + 2);
    SliderPoint* next_point = first_point;
#if FOSU_SIMD
    if (const auto initial_points =
            try_parse_slider_point_prefix_fast<SliderPoint>(p, k)) {
        *next_point++ = initial_points->first;
        if (initial_points->has_second)
            *next_point++ = initial_points->second;
        p = initial_points->next;
    }
#endif
    while (p < end && *p == '|') {
        const auto point = parse_slider_point<SliderPoint>(p, end, k);
        if (!point) [[unlikely]] {
            sink.reject_slider(first_point, first_point);
            return false;
        }
        *next_point++ = point->value;
        p = point->next;
    }
    const auto tail = parse_slider_tail(p, end, k);
    if (!tail) [[unlikely]] {
        sink.reject_slider(first_point, next_point);
        return false;
    }
    auto& slider = sink.begin_slider();
    slider.point_begin = sink.point_index(first_point);
    slider.point_count = static_cast<uint32_t>(next_point - first_point);
    slider.slides = tail->slides;
    slider.curve_type = curve_type;
    slider.length = tail->length;
    slider.edge_sounds = sink.view(
        tail->sounds.edge_sounds.data(), tail->sounds.edge_sounds.size());
    slider.edge_sets = sink.view(
        tail->sounds.edge_sets.data(), tail->sounds.edge_sets.size());
    sink.commit_slider(object, slider, next_point,
                       tail->sounds.hit_sample.data(),
                       tail->sounds.hit_sample.size());
    return true;
}

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, trailing hitSample.
template <typename Sink>
inline bool parse_hitobject_details(
    Sink& sink, typename Sink::HitObject& object, const char* p,
    const char* end, const HitObjectParseConstants& k) {
    switch (classify_hitobject_kind(object.type)) {
        case HitObjectKind::Circle: {
            const auto details = parse_circle_details(p, end);
            if (!details) return false;
            object.end_time = 0;
            sink.sample(object, details->hit_sample.data(),
                        details->hit_sample.size());
            return true;
        }
        case HitObjectKind::Slider:
            return p < end && *p == ',' &&
                   parse_slider(sink, object, p + 1, end, k);
        case HitObjectKind::Spinner: {
            const auto details = parse_spinner_details(p, end);
            if (!details) return false;
            object.end_time = details->end_time;
            sink.sample(object, details->hit_sample.data(),
                        details->hit_sample.size());
            return true;
        }
        case HitObjectKind::Hold: {
            const auto details = parse_hold_details(object.time, p, end);
            if (!details) return false;
            object.end_time = details->end_time;
            sink.sample(object, details->hit_sample.data(),
                        details->hit_sample.size());
            return true;
        }
        case HitObjectKind::Invalid:
            return false;
    }
    return false;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
// Kept out of line so the hot loop keeps its registers.
template <typename Sink>
__attribute__((noinline))
inline bool parse_hitobject_line_scalar(
    Sink& sink, typename Sink::HitObject& h, const char* p,
    const char* line_end, const HitObjectParseConstants& k) {
    const auto prefix = parse_hitobject_prefix_scalar(
        p, static_cast<size_t>(line_end - p));
    if (!prefix) return false;
    initialize_hitobject(h, prefix->value);
    ++sink.stats().slow_path_lines;
    return parse_hitobject_details(sink, h, prefix->next, line_end, k);
}

// Scalar section loop (scalar builds and ParseOptions::use_simd = false).
// Returns the position after the section.
template <typename Sink>
inline const char* parse_hitobject_lines_scalar(Sink& sink, const char* p,
                                                const char* file_end,
                                                const HitObjectParseConstants& k) {
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
            if (parse_hitobject_line_scalar(sink, h, p, line_end, k))
                sink.commit(h);
            else [[unlikely]] { sink.rollback(h); ++sink.stats().malformed_lines; }
        }
        p = next_line;
    }
    return p;
}

#if FOSU_SIMD
// SIMD section loop. One 32-byte load per line yields the newline, comma and
// non-digit masks. The prefix shape is validated arithmetically; only lines
// that fail it (blank, comments, headers, signed/decimal/wide fields) take
// the scalar route, so no per-line whitespace or comment scan runs on the
// fast path. Returns the position after the section.
template <typename Sink>
inline const char* parse_hitobject_lines(Sink& sink, const char* p, const char* file_end,
                                         const HitObjectParseConstants& k) {
    const ByteVector k_nl = k.nl, k_comma = k.comma, k_bias = k.bias, k_thr = k.thr, k_zero = k.zero;
    uint32_t fast_lines = 0, malformed = 0;
    while (p < file_end) {
        const Bytes32 ascii = load32(p);
        const auto nl_mask = equal_mask32(ascii, k_nl);
        const auto commas = equal_mask32(ascii, k_comma);
        const uint32_t nondig = nondigit_mask32(ascii, k_bias, k_thr);
        const char* nl = nl_mask ? p + trailing_zeros(nl_mask) : find_newline32(p + 32, file_end, k_nl);
        const char* next_line = nl + (nl < file_end);
        const char* line_end = nl - (nl > p && nl[-1] == '\r');
        const auto len = static_cast<size_t>(line_end - p);
        const auto prefix_shape = classify_hitobject_prefix(nondig, commas);
        const char after = p[prefix_shape.p4];
        if (prefix_shape.ok &&
            (prefix_shape.p4 == len || after == ',' || after == '\0'))
            [[likely]] {
            auto& h = sink.begin(len);
            bool ok;
            const auto prefix = decode_hitobject_prefix(
                ascii, k_zero, prefix_shape, p);
            if (prefix) [[likely]] {
                initialize_hitobject(h, *prefix);
                ++fast_lines;
                const HitObjectKind kind =
                    classify_hitobject_kind(prefix->type);
                if (kind == HitObjectKind::Circle) {
                    // Circle precedence: the tail is empty or ",sample".
                    if (prefix_shape.p4 == len) {
                        sink.sample(h, line_end, 0);
                        ok = true;
                    } else if (len - prefix_shape.p4 == 9 && after == ',' &&
                               short_sample(p + prefix_shape.p4 + 1)) {
                        sink.sample(h, p + prefix_shape.p4 + 1, 8);
                        ok = true;
                    } else {
                        ok = parse_hitobject_details(
                            sink, h, p + prefix_shape.p4, line_end, k);
                    }
                } else if (kind == HitObjectKind::Slider) {
                    ok = prefix_shape.p4 < len && after == ',' &&
                         parse_slider(
                             sink, h, p + prefix_shape.p4 + 1, line_end, k);
                } else {
                    ok = parse_hitobject_details(
                        sink, h, p + prefix_shape.p4, line_end, k);
                }
            } else {
                ok = parse_hitobject_line_scalar(
                    sink, h, p, line_end, k);
            }
            if (ok) sink.commit(h);
            else [[unlikely]] { sink.rollback(h); ++malformed; }
        } else {
            const char c = *p;
            if (c == '\r' || c == '\n') { ++p; continue; }
            if (c == '[') break;
            if (!ignored_line(p, line_end)) {
                auto& h = sink.begin(len);
                if (parse_hitobject_line_scalar(
                        sink, h, p, line_end, k))
                    sink.commit(h);
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

}  // namespace fosu::internal
