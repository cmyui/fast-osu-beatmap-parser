#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace fosu {

// Field order of the first 16 bytes is load-bearing: the AVX2 hitobject
// fast path stores its result vector directly over {x, y, type, hitsound}.
struct HitObject {
    int32_t x;
    int32_t y;
    uint32_t type;
    uint32_t hitsound;
    int32_t time;
    int32_t end_time;      // spinners and mania holds; 0 otherwise
    uint32_t slider;       // index into Beatmap::sliders, or kNoSlider
    std::string_view hit_sample;

    static constexpr uint32_t kNoSlider = 0xFFFFFFFF;

    // Tag construction that skips value-init: the parser writes every
    // field on both the fast and fallback paths, so emplace_back()'s
    // 48-byte zero-fill per object is pure waste (measured in the
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
};

struct Slider {
    uint32_t point_begin;  // range into Beatmap::slider_points
    uint32_t point_count;  // control points, excluding the head position
    int32_t slides;        // 1 = no repeats
    double length;         // pixel length
    char curve_type;       // 'B', 'C', 'L', 'P'
    std::string_view edge_sounds;  // raw "2|0|0" (parse on demand)
    std::string_view edge_sets;    // raw "0:0|0:0|0:0"
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
    int32_t start;
    int32_t end;
};

struct ParseStats {
    uint32_t fast_path_lines = 0;    // hitobject prefixes taken by the SIMD path
    uint32_t slow_path_lines = 0;    // hitobject prefixes that fell back to scalar
    uint32_t malformed_lines = 0;    // lines skipped as unparseable
    uint32_t storyboard_lines = 0;   // event lines ignored (sprites, samples, ...)
};

struct Beatmap {
    int format_version = 14;

    // [General]
    std::string_view audio_filename;
    int32_t audio_lead_in = 0;
    int32_t preview_time = -1;
    int32_t countdown = 1;
    std::string_view sample_set = "Normal";
    double stack_leniency = 0.7;
    int32_t mode = 0;
    bool letterbox_in_breaks = false;
    bool widescreen_storyboard = false;
    bool epilepsy_warning = false;
    bool special_style = false;
    bool use_skin_sprites = false;
    bool samples_match_playback_rate = false;
    int32_t countdown_offset = 0;
    std::string_view overlay_position;
    std::string_view skin_preference;

    // [Editor]
    std::string_view bookmarks;    // raw comma list
    double distance_spacing = 0;
    int32_t beat_divisor = 4;
    int32_t grid_size = 4;
    double timeline_zoom = 1;

    // [Metadata]
    std::string_view title;
    std::string_view title_unicode;
    std::string_view artist;
    std::string_view artist_unicode;
    std::string_view creator;
    std::string_view version;
    std::string_view source;
    std::string_view tags;
    int64_t beatmap_id = -1;
    int64_t beatmap_set_id = -1;

    // [Difficulty]
    double hp = 5;
    double cs = 5;
    double od = 5;
    double ar = 5;
    double slider_multiplier = 1.4;
    double slider_tick_rate = 1;

    // [Events]
    std::string_view background;
    std::string_view video;
    std::vector<Break> breaks;

    // [Colours]
    std::vector<uint32_t> combo_colours;  // 0xRRGGBB in file order

    // [TimingPoints] / [HitObjects]
    std::vector<TimingPoint> timing_points;
    std::vector<HitObject> hit_objects;
    std::vector<Slider> sliders;
    std::vector<SliderPoint> slider_points;  // shared pool, ranged by Slider

    ParseStats stats;
};

}  // namespace fosu
