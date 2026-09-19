#pragma once

#include <fosu/enums.h>
#include <fosu/types.h>

#include <string_view>

namespace fosu {

struct BeatmapHeader {
  int format_version = 14;

  // [General]
  std::string_view audio_filename;
  i32 audio_lead_in = 0;
  i32 preview_time = -1;
  i32 countdown = 1;
  SampleSet sample_set = SampleSet::Normal;
  i32 sample_volume = 100;
  f64 stack_leniency = 0.7f;
  i32 mode = 0;
  bool letterbox_in_breaks = false;
  bool widescreen_storyboard = false;
  bool epilepsy_warning = false;
  bool special_style = false;
  bool use_skin_sprites = false;
  bool samples_match_playback_rate = false;
  i32 countdown_offset = 0;
  std::string_view overlay_position;
  std::string_view skin_preference;

  // [Editor]
  std::string_view bookmarks;  // raw comma list
  f64 distance_spacing = 1;
  i32 beat_divisor = 4;
  i32 grid_size = 0;
  f64 timeline_zoom = 1;

  // [Metadata]
  std::string_view title;
  std::string_view title_unicode;
  std::string_view artist;
  std::string_view artist_unicode;
  std::string_view creator;
  std::string_view version;
  std::string_view source;
  std::string_view tags;
  i64 beatmap_id = -1;
  i64 beatmap_set_id = -1;

  // [Difficulty]
  f64 hp = 5;
  f64 cs = 5;
  f64 od = 5;
  f64 ar = 5;
  f64 slider_multiplier = 1.4;
  f64 slider_tick_rate = 1;

  // [Events]
  std::string_view background;
  std::string_view video;
};

}  // namespace fosu
