#pragma once
// Verification format, not a persistent storage format. Covers every
// Beatmap value, including pool indices and counters; excludes capacities,
// pointer addresses, and C++ object padding. Doubles retain their raw bits.
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>

#include <fosu/beatmap.hpp>
static void put(std::string& s, uint64_t v) {
    s.append(reinterpret_cast<const char*>(&v), 8);
}
static void put_string(std::string& s, std::string_view v) {
    put(s, v.size());
    if (!v.empty()) s.append(v);
}
static void put_double(std::string& s, double v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    put(s, b);
}
static std::string serialize(const fosu::Beatmap& bm) {
    std::string h;
    put(h, static_cast<uint64_t>(bm.format_version));
    put_string(h, bm.audio_filename);
    put(h, static_cast<uint32_t>(bm.audio_lead_in));
    put(h, static_cast<uint32_t>(bm.preview_time));
    put(h, static_cast<uint32_t>(bm.countdown));
    put_string(h, bm.sample_set);
    put_double(h, bm.stack_leniency);
    put(h, static_cast<uint32_t>(bm.mode));
    put(h, bm.letterbox_in_breaks);
    put(h, bm.widescreen_storyboard);
    put(h, bm.epilepsy_warning);
    put(h, bm.special_style);
    put(h, bm.use_skin_sprites);
    put(h, bm.samples_match_playback_rate);
    put(h, static_cast<uint32_t>(bm.countdown_offset));
    put_string(h, bm.overlay_position);
    put_string(h, bm.skin_preference);
    put_string(h, bm.bookmarks);
    put_double(h, bm.distance_spacing);
    put(h, static_cast<uint32_t>(bm.beat_divisor));
    put(h, static_cast<uint32_t>(bm.grid_size));
    put_double(h, bm.timeline_zoom);
    put_string(h, bm.title);
    put_string(h, bm.title_unicode);
    put_string(h, bm.artist);
    put_string(h, bm.artist_unicode);
    put_string(h, bm.creator);
    put_string(h, bm.version);
    put_string(h, bm.source);
    put_string(h, bm.tags);
    put(h, static_cast<uint64_t>(bm.beatmap_id));
    put(h, static_cast<uint64_t>(bm.beatmap_set_id));
    put_double(h, bm.hp);
    put_double(h, bm.cs);
    put_double(h, bm.od);
    put_double(h, bm.ar);
    put_double(h, bm.slider_multiplier);
    put_double(h, bm.slider_tick_rate);
    put_string(h, bm.background);
    put_string(h, bm.video);
    put(h, bm.breaks.size());
    for (const auto& b : bm.breaks) {
        put(h, static_cast<uint32_t>(b.start));
        put(h, static_cast<uint32_t>(b.end));
    }
    put(h, bm.combo_colours.size());
    for (uint32_t c : bm.combo_colours) put(h, c);
    put(h, bm.timing_points.size());
    for (const auto& tp : bm.timing_points) {
        put_double(h, tp.time);
        put_double(h, tp.beat_length);
        put(h, static_cast<uint32_t>(tp.meter));
        put(h, static_cast<uint32_t>(tp.sample_set));
        put(h, static_cast<uint32_t>(tp.sample_index));
        put(h, static_cast<uint32_t>(tp.volume));
        put(h, tp.uninherited);
        put(h, tp.effects);
    }
    put(h, bm.hit_objects.size());
    for (const auto& o : bm.hit_objects) {
        put(h, static_cast<uint32_t>(o.x));
        put(h, static_cast<uint32_t>(o.y));
        put(h, o.type);
        put(h, o.hitsound);
        put(h, static_cast<uint32_t>(o.time));
        put(h, static_cast<uint32_t>(o.end_time));
        put(h, o.slider);
        put_string(h, o.hit_sample);
    }
    put(h, bm.sliders.size());
    for (const auto& s : bm.sliders) {
        put(h, s.point_begin);
        put(h, s.point_count);
        put(h, static_cast<uint32_t>(s.slides));
        put_double(h, s.length);
        put(h, static_cast<uint8_t>(s.curve_type));
        put_string(h, s.edge_sounds);
        put_string(h, s.edge_sets);
    }
    put(h, bm.slider_points.size());
    for (const auto& p : bm.slider_points) {
        put(h, static_cast<uint32_t>(p.x));
        put(h, static_cast<uint32_t>(p.y));
    }
    put(h, bm.stats.fast_path_lines);
    put(h, bm.stats.slow_path_lines);
    put(h, bm.stats.malformed_lines);
    put(h, bm.stats.storyboard_lines);
    return h;
}
