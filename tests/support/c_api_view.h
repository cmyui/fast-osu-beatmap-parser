#pragma once
// Verification adapter only. The public C API exposes its arrays directly.
#include <fosu/beatmap.h>
#include <fosu/bindings/c_api.h>
#include <span>

struct CApiView : fosu::BeatmapHeader {
  const char* text;
  std::span<const fosu_hit_object> hit_objects;
  std::span<const fosu_slider> sliders;
  std::span<const fosu_point> slider_points;
  std::span<const fosu_timing_point> timing_points;
  std::span<const fosu_break> breaks;
  std::span<const uint32_t> combo_colours;
  fosu::ParseStats stats;
  std::string_view resolve(fosu_string_ref s) const {
    return s.length ? std::string_view(text + s.offset, s.length) : std::string_view{};
  }
  explicit CApiView(const fosu_view& v)
      : text(v.text),
        hit_objects(v.hit_objects, v.hit_object_count),
        sliders(v.sliders, v.slider_count),
        slider_points(v.points, v.point_count),
        timing_points(v.timing_points, v.timing_point_count),
        breaks(v.breaks, v.break_count),
        combo_colours(v.colours, v.colour_count),
        stats{v.stats.fast_path_lines, v.stats.slow_path_lines, v.stats.malformed_lines,
              v.stats.storyboard_lines} {
    format_version = v.metadata.format_version;
    audio_filename = resolve(v.metadata.audio_filename);
    audio_lead_in = v.metadata.audio_lead_in;
    preview_time = v.metadata.preview_time;
    countdown = v.metadata.countdown;
    sample_set = static_cast<fosu::SampleSet>(v.metadata.sample_set);
    stack_leniency = v.metadata.stack_leniency;
    mode = v.metadata.mode;
    letterbox_in_breaks = v.metadata.letterbox_in_breaks;
    widescreen_storyboard = v.metadata.widescreen_storyboard;
    epilepsy_warning = v.metadata.epilepsy_warning;
    special_style = v.metadata.special_style;
    use_skin_sprites = v.metadata.use_skin_sprites;
    samples_match_playback_rate = v.metadata.samples_match_playback_rate;
    countdown_offset = v.metadata.countdown_offset;
    overlay_position = resolve(v.metadata.overlay_position);
    skin_preference = resolve(v.metadata.skin_preference);
    bookmarks = resolve(v.metadata.bookmarks);
    distance_spacing = v.metadata.distance_spacing;
    beat_divisor = v.metadata.beat_divisor;
    grid_size = v.metadata.grid_size;
    timeline_zoom = v.metadata.timeline_zoom;
    title = resolve(v.metadata.title);
    title_unicode = resolve(v.metadata.title_unicode);
    artist = resolve(v.metadata.artist);
    artist_unicode = resolve(v.metadata.artist_unicode);
    creator = resolve(v.metadata.creator);
    version = resolve(v.metadata.version);
    source = resolve(v.metadata.source);
    tags = resolve(v.metadata.tags);
    beatmap_id = v.metadata.beatmap_id;
    beatmap_set_id = v.metadata.beatmap_set_id;
    hp = v.metadata.hp;
    cs = v.metadata.cs;
    od = v.metadata.od;
    ar = v.metadata.ar;
    slider_multiplier = v.metadata.slider_multiplier;
    slider_tick_rate = v.metadata.slider_tick_rate;
    background = resolve(v.metadata.background);
    video = resolve(v.metadata.video);
  }
};
