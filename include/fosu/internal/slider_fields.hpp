#pragma once

#include <optional>
#include <string_view>

#include "object_tail.hpp"
#include "slider.hpp"

namespace fosu::internal {

struct SliderExtras {
    std::string_view edge_sounds;
    std::string_view edge_sets;
    std::string_view hit_sample;
};

// Fields after length are positional; absent fields are empty and any
// columns after hitSample are ignored. Views borrow the padded input.
inline SliderExtras parse_slider_extras(const char* p, const char* end,
                                        [[maybe_unused]] const HitConsts& k) {
    if (p == end) return {};
    ++p;  // comma after length
#if FOSU_SIMD
    const auto span = static_cast<size_t>(end - p);
    if (span <= 32) {
        const auto commas = equal_mask32(load32(p), k.comma) &
                            static_cast<uint32_t>((1ull << span) - 1);
        const uint32_t first = trailing_zeros(commas);
        const uint32_t rest = commas & (commas - 1);
        const uint32_t second = trailing_zeros(rest);
        const uint32_t third = trailing_zeros(rest & (rest - 1));
        if (first >= span) return {{p, span}, {}, {}};
        if (second >= span)
            return {{p, first}, {p + first + 1, span - first - 1}, {}};
        const auto sample_end = third < span ? third : static_cast<uint32_t>(span);
        return {{p, first}, {p + first + 1, second - first - 1},
                {p + second + 1, sample_end - second - 1}};
    }
#endif
    std::string_view fields[3];
    for (auto& field : fields) {
        const auto* comma = static_cast<const char*>(memchr(p, ',', end - p));
        const char* field_end = comma ? comma : end;
        field = {p, static_cast<size_t>(field_end - p)};
        if (!comma) break;
        p = comma + 1;
    }
    return {fields[0], fields[1], fields[2]};
}

struct SliderFields {
    int32_t slides;
    double length;
    SliderExtras extras;
};

// Parses ",slides[,length[,edgeSounds,edgeSets,hitSample]]" after the
// point list. A missing length defaults to zero; an explicitly empty one
// is invalid. Validation has no effect on the point pool or hitobject.
// Inline so the returned fields can flow directly into the destination record.
__attribute__((always_inline))
inline std::optional<SliderFields> parse_slider_fields(const char* p, const char* end,
                                                       const HitConsts& k) {
    if (p >= end || *p != ',') return std::nullopt;
    ++p;
    // Editor files normally write one or two bare digits.
    int32_t slides;
    const uint32_t first = static_cast<uint8_t>(p[0] - '0');
    const uint32_t second = static_cast<uint8_t>(p[1] - '0');
    if (first <= 9 && p[1] == ',') {
        slides = static_cast<int32_t>(first);
        p += 1;
    } else if (first <= 9 && second <= 9 && p[2] == ',') {
        slides = static_cast<int32_t>(first * 10 + second);
        p += 2;
    } else {
        const uint32_t run = digit_run8(p);
        if (run - 1 <= 6) {
            slides = static_cast<int32_t>(swar_parse_u64(p, run));
            p = skip_numeric_space(p + run, end);
        } else {
            int64_t wide;
            const char* next = parse_osu_int(p, end, wide);
            if (next == p) return std::nullopt;
            slides = clamp_i32(wide);
            p = next;
        }
    }
    if (slides > 9000 || (p < end && *p != ',')) return std::nullopt;
    double length = 0;
    if (p < end) {
#if FOSU_SIMD
        const char* next = parse_slider_length(p + 1, length, k);
        if (!next) next = parse_osu_double(p + 1, end, length, 131072);
#else
        const char* next = parse_osu_double(p + 1, end, length, 131072);
#endif
        if (next != p + 1) next = skip_numeric_space(next, end);
        if (next == p + 1 || (next < end && *next != ',')) return std::nullopt;
        p = next;
    }
    const auto extras = parse_slider_extras(p, end, k);
    if (!valid_sample(extras.hit_sample, true) || !valid_edge_sets(extras.edge_sets, slides))
        return std::nullopt;
    return SliderFields{slides, length, extras};
}

}  // namespace fosu::internal
