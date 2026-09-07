#pragma once
#include "detail/object_tail.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "beatmap.hpp"
#include "hitobject_prefix.hpp"
#include "detail/timing.hpp"
#include "detail/slider.hpp"
#include "detail/metadata.hpp"
#include "detail/sections.hpp"
#include "detail/section_names.hpp"
#include "io.hpp"
#include "scalar_parse.hpp"

namespace fosu {

// Section-selection bits for ParseOptions::sections. A caller that only
// needs, say, OverallDifficulty can parse just [Difficulty]: unwanted
// sections are skipped with a single memchr jump (no line iteration) and
// parsing stops entirely once every requested section has been consumed
// — [Difficulty] lives in the first ~2KB of a file whose remaining ~98%
// is hit objects, timing and events, so a difficulty-only parse is
// ~20-30x cheaper than a full one.
enum : uint32_t {
    kSectionGeneral = 1u << 1,
    kSectionEditor = 1u << 2,
    kSectionMetadata = 1u << 3,
    kSectionDifficulty = 1u << 4,
    kSectionEvents = 1u << 5,
    kSectionTimingPoints = 1u << 6,
    kSectionColours = 1u << 7,
    kSectionHitObjects = 1u << 8,
    kAllSections = 0xFFFFFFFFu,
};

struct ParseOptions {
    bool use_simd = true;  // false forces the scalar hitobject path (benchmarking)
    uint32_t sections = kAllSections;  // bitmask of kSection*
};

namespace detail {

static_assert(offsetof(HitObject, x) == 0 && offsetof(HitObject, y) == 4 &&
                  offsetof(HitObject, type) == 8 &&
                  offsetof(HitObject, hitsound) == 12,
              "AVX2 prefix path stores {x,y,type,hitsound} as one vector");

static_assert(kSectionGeneral == 1u << static_cast<int>(Section::General) &&
                  kSectionDifficulty ==
                      1u << static_cast<int>(Section::Difficulty) &&
                  kSectionHitObjects ==
                      1u << static_cast<int>(Section::HitObjects),
              "public section bits mirror the internal Section ordinals");

inline std::string_view trim(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return {p, static_cast<size_t>(end - p)};
}

inline bool split_kv(const char* p, size_t len, std::string_view& key,
                     std::string_view& val) {
    const auto* colon = static_cast<const char*>(memchr(p, ':', len));
    if (!colon) return false;
    key = trim(p, colon);
    val = trim(colon + 1, p + len);
    return !key.empty();
}

template <typename Map>
inline void parse_general_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kGeneral, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_editor_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kEditor, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_metadata_line(Map& bm, const char* p, size_t len) {
    parse_kv_line<parse_double>(bm, kMetadata, p, len, &bm.stats.malformed_lines);
}
template <typename Map>
inline void parse_difficulty_line(Map& bm, const char* p, size_t len, bool& ar_specified) {
    ar_specified |= parse_kv_line<parse_double>(bm, kDifficulty, p, len, &bm.stats.malformed_lines);
}

inline std::string_view strip_quotes(std::string_view v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        return v.substr(1, v.size() - 2);
    return v;
}

template <typename Map>
inline void parse_event_line(Map& bm, const char* p, size_t len) {
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
        const char* q = fosu::detail::parse_osu_double(rest, end, start);
        if (q == rest || q >= end || *q != ',') { ++bm.stats.malformed_lines; return; }
        const char* r = fosu::detail::parse_osu_double(q + 1, end, stop);
        if (r == q + 1 || r != end) { ++bm.stats.malformed_lines; return; }
        bm.breaks.push_back({start, stop});
    } else {
        ++bm.stats.storyboard_lines;
    }
}

template <typename Map>
inline void parse_colour_kv(Map& bm, std::string_view k, std::string_view v) {
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

template <typename Map>
inline void parse_timing_point_line(Map& bm, const char* p, size_t len) {
    typename Map::TimingPoint tp;
    if (parse_timing_fields(p, p + len, tp)) bm.timing_points.push_back(tp);
    else ++bm.stats.malformed_lines;
}

#if FOSU_SIMD_X86

// Fused [TimingPoints] section loop: the same two loads serve the newline
// scan and the parser, and the section is sized exactly once — the next
// '[' bounds it, so reserve never over-allocates for short sections nor
// grows for marathon ones (growth reallocs plus resize's value-init
// memsets measured worse than the push_back they replaced). Returns the
// position after the section.
template <typename Map>
inline const char* parse_timing_points_section(Map& bm, const char* p,
                                               const char* file_end) {
    auto& tps = bm.timing_points;
    const auto* bracket = static_cast<const char*>(
        memchr(p, '[', static_cast<size_t>(file_end - p)));
    const char* section_end = bracket ? bracket : file_end;
    tps.reserve(tps.size() + static_cast<size_t>(section_end - p) / 17 + 4);
    TpShapeCache cache{};
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        if (c == '[') break;

        const __m256i a =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const __m256i b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
        const uint64_t nl =
            static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(a, _mm256_set1_epi8('\n')))) |
            static_cast<uint64_t>(static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n')))))
                << 32;
        const char* next_line;
        size_t len;
        if (nl) {
            const auto pos = static_cast<uint32_t>(_tzcnt_u64(nl));
            len = pos - (pos > 0 && p[pos - 1] == '\r');
            next_line = p + pos + 1;
        } else {
            const auto* m = static_cast<const char*>(
                memchr(p, '\n', static_cast<size_t>(file_end - p)));
            const char* le = m ? m : file_end;
            len = static_cast<size_t>(le - p) - (le > p && le[-1] == '\r');
            next_line = m ? m + 1 : file_end;
        }

        if (fosu::detail::ignored_line(p, p + len)) { p = next_line; continue; }
        typename Map::TimingPoint tp;
        if (len <= 64 && len >= 15) [[likely]] {
            const uint64_t line_mask =
                len == 64 ? ~0ull : ((1ull << len) - 1);
            const uint64_t commas =
                (comma_mask32(a) |
                 static_cast<uint64_t>(comma_mask32(b)) << 32) &
                line_mask;
            const uint64_t nondig =
                (nondigit_mask32(a) |
                 static_cast<uint64_t>(nondigit_mask32(b)) << 32) &
                line_mask;
            const TpShapeRow& row =
                cache.rows[TpShapeCache::slot(commas)];
            if (tp_shape_match(row, commas, nondig, len, p)) {
                tp_shape_convert(row, p, tp);
                tps.push_back(tp);
            } else {
                TpGeom geom;
                if (fast_parse_timing_point_masked(commas, nondig, p, len,
                                                   tp, &geom)) {
                    tp_shape_insert(cache, commas, nondig, len, geom);
                    tps.push_back(tp);
                } else {
                    parse_timing_point_line(bm, p, len);
                }
            }
        } else {
            parse_timing_point_line(bm, p, len);
        }
        p = next_line;
    }
    return p;
}
#endif  // FOSU_SIMD_X86

// Slider params: curveType|x:y|x:y...,slides,length[,edgeSounds,edgeSets][,hitSample]
//
// The Slider is emplaced first and filled in place (popped again if the
// line turns out malformed): a stack temporary handed to push_back costs
// a 64-byte zero-fill plus a 64-byte copy on every slider.
template <typename Map>
inline bool parse_slider_params(Map& bm, typename Map::HitObject& h, const char* p,
                                const char* end) {
    if (p >= end) return false;
    auto& sliders = bm.sliders;
    sliders.emplace_back(typename Map::Slider::uninit_t{});
    auto& s = sliders.back();
    s.curve_type = *p++;

    // Points are written straight into the pool through a raw cursor —
    // one bounds ensure per slider instead of a checked push per point.
    // A point pair costs at least 4 bytes ("|x:y"), which bounds the count.
    // SliderPoint's no-op default constructor makes both resizes free.
    auto& pts = bm.slider_points;
    const size_t base = pts.size();
    pts.resize(base + static_cast<size_t>(end - p) / 4 + 1);
    auto* w = pts.data() + base;
    if (!parse_slider_points(p, end, w)) {
        pts.resize(base);
        sliders.pop_back();
        return false;
    }
    pts.resize(static_cast<size_t>(w - pts.data()));
    s.point_begin = static_cast<uint32_t>(base);
    s.point_count = static_cast<uint32_t>(pts.size() - base);

    if (p >= end || *p != ',') {
        sliders.pop_back();
        return false;
    }
    ++p;
    const uint32_t srun = digit_run8(p);  // slides: a bare small integer
    if (srun - 1 <= 6) {
        s.slides = static_cast<int32_t>(swar_parse_u64(p, srun));
        p = fosu::detail::skip_numeric_space(p + srun, end);
    } else {
        int64_t slides;
        const char* next = parse_osu_int(p, end, slides);
        if (next == p) { sliders.pop_back(); return false; }
        s.slides = clamp_i32(slides);
        p = next;
    }
    if (s.slides > 9000 || (p < end && *p != ',')) {
        sliders.pop_back();
        return false;
    }
    s.length = 0;
    if (p < end) {
#if FOSU_SIMD_X86
        const char* q = parse_slider_length(p + 1, s.length);
        if (!q) q = parse_osu_double(p + 1, end, s.length, 131072);
#else
        const char* q = parse_osu_double(p + 1, end, s.length, 131072);
#endif
        if (q != p + 1) q = skip_numeric_space(q, end);
        if (q == p + 1 || (q < end && *q != ',') || s.length > 131072 || s.length < -131072) {
            sliders.pop_back();
            return false;
        }
        p = q;
    }

    // Optional: edgeSounds, edgeSets, hitSample, assigned positionally;
    // absent fields are empty.
    s.edge_sounds = {};
    s.edge_sets = {};
    std::string_view hit_sample{};
    if (p < end && *p == ',') {
        ++p;
#if FOSU_SIMD_X86
        const auto span = static_cast<size_t>(end - p);
        if (span <= 32) {
            // Both remaining comma positions from one 32-byte scan.
            const __m256i v =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
            const auto cm =
                static_cast<uint32_t>(_mm256_movemask_epi8(
                    _mm256_cmpeq_epi8(v, _mm256_set1_epi8(',')))) &
                static_cast<uint32_t>((1ull << span) - 1);
            const uint32_t c0 = _tzcnt_u32(cm);
            const uint32_t c1 = _tzcnt_u32(_blsr_u32(cm));
            if (c0 >= span) {
                s.edge_sounds = bm.view({p, span});
            } else if (c1 >= span) {
                s.edge_sounds = bm.view({p, c0});
                s.edge_sets = bm.view({p + c0 + 1, span - c0 - 1});
            } else {
                s.edge_sounds = bm.view({p, c0});
                s.edge_sets = bm.view({p + c0 + 1, c1 - c0 - 1});
                hit_sample = {p + c1 + 1, span - c1 - 1};
            }
        } else
#endif
        {
            std::string_view extra[3];
            int n = 0;
            while (n < 3 && p < end) {
                const auto* c =
                    static_cast<const char*>(memchr(p, ',', end - p));
                const char* fend = c ? c : end;
                extra[n++] = {p, static_cast<size_t>(fend - p)};
                p = fend + 1;
            }
            s.edge_sounds = bm.view(extra[0]);
            s.edge_sets = bm.view(extra[1]);
            hit_sample = extra[2];
        }
    }
    if (!valid_sample(hit_sample, true) ||
        !valid_edge_sets(bm.resolve(s.edge_sets), s.slides)) {
        sliders.pop_back();
        return false;
    }
    h.hit_sample = bm.view(hit_sample);
    h.slider = static_cast<uint32_t>(sliders.size() - 1);
    return true;
}

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, trailing hitSample.
template <typename Map>
inline bool finish_hitobject(Map& bm, typename Map::HitObject& h, const char* p,
                             const char* end, size_t bytes_remaining) {
    if (!(h.type & (1 | 2 | 8 | 128))) return false;
    if (p < end && *p != ',') return false;
    if (!(h.type & 1) && (h.type & 2)) {  // slider
        if (p >= end || *p != ',') return false;
        // Size the slider pools once, when a map first proves it has
        // sliders — reserving eagerly per map wastes multi-MB allocations
        // on slider-free maps, which costs more than the reallocations it
        // saves.
        if (bm.sliders.capacity() == 0) {
            bm.slider_points.reserve(bytes_remaining / 14);
            bm.sliders.reserve(bytes_remaining / 48);
        }
        return parse_slider_params(bm, h, p + 1, end);
    }
    std::string_view sample;
    if (!parse_object_tail(h, p, end, sample)) return false;
    h.hit_sample = bm.view(sample);
    return true;
}

// Scalar-only per-line path; the SIMD build routes [HitObjects] through
// parse_hitobjects_section instead.
template <typename Map>
inline void parse_hitobject_line(Map& bm, const char* line, size_t len,
                                 size_t bytes_remaining) {
    bm.hit_objects.emplace_back(typename Map::HitObject::uninit_t{});
    auto& h = bm.hit_objects.back();
    h.end_time = 0;
    h.slider = Map::HitObject::kNoSlider;

    const int next = scalar_parse_prefix(line, len, h);
    if (next < 0) {
        bm.hit_objects.pop_back();
        ++bm.stats.malformed_lines;
        return;
    }
    ++bm.stats.slow_path_lines;

    if (!finish_hitobject(bm, h, line + next, line + len, bytes_remaining)) {
        bm.hit_objects.pop_back();
        ++bm.stats.malformed_lines;
    }
}

#if FOSU_SIMD_X86
template <typename Map>
struct MaterializedHits {
    using HitObject = typename Map::HitObject;
    Map& bm;
    HitObject& begin(size_t) {
        bm.hit_objects.emplace_back(typename HitObject::uninit_t{});
        return bm.hit_objects.back();
    }
    bool finish(HitObject& h, const char* p, const char* end, size_t remaining) {
        return finish_hitobject(bm, h, p, end, remaining);
    }
    void commit(HitObject&) {}
    void rollback(HitObject&) { bm.hit_objects.pop_back(); }
    auto& stats() { return bm.stats; }
};
template <typename Map>
inline const char* parse_hitobjects_section(Map& bm, const char* p, const char* file_end) {
    MaterializedHits<Map> sink{bm};
    return parse_hitobject_lines(sink, p, file_end);
}

// Fused [Events] section loop. Storyboard command lines — indented, and
// ~12% of all lines in the popular corpus — are counted and skipped on
// their first byte; every line finds its end with vector compares (two
// 32-byte windows cover 64 bytes) instead of a memchr call. Returns the
// position after the section.
template <typename Map>
inline const char* parse_events_section(Map& bm, const char* p,
                                        const char* file_end) {
    uint32_t storyboard_lines = 0;
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        if (c == '[') break;

        const __m256i a =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const __m256i b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
        const uint64_t nl =
            static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(a, _mm256_set1_epi8('\n')))) |
            static_cast<uint64_t>(static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n')))))
                << 32;
        const char* line = p;
        const char* next_line;
        const char* line_end;
        if (nl) {
            line_end = p + _tzcnt_u64(nl);
            next_line = line_end + 1;
        } else {
            const auto* m = static_cast<const char*>(memchr(
                p + 64, '\n',
                file_end - p > 64 ? static_cast<size_t>(file_end - p) - 64
                                  : 0));
            line_end = m ? m : file_end;
            next_line = m ? m + 1 : file_end;
        }
        if (line_end[-1] == '\r') --line_end;  // line_end > line: c is not CR
        p = next_line;

        if (fosu::detail::ignored_line(line, line_end)) continue;
        if (c == ' ' || c == '_') {  // indented storyboard command
            ++storyboard_lines;
            continue;
        }
        const auto len = static_cast<size_t>(line_end - line);
        if (len >= 2 && c == '/' && line[1] == '/') continue;  // comment
        parse_event_line(bm, line, len);
    }
    bm.stats.storyboard_lines += storyboard_lines;
    return p;
}
#endif  // FOSU_SIMD_X86

// Resets bm for reuse: every field returns to its default, but vector
// capacity is kept, so the steady state of a parse-many loop allocates
// nothing and touches no new pages.
template <typename Map>
inline void reset_for_reuse(Map& bm) {
    auto breaks = std::move(bm.breaks);
    auto colours = std::move(bm.combo_colours);
    auto tps = std::move(bm.timing_points);
    auto objs = std::move(bm.hit_objects);
    auto sliders = std::move(bm.sliders);
    auto points = std::move(bm.slider_points);
    bm = Map{};
    breaks.clear();
    colours.clear();
    tps.clear();
    objs.clear();
    sliders.clear();
    points.clear();
    bm.breaks = std::move(breaks);
    bm.combo_colours = std::move(colours);
    bm.timing_points = std::move(tps);
    bm.hit_objects = std::move(objs);
    bm.sliders = std::move(sliders);
    bm.slider_points = std::move(points);
}

}  // namespace detail

// `data` must be followed by kBufferPadding readable zero bytes (io.hpp).
// String fields of the result view into `data`; keep the buffer alive.
// parse_into clears bm (keeping vector capacity) and fills it; pass the
// same Beatmap across calls to parse many files without allocating.
template <typename Map>
inline void parse_into(const char* data, size_t size, Map& bm,
                       [[maybe_unused]] ParseOptions opts = {}) {
    using namespace detail;
    if (size > kMaxInputSize) throw std::length_error("beatmap input exceeds 64 MiB");
    reset_for_reuse(bm);
    bm.set_input(data);
    if (size == 0) return;
    const char* p = data;
    const char* file_end = data + size;
    if (size >= 3 && static_cast<uint8_t>(p[0]) == 0xEF &&
        static_cast<uint8_t>(p[1]) == 0xBB && static_cast<uint8_t>(p[2]) == 0xBF)
        p += 3;

    Section sec = Section::None;
    bool ar_specified = false;
    // Wanted sections not yet consumed; once empty, any further unwanted
    // header ends the parse.
    uint32_t pending = opts.sections & 0x1FEu;

    while (p < file_end) {
        // The fused section loops consume [TimingPoints]/[HitObjects] —
        // 95%+ of file bytes — so this loop only walks headers, metadata,
        // and events, where per-line memchr is free. (A 32-byte SIMD line
        // probe here measured exactly zero in the ablation audit: its
        // value was eroded to nothing when the fused sections landed.)
        if (*p == '\r' || *p == '\n') { ++p; continue; }
        const char* nl =
            static_cast<const char*>(memchr(p, '\n', file_end - p));
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const size_t len = static_cast<size_t>(line_end - p);

        if (len == 0) goto next_line;
        if (*p == '[') {
            sec = match_section({p, len});
            const uint32_t sec_bit = 1u << static_cast<int>(sec);
            if (!(opts.sections & sec_bit)) {
                if (pending == 0) break;  // everything wanted is done
                const char* start = nl ? nl + 1 : file_end;
                const auto* nb = static_cast<const char*>(memchr(
                    start, '[', static_cast<size_t>(file_end - start)));
                // A bracket inside a value/comment is not a section header.
                while (nb && nb != start && nb[-1] != '\n' && nb[-1] != '\r') {
                    nb = static_cast<const char*>(memchr(
                        nb + 1, '[', static_cast<size_t>(file_end - nb - 1)));
                }
                p = nb ? nb : file_end;
                sec = Section::Unknown;
                continue;
            }
            pending &= ~sec_bit;
            if (sec == Section::HitObjects) {
                // /16: the shortest hitobject line observed across 167
                // popular ranked maps is 15 bytes + newline; a smaller
                // divisor only over-reserves (untouched pages are free),
                // while under-reserving costs a full-array growth memmove.
                bm.hit_objects.reserve(
                    bm.hit_objects.size() +
                    static_cast<size_t>(file_end - line_end) / 16);
#if FOSU_SIMD_X86
                if (opts.use_simd) {
                    p = parse_hitobjects_section(bm, nl ? nl + 1 : file_end,
                                                 file_end);
                    sec = Section::Unknown;
                    continue;
                }
#endif
            } else if (sec == Section::TimingPoints) {
#if FOSU_SIMD_X86
                if (opts.use_simd) {
                    p = parse_timing_points_section(
                        bm, nl ? nl + 1 : file_end, file_end);
                    sec = Section::Unknown;
                    continue;
                }
#endif
                bm.timing_points.reserve(256);
            }
#if FOSU_SIMD_X86
            else if (sec == Section::Events && opts.use_simd) {
                p = parse_events_section(bm, nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
            }
#endif
            goto next_line;
        }
        if (fosu::detail::ignored_line(p, line_end)) goto next_line;

        switch (sec) {
            case Section::None: {
                const std::string_view line{p, len};
                const size_t v = line.find("osu file format v");
                if (v != std::string_view::npos) {
                    int64_t ver;
                    const char* vp = p + v + 17;
                    if (parse_i64(vp, line_end, ver) != vp)
                        bm.format_version = clamp_i32(ver);
                }
                break;
            }
            case Section::General:
                if (len >= 5) parse_general_line(bm, p, len);
                break;
            case Section::Editor:
                if (len >= 5) parse_editor_line(bm, p, len);
                break;
            case Section::Metadata:
                if (len >= 5) parse_metadata_line(bm, p, len);
                break;
            case Section::Difficulty:
                if (len >= 5) parse_difficulty_line(bm, p, len, ar_specified);
                break;
            case Section::Colours: {
                std::string_view k, v;
                if (split_kv(p, len, k, v)) parse_colour_kv(bm, k, v);
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
                                     static_cast<size_t>(file_end - p));
                break;
            case Section::Unknown:
                break;
        }

    next_line:
        p = nl ? nl + 1 : file_end;
    }

    // Old format versions omit ApproachRate; it mirrors OverallDifficulty.
    if (!ar_specified) bm.ar = bm.od;
}

template <typename Map>
inline void parse_into(const FileBuffer& buf, Map& bm,
                       ParseOptions opts = {}) {
    parse_into(buf.data.get(), buf.size, bm, opts);
}

inline Beatmap parse(const char* data, size_t size, ParseOptions opts = {}) {
    Beatmap bm;
    parse_into(data, size, bm, opts);
    return bm;
}

inline Beatmap parse(const FileBuffer& buf, ParseOptions opts = {}) {
    Beatmap bm;
    parse_into(buf.data.get(), buf.size, bm, opts);
    return bm;
}

}  // namespace fosu
