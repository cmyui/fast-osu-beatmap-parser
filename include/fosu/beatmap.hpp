#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "internal/beatmap_header.hpp"

namespace fosu {

// Field order of the first 16 bytes is load-bearing: the AVX2 hitobject
// fast path stores its result vector directly over {x, y, type, hitsound}.
struct HitObject {
    int32_t x;
    int32_t y;
    uint32_t type;
    uint32_t hitsound;
    double time;
    double end_time;      // spinners and mania holds; 0 otherwise
    uint32_t slider;       // index into Beatmap::sliders, or kNoSlider
    std::string_view hit_sample;

    static constexpr uint32_t kNoSlider = 0xFFFFFFFF;

    // Tag construction that skips value-init: the parser writes every
    // field on both the fast and fallback paths, so emplace_back()'s
    // 56-byte zero-fill per object is pure waste (measured in the
    // disassembly audit). HitObject{} still zero-initializes.
    struct uninit_t {};
    HitObject() = default;
    explicit HitObject(uninit_t) {}

    bool is_circle() const { return type & 1; }
    bool is_slider() const { return type & 2; }
    bool is_spinner() const { return type & 8; }
    bool is_hold() const { return type & 128; }
    bool is_new_combo() const { return type & 4; }
};

struct SliderPoint {
    int32_t x;
    int32_t y;

    // Default construction leaves x and y indeterminate: the parser sizes
    // the point pool with resize() and then writes every element, so the
    // value-initialization a plain aggregate would get is pure waste
    // (measured ~3% of parse time). SliderPoint{} is therefore NOT
    // zeroed; write SliderPoint{0, 0}.
    SliderPoint() {}
    SliderPoint(int32_t x_, int32_t y_) : x(x_), y(y_) {}
};

struct Slider {
    uint32_t point_begin;  // range into Beatmap::slider_points
    uint32_t point_count;  // control points, excluding the head position
    int32_t slides;        // 1 = no repeats
    char curve_type;       // 'B', 'C', 'L', 'P'
    double length;         // pixel length
    std::string_view edge_sounds;  // raw "2|0|0" (parse on demand)
    std::string_view edge_sets;    // raw "0:0|0:0|0:0"

    // See HitObject::uninit_t: the parser emplaces a Slider and fills
    // every field in place. Slider{} still zero-initializes.
    struct uninit_t {};
    Slider() = default;
    explicit Slider(uninit_t) {}
};

struct TimingPoint {
    double time;
    double beat_length;    // ms per beat, or negative SV percentage if inherited
    int32_t meter;
    int32_t sample_set;
    int32_t sample_index;
    int32_t volume;
    bool uninherited;
    uint32_t effects;
};

struct Break {
    double start;
    double end;
};

struct ParseStats {
    uint32_t fast_path_lines = 0;    // hitobject prefixes taken by the SIMD path
    uint32_t slow_path_lines = 0;    // hitobject prefixes that fell back to scalar
    uint32_t malformed_lines = 0;    // lines skipped as unparseable
    uint32_t storyboard_lines = 0;   // event lines ignored (sprites, samples, ...)
};

struct NativeRecords {
    using HitObject = fosu::HitObject;
    using Slider = fosu::Slider;
    using SliderPoint = fosu::SliderPoint;
    using TimingPoint = fosu::TimingPoint;
    using Break = fosu::Break;
    void set_input(const char*) {}
    std::string_view view(std::string_view s) const { return s; }
    std::string_view resolve(std::string_view s) const { return s; }
};

// The ordinary library and C ABI instantiate the same parsing algorithm.
// Record storage is selected at compile time; no virtual calls or ABI macros.
// `Vec` is the array container: std::vector for the public types, an
// arena-backed container inside the C ABI handle.
template <typename Records, template <typename...> class Vec = std::vector>
struct BasicBeatmap : BeatmapHeader, Records {
    using RecordPolicy = Records;
    using HitObject = typename Records::HitObject;
    using Slider = typename Records::Slider;
    using SliderPoint = typename Records::SliderPoint;
    using TimingPoint = typename Records::TimingPoint;
    using Break = typename Records::Break;
    Vec<Break> breaks;

    // [Colours]
    Vec<uint32_t> combo_colours;  // 0xRRGGBB in file order

    // [TimingPoints] / [HitObjects]
    Vec<TimingPoint> timing_points;
    Vec<HitObject> hit_objects;
    Vec<Slider> sliders;
    Vec<SliderPoint> slider_points;  // shared pool, ranged by Slider

    ParseStats stats;
};

struct Beatmap : BasicBeatmap<NativeRecords> {};

}  // namespace fosu
