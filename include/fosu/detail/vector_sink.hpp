#pragma once
#include "hitobjects.hpp"
#include "vector_storage.hpp"

namespace fosu::detail {

// Storage policy for the ordinary library: records are written into the
// Beatmap's vectors through raw cursors (direct mode) and published once per
// section, so the per-record cost is a bounds check and a pointer bump.
// Growth publishes, reserves and reopens the affected cursor. Slider pools
// are sized on the first slider, so slider-free maps allocate none.
template <typename Map>
struct VectorSink {
    using HitObject = typename Map::HitObject;
    using Slider = typename Map::Slider;
    using Point = typename Map::SliderPoint;
    static constexpr bool kDirect = direct_vector_writes_v<decltype(Map::hit_objects)>;
    Map& bm;
    HitObject* hw = nullptr;
    HitObject* hend = nullptr;
    Slider* sw = nullptr;
    Slider* send = nullptr;
    Point* pw = nullptr;
    Point* pend = nullptr;
    size_t remaining;  // section bytes: sizes the slider pools on first use

    VectorSink(Map& m, size_t section_bytes) : bm(m), remaining(section_bytes) {
        open_objects(); open_sliders(); open_points();
    }
    void open_objects() {
        hw = hend = bm.hit_objects.data();
        if (hw) { hw += bm.hit_objects.size(); hend += bm.hit_objects.capacity(); }
    }
    void open_sliders() {
        sw = send = bm.sliders.data();
        if (sw) { sw += bm.sliders.size(); send += bm.sliders.capacity(); }
    }
    void open_points() {
        pw = pend = bm.slider_points.data();
        if (pw) { pw += bm.slider_points.size(); pend += bm.slider_points.capacity(); }
    }
    // Makes every record written so far visible through the vectors.
    void publish() {
        if constexpr (kDirect) {
            if (hw) publish_size(bm.hit_objects, static_cast<size_t>(hw - bm.hit_objects.data()));
            if (sw) publish_size(bm.sliders, static_cast<size_t>(sw - bm.sliders.data()));
            if (pw) publish_size(bm.slider_points, static_cast<size_t>(pw - bm.slider_points.data()));
        } else if (pw) {
            bm.slider_points.resize(static_cast<size_t>(pw - bm.slider_points.data()));
        }
    }
    __attribute__((noinline)) void grow_objects() {
        publish();
        bm.hit_objects.reserve(bm.hit_objects.capacity() * 2 + 64);
        open_objects();
    }
    __attribute__((noinline)) void grow_sliders() {
        publish();
        const size_t cap = bm.sliders.capacity();
        bm.sliders.reserve(cap ? cap * 2 + 16 : remaining / 48 + 16);
        open_sliders();
    }
    __attribute__((noinline)) void grow_points(size_t bound) {
        publish();
        const size_t cap = bm.slider_points.capacity();
        size_t want = cap ? cap * 2 : remaining / 14;
        if (want < bm.slider_points.size() + bound) want = bm.slider_points.size() + bound + 64;
        bm.slider_points.reserve(want);
        open_points();
    }

    HitObject& begin(size_t) {
        if constexpr (kDirect) {
            if (hw == hend) [[unlikely]] grow_objects();
            construct_record(hw);
        } else {
            bm.hit_objects.emplace_back(typename HitObject::uninit_t{});
            hw = &bm.hit_objects.back();
        }
        return *hw;
    }
    void commit(HitObject&) {
        if constexpr (kDirect) ++hw;
    }
    void rollback(HitObject&) {
        if constexpr (!kDirect) bm.hit_objects.pop_back();
    }
    void sample(HitObject& h, const char* s, size_t n) { h.hit_sample = bm.view({s, n}); }
    auto view(const char* s, size_t n) { return bm.view({s, n}); }
    Point* point_slot(size_t bound) {
        if constexpr (kDirect) {
            if (!pw || static_cast<size_t>(pend - pw) < bound) [[unlikely]] grow_points(bound);
            for (size_t i = 0; i < bound; ++i) construct_record(pw + i);
        } else {
            const size_t base = pw ? static_cast<size_t>(pw - bm.slider_points.data()) : 0;
            if (bm.slider_points.capacity() == 0) bm.slider_points.reserve(remaining / 14);
            bm.slider_points.resize(base + bound);
            pw = bm.slider_points.data() + base;
            pend = pw + bound;
        }
        return pw;
    }
    uint32_t point_index(Point* w) const {
        return static_cast<uint32_t>(w - bm.slider_points.data());
    }
    void slider_rollback(Point*, Point* w_end, bool keep_points) {
        if (keep_points) pw = w_end;
        if constexpr (!kDirect)
            bm.slider_points.resize(static_cast<size_t>(pw - bm.slider_points.data()));
    }
    using SliderRecord = Slider;
    Slider& slider_slot() {
        if constexpr (kDirect) {
            if (sw == send) [[unlikely]] grow_sliders();
            construct_record(sw);
        } else {
            bm.sliders.emplace_back(typename Slider::uninit_t{});
            sw = &bm.sliders.back();
        }
        return *sw;
    }
    void slider_commit(HitObject& h, Slider&, Point* w_end, const char* hs, size_t hs_len) {
        if constexpr (!kDirect)
            bm.slider_points.resize(static_cast<size_t>(w_end - bm.slider_points.data()));
        h.hit_sample = bm.view({hs, hs_len});
        h.slider = static_cast<uint32_t>(sw - bm.sliders.data());
        if constexpr (kDirect) ++sw;
        pw = w_end;
    }
    ParseStats& stats() { return bm.stats; }
};

// Parses one [HitObjects] section starting at `p` (after its header line).
template <typename Map>
inline const char* parse_hitobjects_section(Map& bm, const char* p, const char* file_end,
                                            bool use_simd) {
    VectorSink<Map> sink(bm, static_cast<size_t>(file_end - p));
    const HitConsts k;
#if FOSU_SIMD_X86
    if (use_simd) p = parse_hitobject_lines(sink, p, file_end, k);
    else
#else
    (void)use_simd;
#endif
        p = parse_hitobject_lines_scalar(sink, p, file_end, k);
    sink.publish();
    return p;
}

// Resets bm for reuse: every field returns to its default, but vector
// capacity is kept, so the steady state of a parse-many loop allocates
// nothing and touches no new pages.
template <typename Map>
inline void reset_for_reuse(Map& bm) {
    auto breaks = std::move(bm.breaks);
    auto colours = std::move(bm.combo_colours);
    auto tps = std::move(bm.timing_points);
    auto objs = std::move(bm.hit_objects);
    auto sliders = std::move(bm.sliders);
    auto points = std::move(bm.slider_points);
    bm = Map{};
    breaks.clear();
    colours.clear();
    tps.clear();
    objs.clear();
    sliders.clear();
    points.clear();
    bm.breaks = std::move(breaks);
    bm.combo_colours = std::move(colours);
    bm.timing_points = std::move(tps);
    bm.hit_objects = std::move(objs);
    bm.sliders = std::move(sliders);
    bm.slider_points = std::move(points);
}

}  // namespace fosu::detail
