#pragma once

#include <fosu/arena.h>
#include <fosu/beatmap_header.h>
#include <fosu/enums.h>
#include <fosu/slider_event.h>
#include <fosu/slider_path.h>
#include <fosu/types.h>

#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace fosu {

struct HitObject {
  f32              x;
  f32              y;
  u32              type;
  u32              hitsound;
  f64              time;
  f64              end_time;  // ms; 0 for sliders if no calc requested
  u32              slider;    // index into Beatmap::sliders, or kNoSlider
  bool             new_combo;
  u8               combo_skip;
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
  CurveType          type;
  std::optional<u32> degree;  // Present only for lazer B-spline degree
  u32                point_begin;
  u32                point_count;
};

struct Slider {
  u32              point_begin;  // range into Beatmap::slider_points
  u32              point_count;
  u32              segment_begin;  // range into Beatmap::slider_segments
  u32              segment_count;
  i32              slides;  // 1 = no repeats
  // The only path type for legacy sliders, and the first type for a modern
  // multi-segment path.
  CurveType        curve_type;
  f64              length;  // declared pixel length; zero uses the natural path
  std::string_view edge_sounds;
  std::string_view edge_sets;
};

struct TimingPoint {
  f64       time;
  f64       beat_length;
  i32       meter;
  SampleSet sample_set;
  i32       sample_index;
  i32       volume;
  bool      uninherited;
  u32       effects;
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
  i32       stack_height;
  PathPoint stack_offset;
};

struct Beatmap : BeatmapHeader {
  std::span<Break>                  breaks;
  std::span<u32>                    combo_colours;
  std::span<TimingPoint>            timing_points;
  std::span<HitObject>              hit_objects;
  std::span<Slider>                 sliders;
  std::span<CurveSegment>           slider_segments;
  std::span<SliderPoint>            slider_points;
  std::span<f64>                    velocity_presets;

  // Empty unless requested; otherwise indexed identically to sliders.
  std::span<SliderPath>             slider_paths;
  std::span<std::span<SliderEvent>> slider_events;

  // Empty unless osu!standard stacking was requested; indexed by hit object.
  std::span<Stacking>               stacking;
  ParseStats                        stats;
};

}  // namespace fosu
