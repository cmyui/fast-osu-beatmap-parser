#pragma once
// Private, versioned comparison stream. Keep floating-point bits, pool indices
// and optional results; tests/support/decode.py reads the same layout.

#include <fosu/beatmap.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace fosu_dump {

struct Out {
  std::string& s;
  size_t size() const { return s.size(); }
  void raw(const void* p, size_t n) {
    s.append(static_cast<const char*>(p), n);
  }
  void u8(uint8_t v) { s.push_back(static_cast<char>(v)); }
  void i32(int32_t v) { raw(&v, 4); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i64(int64_t v) { raw(&v, 8); }
  void f32(float v) { raw(&v, 4); }
  void f64(double v) { raw(&v, 8); }
  void str(std::string_view v) {
    u32(static_cast<uint32_t>(v.size()));
    raw(v.data(), v.size());
  }
};

template <typename Map, typename String>
inline std::string_view resolve(const Map& bm, String s) {
  if constexpr (requires { bm.resolve(s); })
    return bm.resolve(s);
  else
    return s;
}

template <typename Map, typename Output>
inline void dump_to(const Map& bm, Output& o) {
  o.raw("FOSUDMP9", 8);
  for (const auto& h : bm.hit_objects) {
    const auto sample = resolve(bm, h.hit_sample);
    o.f32(h.x);
    o.f32(h.y);
    o.u32(h.type);
    o.u32(h.hitsound);
    o.f64(h.time);
    o.f64(h.end_time);
    o.u32(h.slider);
    o.u8(h.new_combo);
    o.u8(h.combo_skip);
    o.u32(static_cast<uint32_t>(sample.size()));
    if (h.slider != fosu::HitObject::kNoSlider) {
      const auto& s = bm.sliders[h.slider];
      o.u32(s.point_begin);
      o.u32(s.point_count);
      o.u32(s.segment_begin);
      o.u32(s.segment_count);
      for (uint32_t i = 0; i < s.point_count; ++i) {
        const auto& p = bm.slider_points[s.point_begin + i];
        o.f32(p.x);
        o.f32(p.y);
      }
      o.i32(s.slides);
      o.f64(s.length);
      o.u8(static_cast<uint8_t>(s.curve_type));
      o.str(resolve(bm, s.edge_sounds));
      o.str(resolve(bm, s.edge_sets));
    }
    o.raw(sample.data(), sample.size());
  }
  const size_t trailer_begin = o.size();
  o.raw("TRLR", 4);
  o.u32(static_cast<uint32_t>(bm.hit_objects.size()));
  o.u32(static_cast<uint32_t>(bm.sliders.size()));
  o.u32(static_cast<uint32_t>(bm.slider_points.size()));
  o.i32(bm.format_version);
  o.str(bm.audio_filename);
  o.i32(bm.audio_lead_in);
  o.i32(bm.preview_time);
  o.i32(bm.countdown);
  constexpr std::string_view sample_names[] = {"None", "Normal", "Soft",
                                               "Drum"};
  o.str(sample_names[static_cast<int>(bm.sample_set)]);
  o.f64(bm.stack_leniency);
  o.i32(bm.mode);
  o.u8(bm.letterbox_in_breaks);
  o.u8(bm.widescreen_storyboard);
  o.u8(bm.epilepsy_warning);
  o.u8(bm.special_style);
  o.u8(bm.use_skin_sprites);
  o.u8(bm.samples_match_playback_rate);
  o.i32(bm.countdown_offset);
  o.str(bm.overlay_position);
  o.str(bm.skin_preference);
  o.str(bm.bookmarks);
  o.f64(bm.distance_spacing);
  o.i32(bm.beat_divisor);
  o.i32(bm.grid_size);
  o.f64(bm.timeline_zoom);
  o.str(bm.title);
  o.str(bm.title_unicode);
  o.str(bm.artist);
  o.str(bm.artist_unicode);
  o.str(bm.creator);
  o.str(bm.version);
  o.str(bm.source);
  o.str(bm.tags);
  o.i64(bm.beatmap_id);
  o.i64(bm.beatmap_set_id);
  o.f64(bm.hp);
  o.f64(bm.cs);
  o.f64(bm.od);
  o.f64(bm.ar);
  o.f64(bm.slider_multiplier);
  o.f64(bm.slider_tick_rate);
  o.str(bm.background);
  o.str(bm.video);
  o.u32(static_cast<uint32_t>(bm.breaks.size()));
  for (const auto& b : bm.breaks) {
    o.f64(b.start);
    o.f64(b.end);
  }
  o.u32(static_cast<uint32_t>(bm.combo_colours.size()));
  for (uint32_t c : bm.combo_colours)
    o.u32(c);
  o.u32(static_cast<uint32_t>(bm.timing_points.size()));
  for (const auto& t : bm.timing_points) {
    o.f64(t.time);
    o.f64(t.beat_length);
    o.i32(t.meter);
    o.i32(static_cast<int32_t>(t.sample_set));
    o.i32(t.sample_index);
    o.i32(t.volume);
    o.u8(t.uninherited);
    o.u32(t.effects);
  }
  o.u32(static_cast<uint32_t>(bm.velocity_presets.size()));
  for (fosu::f64 preset : bm.velocity_presets)
    o.f64(preset);
  o.u32(static_cast<uint32_t>(bm.slider_segments.size()));
  for (const auto& segment : bm.slider_segments) {
    o.u8(static_cast<fosu::u8>(segment.type));
    o.u8(segment.degree.has_value());
    o.u32(segment.degree.value_or(0));
    o.u32(segment.point_begin);
    o.u32(segment.point_count);
  }
  o.u32(static_cast<uint32_t>(bm.slider_paths.size()));
  for (const auto& path : bm.slider_paths) {
    o.u32(static_cast<uint32_t>(path.points.size()));
    for (const auto& point : path.points) {
      o.f32(point.x);
      o.f32(point.y);
    }
    for (fosu::f64 length : path.cumulative_lengths)
      o.f64(length);
  }
  o.u32(static_cast<uint32_t>(bm.slider_events.size()));
  for (const auto& events : bm.slider_events) {
    o.u32(static_cast<uint32_t>(events.size()));
    for (const auto& event : events) {
      o.u8(static_cast<fosu::u8>(event.type));
      o.f64(event.time);
      o.i32(event.span_index);
      o.f64(event.span_start_time);
      o.f64(event.path_progress);
      o.f32(event.position.x);
      o.f32(event.position.y);
    }
  }
  o.u32(static_cast<uint32_t>(bm.stacking.size()));
  for (const auto& stacking : bm.stacking) {
    o.i32(stacking.stack_height);
    o.f32(stacking.stack_offset.x);
    o.f32(stacking.stack_offset.y);
  }
  o.u32(bm.stats.malformed_lines);
  o.u32(bm.stats.storyboard_lines);
  o.u32(bm.stats.fast_path_lines);
  o.u32(bm.stats.slow_path_lines);
  // Parser-produced slider ranges are disjoint and in point-pool order.
  size_t covered = 0;
  for (const auto& slider : bm.sliders)
    covered += slider.point_count;
  o.u32(static_cast<uint32_t>(bm.slider_points.size() - covered));
  size_t     point = 0;
  const auto orphan = [&] {
    o.u32(static_cast<uint32_t>(point));
    o.f32(bm.slider_points[point].x);
    o.f32(bm.slider_points[point].y);
    ++point;
  };
  for (const auto& slider : bm.sliders) {
    while (point < slider.point_begin)
      orphan();
    point += slider.point_count;
  }
  while (point < bm.slider_points.size())
    orphan();
  o.i64(static_cast<int64_t>(o.size() - trailer_begin));
}

template <typename Map>
inline void dump(const Map& bm, std::string& out) {
  Out output{out};
  dump_to(bm, output);
}

}  // namespace fosu_dump
