#pragma once
// Canonical binary dump of a parsed Beatmap: the "output" a one-shot
// parse process must produce byte-for-byte. Little-endian fixed-width
// fields, strings by value (u32 length + bytes), doubles as raw bit
// patterns, so equality means bit-identical parsing, plus the parser's
// own path counters and the pool points no slider references (left
// behind by slider lines that failed after their point loop), so nothing
// the library's Beatmap holds is outside the comparison. Pool offsets
// are explicit, including each orphan's position. Slider points follow
// their record inline, so the format can be streamed while parsing.
//
//   "FOSUDMP5"
//   hit objects, in file order, each:
//     i32 x, y; u32 type, hitsound; f64 time, end_time; u32 slider
//     (kNoSlider or the running slider index); u32 hit_sample length;
//     if a slider was parsed (slider != kNoSlider): u32 point_begin, point_count;
//     {i32 x, y} x point_count; i32 slides; f64 length; u8 curve_type;
//     str edge_sounds, edge_sets;
//     then the hit_sample bytes (everything is in parse order)
//   trailer:
//     "TRLR" u32 n_hitobjects, n_sliders, n_points (pool size)
//     i32 format_version
//     [General]   str audio_filename; i32 audio_lead_in, preview_time,
//                 countdown; str sample_set; f64 stack_leniency; i32 mode;
//                 u8 letterbox_in_breaks, widescreen_storyboard,
//                 epilepsy_warning, special_style, use_skin_sprites,
//                 samples_match_playback_rate; i32 countdown_offset;
//                 str overlay_position, skin_preference
//     [Editor]    str bookmarks; f64 distance_spacing; i32 beat_divisor,
//                 grid_size; f64 timeline_zoom
//     [Metadata]  str title, title_unicode, artist, artist_unicode,
//                 creator, version, source, tags; i64 beatmap_id,
//                 beatmap_set_id
//     [Difficulty] f64 hp, cs, od, ar, slider_multiplier, slider_tick_rate
//     [Events]    str background, video; u32 n; {f64 start, end} x n
//     [Colours]   u32 n; u32 rgb x n
//     [TimingPoints] u32 n; {f64 time, beat_length; i32 meter, sample_set,
//                 sample_index, volume; u8 uninherited; u32 effects} x n
//     stats       u32 malformed_lines, storyboard_lines, fast_path_lines,
//                 slow_path_lines
//     orphans     u32 n; {u32 pool_index; i32 x, y} x n  (pool points no slider covers,
//                 in pool order)
//     footer      u64 trailer byte length (excluding this footer)

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <fosu/beatmap.h>

namespace fosu_dump {

struct Out {
  std::string& s;
  size_t size() const { return s.size(); }
  void raw(const void* p, size_t n) { s.append(static_cast<const char*>(p), n); }
  void u8(uint8_t v) { s.push_back(static_cast<char>(v)); }
  void i32(int32_t v) { raw(&v, 4); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i64(int64_t v) { raw(&v, 8); }
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
  o.raw("FOSUDMP5", 8);
  for (const auto& h : bm.hit_objects) {
    const auto sample = resolve(bm, h.hit_sample);
    o.i32(h.x);
    o.i32(h.y);
    o.u32(h.type);
    o.u32(h.hitsound);
    o.f64(h.time);
    o.f64(h.end_time);
    o.u32(h.slider);
    o.u32(static_cast<uint32_t>(sample.size()));
    if (h.slider != fosu::HitObject::kNoSlider) {
      const auto& s = bm.sliders[h.slider];
      o.u32(s.point_begin);
      o.u32(s.point_count);
      for (uint32_t i = 0; i < s.point_count; ++i) {
        const auto& p = bm.slider_points[s.point_begin + i];
        o.i32(p.x);
        o.i32(p.y);
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
  constexpr std::string_view sample_names[] = {"None", "Normal", "Soft", "Drum"};
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
  o.u32(bm.stats.malformed_lines);
  o.u32(bm.stats.storyboard_lines);
  o.u32(bm.stats.fast_path_lines);
  o.u32(bm.stats.slow_path_lines);
  // Parser-produced slider ranges are disjoint and in point-pool order.
  size_t covered = 0;
  for (const auto& slider : bm.sliders)
    covered += slider.point_count;
  o.u32(static_cast<uint32_t>(bm.slider_points.size() - covered));
  size_t point = 0;
  const auto orphan = [&] {
    o.u32(static_cast<uint32_t>(point));
    o.i32(bm.slider_points[point].x);
    o.i32(bm.slider_points[point].y);
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
