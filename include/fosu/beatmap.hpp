#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>

#include "arena.hpp"
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
    double end_time;
    uint32_t slider;  // index into Beatmap::sliders, or kNoSlider
    std::string_view hit_sample;

    static constexpr uint32_t kNoSlider = 0xFFFFFFFF;

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
    uint32_t point_count;
    int32_t slides;        // 1 = no repeats
    char curve_type;       // 'B', 'C', 'L', 'P'
    double length;         // pixel length
    std::string_view edge_sounds;
    std::string_view edge_sets;
};

struct TimingPoint {
    double time;
    double beat_length;
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
    uint32_t fast_path_lines = 0;
    uint32_t slow_path_lines = 0;
    uint32_t malformed_lines = 0;
    uint32_t storyboard_lines = 0;
};

struct Beatmap : BeatmapHeader {
    std::span<Break> breaks;
    std::span<uint32_t> combo_colours;
    std::span<TimingPoint> timing_points;
    std::span<HitObject> hit_objects;
    std::span<Slider> sliders;
    std::span<SliderPoint> slider_points;
    ParseStats stats;

    Beatmap copy(Arena& destination) const {
        Beatmap result{};
        static_cast<BeatmapHeader&>(result) =
            static_cast<const BeatmapHeader&>(*this);
        result.stats = stats;
        result.breaks = copy_array(destination, breaks);
        result.combo_colours = copy_array(destination, combo_colours);
        result.timing_points = copy_array(destination, timing_points);
        result.hit_objects = copy_array(destination, hit_objects);
        result.sliders = copy_array(destination, sliders);
        result.slider_points = copy_array(destination, slider_points);

        result.audio_filename = copy_string(destination, audio_filename);
        result.sample_set = copy_string(destination, sample_set);
        result.overlay_position = copy_string(destination, overlay_position);
        result.skin_preference = copy_string(destination, skin_preference);
        result.bookmarks = copy_string(destination, bookmarks);
        result.title = copy_string(destination, title);
        result.title_unicode = copy_string(destination, title_unicode);
        result.artist = copy_string(destination, artist);
        result.artist_unicode = copy_string(destination, artist_unicode);
        result.creator = copy_string(destination, creator);
        result.version = copy_string(destination, version);
        result.source = copy_string(destination, source);
        result.tags = copy_string(destination, tags);
        result.background = copy_string(destination, background);
        result.video = copy_string(destination, video);
        for (auto& object : result.hit_objects)
            object.hit_sample = copy_string(destination, object.hit_sample);
        for (auto& slider : result.sliders) {
            slider.edge_sounds = copy_string(destination, slider.edge_sounds);
            slider.edge_sets = copy_string(destination, slider.edge_sets);
        }
        return result;
    }

private:
    template <typename T>
    static std::span<T> copy_array(
        Arena& destination, std::span<T> source) {
        if (source.empty()) return {};
        T* values = arena_push_array<T>(&destination, source.size());
        if (!values) throw std::bad_alloc();
        std::memcpy(values, source.data(), source.size_bytes());
        return {values, source.size()};
    }

    static std::string_view copy_string(
        Arena& destination, std::string_view source) {
        if (source.empty()) return {};
        auto* bytes = static_cast<char*>(
            arena_push(&destination, source.size(), 1));
        if (!bytes) throw std::bad_alloc();
        std::memcpy(bytes, source.data(), source.size());
        return {bytes, source.size()};
    }
};

}  // namespace fosu
