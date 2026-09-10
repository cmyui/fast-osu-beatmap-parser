#pragma once

#include <fosu/enums.h>
#include <cstdint>
#include <string_view>

namespace fosu {

struct BeatmapHeader {
  int format_version = 14;

  // [General]
  std::string_view audio_filename;
  int32_t audio_lead_in = 0;
  int32_t preview_time = -1;
  int32_t countdown = 1;
  SampleSet sample_set = SampleSet::Normal;
  int32_t sample_volume = 100;
  double stack_leniency = 0.7f;
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
  std::string_view bookmarks;  // raw comma list
  double distance_spacing = 1;
  int32_t beat_divisor = 4;
  int32_t grid_size = 0;
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
  double video_offset = 0;
  float storyboard_background_offset_x = 0;
  float storyboard_background_offset_y = 0;
};

}  // namespace fosu
