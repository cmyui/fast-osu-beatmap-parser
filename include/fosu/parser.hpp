#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "beatmap.hpp"
#include "hitobject_prefix.hpp"
#include "io.hpp"
#include "scalar_parse.hpp"

namespace fosu {

struct ParseOptions {
    bool use_simd = true;  // false forces the scalar hitobject path (benchmarking)
};

namespace detail {

static_assert(offsetof(HitObject, x) == 0 && offsetof(HitObject, y) == 4 &&
                  offsetof(HitObject, type) == 8 &&
                  offsetof(HitObject, hitsound) == 12,
              "AVX2 prefix path stores {x,y,type,hitsound} as one vector");

enum class Section : uint8_t {
    None,
    General,
    Editor,
    Metadata,
    Difficulty,
    Events,
    TimingPoints,
    Colours,
    HitObjects,
    Unknown,
};

inline std::string_view trim(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return {p, static_cast<size_t>(end - p)};
}

inline Section match_section(std::string_view line) {
    if (line == "[General]") return Section::General;
    if (line == "[Editor]") return Section::Editor;
    if (line == "[Metadata]") return Section::Metadata;
    if (line == "[Difficulty]") return Section::Difficulty;
    if (line == "[Events]") return Section::Events;
    if (line == "[TimingPoints]") return Section::TimingPoints;
    if (line == "[Colours]") return Section::Colours;
    if (line == "[HitObjects]") return Section::HitObjects;
    return Section::Unknown;
}

inline bool split_kv(const char* p, size_t len, std::string_view& key,
                     std::string_view& val) {
    const auto* colon = static_cast<const char*>(memchr(p, ':', len));
    if (!colon) return false;
    key = trim(p, colon);
    val = trim(colon + 1, p + len);
    return !key.empty();
}

inline bool parse_bool(std::string_view v) {
    return !v.empty() && v[0] == '1';
}

inline int32_t parse_i32_field(std::string_view v, int32_t fallback) {
    int64_t out;
    const char* q = parse_i64(v.data(), v.data() + v.size(), out);
    return q == v.data() ? fallback : clamp_i32(out);
}

inline double parse_f64_field(std::string_view v, double fallback) {
    double out;
    const char* q = parse_double(v.data(), v.data() + v.size(), out);
    return q == v.data() ? fallback : out;
}

inline void parse_general_kv(Beatmap& bm, std::string_view k, std::string_view v) {
    switch (k.empty() ? '\0' : k[0]) {
        case 'A':
            if (k == "AudioFilename") bm.audio_filename = v;
            else if (k == "AudioLeadIn") bm.audio_lead_in = parse_i32_field(v, 0);
            break;
        case 'C':
            if (k == "Countdown") bm.countdown = parse_i32_field(v, 1);
            else if (k == "CountdownOffset") bm.countdown_offset = parse_i32_field(v, 0);
            break;
        case 'E':
            if (k == "EpilepsyWarning") bm.epilepsy_warning = parse_bool(v);
            break;
        case 'L':
            if (k == "LetterboxInBreaks") bm.letterbox_in_breaks = parse_bool(v);
            break;
        case 'M':
            if (k == "Mode") bm.mode = parse_i32_field(v, 0);
            break;
        case 'O':
            if (k == "OverlayPosition") bm.overlay_position = v;
            break;
        case 'P':
            if (k == "PreviewTime") bm.preview_time = parse_i32_field(v, -1);
            break;
        case 'S':
            if (k == "SampleSet") bm.sample_set = v;
            else if (k == "StackLeniency") bm.stack_leniency = parse_f64_field(v, 0.7);
            else if (k == "SkinPreference") bm.skin_preference = v;
            else if (k == "SpecialStyle") bm.special_style = parse_bool(v);
            else if (k == "SamplesMatchPlaybackRate")
                bm.samples_match_playback_rate = parse_bool(v);
            break;
        case 'U':
            if (k == "UseSkinSprites") bm.use_skin_sprites = parse_bool(v);
            break;
        case 'W':
            if (k == "WidescreenStoryboard") bm.widescreen_storyboard = parse_bool(v);
            break;
        default:
            break;
    }
}

inline void parse_editor_kv(Beatmap& bm, std::string_view k, std::string_view v) {
    if (k == "Bookmarks") bm.bookmarks = v;
    else if (k == "DistanceSpacing") bm.distance_spacing = parse_f64_field(v, 0);
    else if (k == "BeatDivisor") bm.beat_divisor = parse_i32_field(v, 4);
    else if (k == "GridSize") bm.grid_size = parse_i32_field(v, 4);
    else if (k == "TimelineZoom") bm.timeline_zoom = parse_f64_field(v, 1);
}

inline void parse_metadata_kv(Beatmap& bm, std::string_view k, std::string_view v) {
    switch (k.empty() ? '\0' : k[0]) {
        case 'T':
            if (k == "Title") bm.title = v;
            else if (k == "TitleUnicode") bm.title_unicode = v;
            else if (k == "Tags") bm.tags = v;
            break;
        case 'A':
            if (k == "Artist") bm.artist = v;
            else if (k == "ArtistUnicode") bm.artist_unicode = v;
            break;
        case 'C':
            if (k == "Creator") bm.creator = v;
            break;
        case 'V':
            if (k == "Version") bm.version = v;
            break;
        case 'S':
            if (k == "Source") bm.source = v;
            break;
        case 'B': {
            int64_t id;
            if (k == "BeatmapID") {
                if (parse_i64(v.data(), v.data() + v.size(), id) != v.data())
                    bm.beatmap_id = id;
            } else if (k == "BeatmapSetID") {
                if (parse_i64(v.data(), v.data() + v.size(), id) != v.data())
                    bm.beatmap_set_id = id;
            }
            break;
        }
        default:
            break;
    }
}

inline void parse_difficulty_kv(Beatmap& bm, std::string_view k, std::string_view v,
                                bool& ar_specified) {
    if (k == "HPDrainRate") bm.hp = parse_f64_field(v, 5);
    else if (k == "CircleSize") bm.cs = parse_f64_field(v, 5);
    else if (k == "OverallDifficulty") bm.od = parse_f64_field(v, 5);
    else if (k == "ApproachRate") {
        bm.ar = parse_f64_field(v, 5);
        ar_specified = true;
    } else if (k == "SliderMultiplier")
        bm.slider_multiplier = parse_f64_field(v, 1.4);
    else if (k == "SliderTickRate")
        bm.slider_tick_rate = parse_f64_field(v, 1);
}

inline std::string_view strip_quotes(std::string_view v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        return v.substr(1, v.size() - 2);
    return v;
}

inline void parse_event_line(Beatmap& bm, const char* p, size_t len) {
    // Storyboard commands are indented; count and skip them.
    if (len == 0 || *p == ' ' || *p == '_') {
        ++bm.stats.storyboard_lines;
        return;
    }
    const char* end = p + len;
    const auto* c1 = static_cast<const char*>(memchr(p, ',', len));
    if (!c1) {
        ++bm.stats.storyboard_lines;
        return;
    }
    const std::string_view f0{p, static_cast<size_t>(c1 - p)};
    const char* rest = c1 + 1;
    if (f0 == "0") {
        // 0,0,"bg.jpg",xOffset,yOffset
        const auto* c2 = static_cast<const char*>(memchr(rest, ',', end - rest));
        if (!c2) return;
        const char* fname = c2 + 1;
        const auto* c3 = static_cast<const char*>(memchr(fname, ',', end - fname));
        const char* fend = c3 ? c3 : end;
        bm.background = strip_quotes(trim(fname, fend));
    } else if (f0 == "1" || f0 == "Video") {
        const auto* c2 = static_cast<const char*>(memchr(rest, ',', end - rest));
        if (!c2) return;
        const char* fname = c2 + 1;
        const auto* c3 = static_cast<const char*>(memchr(fname, ',', end - fname));
        const char* fend = c3 ? c3 : end;
        bm.video = strip_quotes(trim(fname, fend));
    } else if (f0 == "2" || f0 == "Break") {
        double start, stop;
        const char* q = parse_double(rest, end, start);
        if (q == rest || q >= end || *q != ',') return;
        const char* r = parse_double(q + 1, end, stop);
        if (r == q + 1) return;
        bm.breaks.push_back({clamp_i32(static_cast<int64_t>(start)),
                             clamp_i32(static_cast<int64_t>(stop))});
    } else {
        ++bm.stats.storyboard_lines;
    }
}

inline void parse_colour_kv(Beatmap& bm, std::string_view k, std::string_view v) {
    if (k.substr(0, 5) != "Combo") return;
    const char* p = v.data();
    const char* end = p + v.size();
    uint32_t rgb = 0;
    for (int i = 0; i < 3; ++i) {
        int64_t c;
        const char* q = parse_i64(p, end, c);
        if (q == p) return;
        p = q;
        if (i < 2) {
            if (p >= end || *p != ',') return;
            ++p;
            while (p < end && *p == ' ') ++p;
        }
        rgb = (rgb << 8) | (static_cast<uint32_t>(c) & 0xFF);
    }
    bm.combo_colours.push_back(rgb);
}

inline void parse_timing_point_line(Beatmap& bm, const char* p, size_t len) {
    const char* end = p + len;
    TimingPoint tp{0, 0, 4, 0, 0, 100, true, 0};
    const char* q = parse_double(p, end, tp.time);
    if (q == p) {
        ++bm.stats.malformed_lines;
        return;
    }
    p = q;
    if (p >= end || *p != ',') {
        ++bm.stats.malformed_lines;
        return;
    }
    q = parse_double(++p, end, tp.beat_length);
    if (q == p) {
        ++bm.stats.malformed_lines;
        return;
    }
    p = q;
    // Remaining fields are optional (old format versions have fewer).
    int64_t rest[6] = {4, 0, 0, 100, 1, 0};
    for (auto& field : rest) {
        if (p >= end || *p != ',') break;
        q = parse_i64(++p, end, field);
        if (q == p) break;
        p = q;
    }
    tp.meter = clamp_i32(rest[0]);
    tp.sample_set = clamp_i32(rest[1]);
    tp.sample_index = clamp_i32(rest[2]);
    tp.volume = clamp_i32(rest[3]);
    tp.uninherited = rest[4] != 0;
    tp.effects = static_cast<uint32_t>(rest[5]);
    bm.timing_points.push_back(tp);
}

// Slider control point coordinate: overwhelmingly 1-4 plain digits, parsed
// branchlessly via SWAR. Signs, 5+ digit values, and empty fields take the
// general path. Returns the advanced pointer, or `p` unchanged on failure.
inline const char* parse_coord(const char* p, const char* end, int32_t& out) {
    const uint32_t run = digit_run8(p);
    if (run - 1 <= 3) {  // 1..4 digits; a run never crosses `end` (the line
                         // terminator and buffer padding are non-digits)
        out = static_cast<int32_t>(swar_parse_u32(p, run));
        return p + run;
    }
    int64_t v;
    const char* q = parse_i64(p, end, v);
    if (q == p) return p;
    out = clamp_i32(v);
    return q;
}

// Slider params: curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
inline bool parse_slider_params(Beatmap& bm, HitObject& h, const char* p,
                                const char* end) {
    if (p >= end) return false;
    Slider s{};
    s.curve_type = *p++;
    s.point_begin = static_cast<uint32_t>(bm.slider_points.size());
    while (p < end && *p == '|') {
        int32_t px, py;
        const char* q = parse_coord(p + 1, end, px);
        if (q == p + 1 || q >= end || *q != ':') return false;
        const char* r = parse_coord(q + 1, end, py);
        if (r == q + 1) return false;
        bm.slider_points.push_back({px, py});
        p = r;
    }
    s.point_count =
        static_cast<uint32_t>(bm.slider_points.size()) - s.point_begin;
    if (p >= end || *p != ',') return false;
    int64_t slides;
    const char* q = parse_i64(p + 1, end, slides);
    if (q == p + 1) return false;
    s.slides = clamp_i32(slides);
    p = q;
    if (p >= end || *p != ',') return false;
    q = parse_double(p + 1, end, s.length);
    if (q == p + 1) return false;
    p = q;
    // Optional: edgeSounds, edgeSets, hitSample (assigned positionally).
    std::string_view extra[3];
    int n = 0;
    while (n < 3 && p < end && *p == ',') {
        ++p;
        const auto* c = static_cast<const char*>(memchr(p, ',', end - p));
        const char* fend = c ? c : end;
        extra[n++] = {p, static_cast<size_t>(fend - p)};
        p = fend;
    }
    s.edge_sounds = extra[0];
    s.edge_sets = extra[1];
    h.hit_sample = extra[2];
    h.slider = static_cast<uint32_t>(bm.sliders.size());
    bm.sliders.push_back(s);
    return true;
}

inline void parse_hitobject_line(Beatmap& bm, const char* line, size_t len,
                                 size_t bytes_remaining, bool use_simd) {
    bm.hit_objects.emplace_back();
    HitObject& h = bm.hit_objects.back();
    h.end_time = 0;
    h.slider = HitObject::kNoSlider;
    h.hit_sample = {};

    int next = -1;
#if FOSU_SIMD_X86
    if (use_simd) {
        next = fast_parse_prefix(line, h);
        if (next >= 0) ++bm.stats.fast_path_lines;
    }
#else
    (void)use_simd;
#endif
    if (next < 0) {
        next = scalar_parse_prefix(line, len, h);
        if (next < 0) {
            bm.hit_objects.pop_back();
            ++bm.stats.malformed_lines;
            return;
        }
        ++bm.stats.slow_path_lines;
    }

    const char* p = line + next;
    const char* end = line + len;
    const bool ok = [&] {
        if (h.type & 2) {  // slider
            if (p >= end || *p != ',') return false;
            // Size the slider pools once, when a map first proves it has
            // sliders — reserving eagerly per map wastes multi-MB
            // allocations on slider-free maps, which costs more than the
            // reallocations it saves.
            if (bm.sliders.capacity() == 0) {
                bm.slider_points.reserve(bytes_remaining / 14);
                bm.sliders.reserve(bytes_remaining / 48);
            }
            return parse_slider_params(bm, h, p + 1, end);
        }
        if (h.type & 8 || h.type & 128) {  // spinner / mania hold
            if (p >= end || *p != ',') return false;
            int64_t t;
            const char* q = parse_i64(p + 1, end, t);
            if (q == p + 1) return false;
            h.end_time = clamp_i32(t);
            p = q;
            if ((h.type & 128) && p < end && *p == ':') ++p;
            else if (p < end && *p == ',') ++p;
            else {
                h.hit_sample = {};
                return true;
            }
            h.hit_sample = {p, static_cast<size_t>(end - p)};
            return true;
        }
        // circle: optional trailing hitSample
        if (p < end && *p == ',')
            h.hit_sample = {p + 1, static_cast<size_t>(end - (p + 1))};
        return true;
    }();
    if (!ok) {
        bm.hit_objects.pop_back();
        ++bm.stats.malformed_lines;
    }
}

}  // namespace detail

// `data` must be followed by kBufferPadding readable zero bytes (io.hpp).
// String fields of the result view into `data`; keep the buffer alive.
inline Beatmap parse(const char* data, size_t size, ParseOptions opts = {}) {
    using namespace detail;
    Beatmap bm;
    const char* p = data;
    const char* file_end = data + size;
    if (size >= 3 && static_cast<uint8_t>(p[0]) == 0xEF &&
        static_cast<uint8_t>(p[1]) == 0xBB && static_cast<uint8_t>(p[2]) == 0xBF)
        p += 3;

    Section sec = Section::None;
    bool ar_specified = false;

    while (p < file_end) {
        const char* nl;
#if FOSU_SIMD_X86 && !defined(FOSU_NO_LINE_PROBE)
        // Most lines fit in one 32-byte probe (the buffer padding contains
        // no '\n', so hits are always within the file); longer lines fall
        // through to memchr for the remainder.
        const auto probe = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const auto nl_mask = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(probe, _mm256_set1_epi8('\n'))));
        if (nl_mask)
            nl = p + _tzcnt_u32(nl_mask);
        else if (file_end - p <= 32)
            nl = nullptr;
        else
            nl = static_cast<const char*>(
                memchr(p + 32, '\n', static_cast<size_t>(file_end - p) - 32));
#else
        nl = static_cast<const char*>(memchr(p, '\n', file_end - p));
#endif
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const size_t len = static_cast<size_t>(line_end - p);

        if (len == 0) goto next_line;
        if (*p == '[') {
            sec = match_section({p, len});
            if (sec == Section::HitObjects)
                bm.hit_objects.reserve(
                    bm.hit_objects.size() +
                    static_cast<size_t>(file_end - line_end) / 24);
            else if (sec == Section::TimingPoints)
                bm.timing_points.reserve(256);
            goto next_line;
        }
        if (len >= 2 && p[0] == '/' && p[1] == '/') goto next_line;

        switch (sec) {
            case Section::None: {
                const std::string_view line{p, len};
                const size_t v = line.find("osu file format v");
                if (v != std::string_view::npos) {
                    int64_t ver;
                    const char* vp = p + v + 17;
                    if (parse_i64(vp, line_end, ver) != vp)
                        bm.format_version = static_cast<int>(ver);
                }
                break;
            }
            case Section::General:
            case Section::Editor:
            case Section::Metadata:
            case Section::Difficulty:
            case Section::Colours: {
                std::string_view k, v;
                if (!split_kv(p, len, k, v)) break;
                if (sec == Section::General) parse_general_kv(bm, k, v);
                else if (sec == Section::Editor) parse_editor_kv(bm, k, v);
                else if (sec == Section::Metadata) parse_metadata_kv(bm, k, v);
                else if (sec == Section::Difficulty)
                    parse_difficulty_kv(bm, k, v, ar_specified);
                else parse_colour_kv(bm, k, v);
                break;
            }
            case Section::Events:
                parse_event_line(bm, p, len);
                break;
            case Section::TimingPoints:
                parse_timing_point_line(bm, p, len);
                break;
            case Section::HitObjects:
                parse_hitobject_line(bm, p, len,
                                     static_cast<size_t>(file_end - p),
                                     opts.use_simd);
                break;
            case Section::Unknown:
                break;
        }

    next_line:
        p = nl ? nl + 1 : file_end;
    }

    // Old format versions omit ApproachRate; it mirrors OverallDifficulty.
    if (!ar_specified) bm.ar = bm.od;
    return bm;
}

inline Beatmap parse(const FileBuffer& buf, ParseOptions opts = {}) {
    return parse(buf.data.get(), buf.size, opts);
}

}  // namespace fosu
