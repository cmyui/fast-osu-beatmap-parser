#pragma once

#include "beatmap.hpp"
#include "c_api.h"

namespace fosu {

// A distinct compact type, with full-width coordinates and numeric fields.
// parse_into sets the string base; keep the padded input alive.
// The input span and individual strings must fit in uint32_t.
struct OffsetRecords {
    using HitObject = fosu_hit_object;
    using Slider = fosu_slider;
    using SliderPoint = fosu_point;
    using TimingPoint = fosu_timing_point;
    using Break = fosu_break;
    const char* string_base = nullptr;
    void set_input(const char* p) { string_base = p; }
    fosu_string_ref view(std::string_view s) const {
        return s.empty() ? fosu_string_ref{0, 0}
                         : fosu_string_ref{static_cast<uint32_t>(s.data() - string_base),
                                           static_cast<uint32_t>(s.size())};
    }
    std::string_view resolve(fosu_string_ref s) const {
        return s.length ? std::string_view(string_base + s.offset, s.length)
                        : std::string_view{};
    }
};
struct OffsetBeatmap : BasicBeatmap<OffsetRecords> {};
static_assert(sizeof(fosu_hit_object) == 48 && sizeof(fosu_slider) == 40);
static_assert(sizeof(fosu_point) == 8 && sizeof(fosu_timing_point) == 40);

}  // namespace fosu
