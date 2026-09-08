#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include <fosu/arena.h>
#include <fosu/beatmap_header.h>
#include <fosu/result.h>

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

    Result<Beatmap> copy(Arena& destination) const noexcept {
        const size_t checkpoint = arena_pos(&destination);
        Beatmap result{};
        static_cast<BeatmapHeader&>(result) =
            static_cast<const BeatmapHeader&>(*this);
        result.stats = stats;

        auto copied_breaks = copy_array(destination, breaks);
        auto copied_colours = copy_array(destination, combo_colours);
        auto copied_timing_points = copy_array(destination, timing_points);
        auto copied_hit_objects = copy_array(destination, hit_objects);
        auto copied_sliders = copy_array(destination, sliders);
        auto copied_slider_points = copy_array(destination, slider_points);
        if (!copied_breaks || !copied_colours || !copied_timing_points ||
            !copied_hit_objects || !copied_sliders ||
            !copied_slider_points) {
            return rewind_failed_copy(destination, checkpoint);
        }

        auto audio_filename_copy = copy_string(destination, audio_filename);
        auto sample_set_copy = copy_string(destination, sample_set);
        auto overlay_position_copy = copy_string(destination, overlay_position);
        auto skin_preference_copy = copy_string(destination, skin_preference);
        auto bookmarks_copy = copy_string(destination, bookmarks);
        auto title_copy = copy_string(destination, title);
        auto title_unicode_copy = copy_string(destination, title_unicode);
        auto artist_copy = copy_string(destination, artist);
        auto artist_unicode_copy = copy_string(destination, artist_unicode);
        auto creator_copy = copy_string(destination, creator);
        auto version_copy = copy_string(destination, version);
        auto source_copy = copy_string(destination, source);
        auto tags_copy = copy_string(destination, tags);
        auto background_copy = copy_string(destination, background);
        auto video_copy = copy_string(destination, video);
        if (!audio_filename_copy || !sample_set_copy ||
            !overlay_position_copy || !skin_preference_copy ||
            !bookmarks_copy || !title_copy || !title_unicode_copy ||
            !artist_copy || !artist_unicode_copy || !creator_copy ||
            !version_copy || !source_copy || !tags_copy ||
            !background_copy || !video_copy) {
            return rewind_failed_copy(destination, checkpoint);
        }

        auto mutable_hit_objects = copied_hit_objects.value();
        for (auto& object : mutable_hit_objects) {
            auto hit_sample_copy = copy_string(destination, object.hit_sample);
            if (!hit_sample_copy)
                return rewind_failed_copy(destination, checkpoint);
            object.hit_sample = hit_sample_copy.value();
        }

        auto mutable_sliders = copied_sliders.value();
        for (auto& slider : mutable_sliders) {
            auto edge_sounds_copy = copy_string(destination, slider.edge_sounds);
            auto edge_sets_copy = copy_string(destination, slider.edge_sets);
            if (!edge_sounds_copy || !edge_sets_copy)
                return rewind_failed_copy(destination, checkpoint);
            slider.edge_sounds = edge_sounds_copy.value();
            slider.edge_sets = edge_sets_copy.value();
        }

        result.breaks = copied_breaks.value();
        result.combo_colours = copied_colours.value();
        result.timing_points = copied_timing_points.value();
        result.hit_objects = mutable_hit_objects;
        result.sliders = mutable_sliders;
        result.slider_points = copied_slider_points.value();
        result.audio_filename = audio_filename_copy.value();
        result.sample_set = sample_set_copy.value();
        result.overlay_position = overlay_position_copy.value();
        result.skin_preference = skin_preference_copy.value();
        result.bookmarks = bookmarks_copy.value();
        result.title = title_copy.value();
        result.title_unicode = title_unicode_copy.value();
        result.artist = artist_copy.value();
        result.artist_unicode = artist_unicode_copy.value();
        result.creator = creator_copy.value();
        result.version = version_copy.value();
        result.source = source_copy.value();
        result.tags = tags_copy.value();
        result.background = background_copy.value();
        result.video = video_copy.value();
        return result;
    }

private:
    template <typename T>
    static Result<std::span<T>> copy_array(
        Arena& destination, std::span<T> source) noexcept {
        if (source.empty()) return std::span<T>{};
        T* values = arena_push_array<T>(&destination, source.size());
        if (!values) return Error{ErrorCode::AllocationFailure};
        std::memcpy(values, source.data(), source.size_bytes());
        return std::span<T>{values, source.size()};
    }

    static Result<std::string_view> copy_string(
        Arena& destination, std::string_view source) noexcept {
        if (source.empty()) return std::string_view{};
        auto* bytes = static_cast<char*>(
            arena_push(&destination, source.size(), 1));
        if (!bytes) return Error{ErrorCode::AllocationFailure};
        std::memcpy(bytes, source.data(), source.size());
        return std::string_view{bytes, source.size()};
    }

    static Result<Beatmap> rewind_failed_copy(
        Arena& destination, size_t checkpoint) noexcept {
        arena_pop_to(&destination, checkpoint);
        return Error{ErrorCode::AllocationFailure};
    }
};

}  // namespace fosu
