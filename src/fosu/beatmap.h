#pragma once

#include <fosu/arena.h>
#include <fosu/beatmap_header.h>
#include <fosu/result.h>
#include <fosu/slider_event.h>
#include <fosu/slider_path.h>
#include <fosu/types.h>

#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace fosu {

struct HitObject {
  f32 x;
  f32 y;
  u32 type;
  u32 hitsound;
  f64 time;
  f64 end_time;  // milliseconds; 0 for sliders unless calculation is requested
  u32 slider;    // index into Beatmap::sliders, or kNoSlider
  bool new_combo;
  u8 combo_skip;
  std::string_view hit_sample;
  std::pair<f32, f32> raw_position(PathPoint stack_offset = {}) const {
    return {x - stack_offset.x, y - stack_offset.y};
  }

  static constexpr u32 kNoSlider = 0xFFFFFFFF;

  bool is_circle() const { return type & 1; }
  bool is_slider() const { return type & 2; }
  bool is_spinner() const { return type & 8; }
  bool is_hold() const { return type & 128; }
  bool is_new_combo() const { return new_combo; }
};

struct SliderPoint {
  f32 x;
  f32 y;
};

struct CurveSegment {
  CurveType type;
  // Present only for an explicit lazer B-spline degree (for example, B2).
  // An absent degree on Bezier means an ordinary Bezier segment.
  std::optional<u32> degree;
  // Range relative to the owning Slider's control-point range.
  u32 point_begin;
  u32 point_count;
};

struct Slider {
  u32 point_begin;  // range into Beatmap::slider_points
  u32 point_count;
  u32 segment_begin;  // range into Beatmap::slider_segments
  u32 segment_count;
  i32 slides;  // 1 = no repeats
  // The only path type for legacy sliders, and the first type for a modern
  // multi-segment path.
  CurveType curve_type;
  f64 length;  // declared pixel length; zero uses the natural path for duration
  std::string_view edge_sounds;
  std::string_view edge_sets;
};

struct TimingPoint {
  f64 time;
  f64 beat_length;
  i32 meter;
  SampleSet sample_set;
  i32 sample_index;
  i32 volume;
  bool uninherited;
  u32 effects;
};

struct Break {
  f64 start;
  f64 end;
};

struct ParseStats {
  u32 fast_path_lines = 0;
  u32 slow_path_lines = 0;
  u32 malformed_lines = 0;
  u32 storyboard_lines = 0;
};

struct Stacking {
  i32 stack_height;
  PathPoint stack_offset;
};

struct Beatmap : BeatmapHeader {
  std::span<Break> breaks;
  std::span<u32> combo_colours;
  std::span<TimingPoint> timing_points;
  std::span<HitObject> hit_objects;
  std::span<Slider> sliders;
  std::span<CurveSegment> slider_segments;
  std::span<SliderPoint> slider_points;
  std::span<f64> velocity_presets;
  // Empty unless requested; otherwise indexed identically to sliders.
  std::span<SliderPath> slider_paths;
  std::span<std::span<SliderEvent>> slider_events;
  // Empty unless osu!standard stacking was requested; indexed by hit object.
  std::span<Stacking> stacking;
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
    auto copied_slider_segments = copy_array(destination, slider_segments);
    auto copied_slider_points = copy_array(destination, slider_points);
    auto copied_velocity_presets = copy_array(destination, velocity_presets);
    auto copied_paths = copy_array(destination, slider_paths);
    auto copied_events = copy_array(destination, slider_events);
    auto copied_stacking = copy_array(destination, stacking);
    if (!copied_breaks || !copied_colours || !copied_timing_points ||
        !copied_hit_objects || !copied_sliders || !copied_slider_segments ||
        !copied_slider_points || !copied_velocity_presets || !copied_paths ||
        !copied_events || !copied_stacking) {
      return rewind_failed_copy(destination, checkpoint);
    }

    auto audio_filename_copy = copy_string(destination, audio_filename);
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
    if (!audio_filename_copy || !overlay_position_copy ||
        !skin_preference_copy || !bookmarks_copy || !title_copy ||
        !title_unicode_copy || !artist_copy || !artist_unicode_copy ||
        !creator_copy || !version_copy || !source_copy || !tags_copy ||
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
    result.slider_segments = copied_slider_segments.value();
    result.slider_points = copied_slider_points.value();
    result.velocity_presets = copied_velocity_presets.value();
    result.slider_paths = copied_paths.value();
    result.slider_events = copied_events.value();
    result.stacking = copied_stacking.value();
    for (auto& events : result.slider_events) {
      auto copy = copy_array(destination, events);
      if (!copy)
        return rewind_failed_copy(destination, checkpoint);
      events = copy.value();
    }
    for (auto& path : result.slider_paths) {
      auto points = copy_array(destination, path.points);
      auto lengths = copy_array(destination, path.cumulative_lengths);
      if (!points || !lengths)
        return rewind_failed_copy(destination, checkpoint);
      path = {points.value(), lengths.value()};
    }
    result.audio_filename = audio_filename_copy.value();
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
  static Result<std::span<T>> copy_array(Arena& destination,
                                         std::span<T> source) noexcept {
    if (source.empty())
      return std::span<T>{};
    T* values = arena_push_array<T>(&destination, source.size());
    if (!values)
      return Error{ErrorCode::AllocationFailure};
    std::memcpy(values, source.data(), source.size_bytes());
    return std::span<T>{values, source.size()};
  }

  static Result<std::string_view> copy_string(
      Arena& destination,
      std::string_view source) noexcept {
    if (source.empty())
      return std::string_view{};
    auto* bytes =
        static_cast<char*>(arena_push(&destination, source.size(), 1));
    if (!bytes)
      return Error{ErrorCode::AllocationFailure};
    std::memcpy(bytes, source.data(), source.size());
    return std::string_view{bytes, source.size()};
  }

  static Result<Beatmap> rewind_failed_copy(Arena& destination,
                                            size_t checkpoint) noexcept {
    arena_pop_to(&destination, checkpoint);
    return Error{ErrorCode::AllocationFailure};
  }
};

}  // namespace fosu
