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
#include "detail/hitobjects.hpp"
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

// Publishes records written directly into a vector's reserved capacity.
// libstdc++ and libc++ lay a vector out as {begin, end, capacity_end};
// other standard libraries use the portable path below. The layout is
// checked by tests/test_parser.cpp against the public accessors.
#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)
inline constexpr bool kDirectVectorWrites = true;
#else
inline constexpr bool kDirectVectorWrites = false;
#endif
template <typename T>
inline bool vector_layout_ok(const std::vector<T>& v) {
    const T* raw[3];
    static_assert(sizeof(v) == sizeof(raw));
    const void* const object = &v;
    memcpy(raw, object, sizeof raw);
    return raw[0] == v.data() && raw[1] == v.data() + v.size() &&
           raw[2] == v.data() + v.capacity();
}
template <typename T>
inline void set_vector_size(std::vector<T>& v, size_t n) {
    T* raw[3];
    static_assert(sizeof(v) == sizeof(raw));
    void* const object = &v;
    memcpy(raw, object, sizeof raw);
    raw[1] = raw[0] + n;
    memcpy(object, raw, sizeof raw);
}
// Containers with a native set_size (the C ABI arena vectors) always take
// the direct path; std::vector takes it on the known layouts.
template <typename V>
inline constexpr bool has_set_size_v = requires(V& v) { v.set_size(size_t{}); };
template <typename V>
inline constexpr bool direct_vector_writes_v = has_set_size_v<V> || kDirectVectorWrites;
template <typename V>
inline void publish_size(V& v, size_t n) {
    if constexpr (has_set_size_v<V>) v.set_size(n);
    else set_vector_size(v, n);
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
// grows for marathon ones. Points are written straight into the reserved
// capacity and published at the end; blank, comment and header lines are
// only examined when the editor shape fails. Returns the position after
// the section.
template <typename Map>
inline const char* parse_timing_points_section(Map& bm, const char* p,
                                               const char* file_end) {
    using TP = typename Map::TimingPoint;
    auto& tps = bm.timing_points;
    constexpr bool kDirect = direct_vector_writes_v<decltype(Map::timing_points)>;
    const auto* bracket = static_cast<const char*>(
        memchr(p, '[', static_cast<size_t>(file_end - p)));
    const char* section_end = bracket ? bracket : file_end;
    tps.reserve(tps.size() + static_cast<size_t>(section_end - p) / 17 + 4);
    TpShapeCache cache{};
    const __m256i k_nl = bcast256(kByteNewline);
    const __m256i k_comma = bcast256(kByteComma);
    const __m256i k_bias = bcast256(kByteBias);
    const __m256i k_thr = bcast256(kByteThreshold);
    // Direct mode writes each point into the reserved capacity and publishes
    // the count at the end; the portable mode falls back to push_back.
    TP* w = tps.data() + tps.size();
    TP* wend = tps.data() + tps.capacity();
    TP local;
    uint32_t malformed = 0;
    while (p < file_end) {
        const __m256i a =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const __m256i b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
        const uint64_t nl =
            static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, k_nl))) |
            static_cast<uint64_t>(static_cast<uint32_t>(
                _mm256_movemask_epi8(_mm256_cmpeq_epi8(b, k_nl)))) << 32;
        const char* nlp = nl ? p + _tzcnt_u64(nl) : find_newline32(p + 64, file_end, k_nl);
        const char* next_line = nlp + (nlp < file_end);
        const char* line_end = nlp - (nlp > p && nlp[-1] == '\r');
        const auto len = static_cast<size_t>(line_end - p);
        TP* tp;
        if constexpr (kDirect) {
            if (w == wend) [[unlikely]] {
                publish_size(tps, static_cast<size_t>(w - tps.data()));
                tps.reserve(tps.capacity() * 2 + 16);
                w = tps.data() + tps.size();
                wend = tps.data() + tps.capacity();
            }
            tp = w;
            if constexpr (requires(TP t) { t.reserved; }) memset(tp->reserved, 0, sizeof tp->reserved);
        } else {
            tp = &local;
        }
        if (len - 15 <= 64 - 15) [[likely]] {
            const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
            const uint64_t commas =
                (static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, k_comma))) |
                 static_cast<uint64_t>(static_cast<uint32_t>(
                     _mm256_movemask_epi8(_mm256_cmpeq_epi8(b, k_comma)))) << 32) &
                line_mask;
            const uint64_t nondig =
                (nondigit_mask32(a, k_bias, k_thr) |
                 static_cast<uint64_t>(nondigit_mask32(b, k_bias, k_thr)) << 32) &
                line_mask;
            const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
            bool accepted;
            if (tp_shape_match(row, commas, nondig, len, p)) {
                tp_shape_convert(row, p, *tp);
                accepted = true;
            } else {
                TpGeom geom;
                accepted = fast_parse_timing_point_masked(commas, nondig, p, len, *tp, &geom);
                if (accepted) tp_shape_insert(cache, commas, nondig, len, geom);
            }
            if (accepted) {
                if constexpr (kDirect) ++w;
                else tps.push_back(local);
                p = next_line;
                continue;
            }
        }
        // Unusual line: blank, comment, header, old field layouts or bytes
        // outside the editor shape.
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        if (!ignored_line(p, line_end)) {
            if (parse_timing_fields(p, line_end, *tp)) {
                if constexpr (kDirect) ++w;
                else tps.push_back(local);
            } else {
                ++malformed;
            }
        }
        p = next_line;
    }
    if constexpr (kDirect) publish_size(tps, static_cast<size_t>(w - tps.data()));
    bm.stats.malformed_lines += malformed;
    return p;
}
#endif  // FOSU_SIMD_X86

// Storage policy for the ordinary library: records are written into the
// Beatmap's vectors through raw cursors (direct mode) and published once per
// section, so the per-record cost is a bounds check and a pointer bump.
// Growth publishes, reserves and reopens the affected cursor. Slider pools
// are sized on the first slider, so slider-free maps allocate none.
template <typename Map>
struct VectorSink {
    using HitObject = typename Map::HitObject;
    using Slider = typename Map::Slider;
    using Point = typename Map::SliderPoint;
    static constexpr bool kDirect = direct_vector_writes_v<decltype(Map::hit_objects)>;
    Map& bm;
    HitObject* hw = nullptr;
    HitObject* hend = nullptr;
    Slider* sw = nullptr;
    Slider* send = nullptr;
    Point* pw = nullptr;
    Point* pend = nullptr;
    size_t remaining;  // section bytes: sizes the slider pools on first use

    VectorSink(Map& m, size_t section_bytes) : bm(m), remaining(section_bytes) {
        open_objects(); open_sliders(); open_points();
    }
    void open_objects() {
        hw = bm.hit_objects.data() + bm.hit_objects.size();
        hend = bm.hit_objects.data() + bm.hit_objects.capacity();
    }
    void open_sliders() {
        sw = bm.sliders.data() + bm.sliders.size();
        send = bm.sliders.data() + bm.sliders.capacity();
    }
    void open_points() {
        pw = bm.slider_points.data() + bm.slider_points.size();
        pend = bm.slider_points.data() + bm.slider_points.capacity();
    }
    // Makes every record written so far visible through the vectors.
    void publish() {
        if constexpr (kDirect) {
            if (hw) publish_size(bm.hit_objects, static_cast<size_t>(hw - bm.hit_objects.data()));
            if (sw) publish_size(bm.sliders, static_cast<size_t>(sw - bm.sliders.data()));
            if (pw) publish_size(bm.slider_points, static_cast<size_t>(pw - bm.slider_points.data()));
        } else {
            bm.slider_points.resize(static_cast<size_t>(pw - bm.slider_points.data()));
        }
    }
    __attribute__((noinline)) void grow_objects() {
        publish();
        bm.hit_objects.reserve(bm.hit_objects.capacity() * 2 + 64);
        open_objects();
    }
    __attribute__((noinline)) void grow_sliders() {
        publish();
        const size_t cap = bm.sliders.capacity();
        bm.sliders.reserve(cap ? cap * 2 + 16 : remaining / 48 + 16);
        open_sliders();
    }
    __attribute__((noinline)) void grow_points(size_t bound) {
        publish();
        const size_t cap = bm.slider_points.capacity();
        size_t want = cap ? cap * 2 : remaining / 14;
        if (want < bm.slider_points.size() + bound) want = bm.slider_points.size() + bound + 64;
        bm.slider_points.reserve(want);
        open_points();
    }

    HitObject& begin(size_t) {
        if constexpr (kDirect) {
            if (hw == hend) [[unlikely]] grow_objects();
        } else {
            bm.hit_objects.emplace_back(typename HitObject::uninit_t{});
            hw = &bm.hit_objects.back();
        }
        if constexpr (requires(HitObject o) { o.reserved; }) hw->reserved = 0;
        return *hw;
    }
    void commit(HitObject&) {
        if constexpr (kDirect) ++hw;
    }
    void rollback(HitObject&) {
        if constexpr (!kDirect) bm.hit_objects.pop_back();
    }
    void sample(HitObject& h, const char* s, size_t n) { h.hit_sample = bm.view({s, n}); }
    auto view(const char* s, size_t n) { return bm.view({s, n}); }
    Point* point_slot(size_t bound) {
        if constexpr (kDirect) {
            if (static_cast<size_t>(pend - pw) < bound) [[unlikely]] grow_points(bound);
        } else {
            const size_t base = static_cast<size_t>(pw - bm.slider_points.data());
            if (bm.slider_points.capacity() == 0) bm.slider_points.reserve(remaining / 14);
            bm.slider_points.resize(base + bound);
            pw = bm.slider_points.data() + base;
            pend = pw + bound;
        }
        return pw;
    }
    uint32_t point_index(Point* w) const {
        return static_cast<uint32_t>(w - bm.slider_points.data());
    }
    void slider_rollback(Point*, Point* w_end, bool keep_points) {
        if (keep_points) pw = w_end;
        if constexpr (!kDirect)
            bm.slider_points.resize(static_cast<size_t>(pw - bm.slider_points.data()));
    }
    using SliderRecord = Slider;
    Slider& slider_slot() {
        if constexpr (kDirect) {
            if (sw == send) [[unlikely]] grow_sliders();
        } else {
            bm.sliders.emplace_back(typename Slider::uninit_t{});
            sw = &bm.sliders.back();
        }
        if constexpr (requires(Slider o) { o.reserved; }) memset(sw->reserved, 0, sizeof sw->reserved);
        return *sw;
    }
    void slider_commit(HitObject& h, Slider&, Point* w_end, const char* hs, size_t hs_len) {
        if constexpr (!kDirect)
            bm.slider_points.resize(static_cast<size_t>(w_end - bm.slider_points.data()));
        h.hit_sample = bm.view({hs, hs_len});
        h.slider = static_cast<uint32_t>(sw - bm.sliders.data());
        if constexpr (kDirect) ++sw;
        pw = w_end;
    }
    ParseStats& stats() { return bm.stats; }
};

// Parses one [HitObjects] section starting at `p` (after its header line).
template <typename Map>
inline const char* parse_hitobjects_section(Map& bm, const char* p, const char* file_end,
                                            bool use_simd) {
    VectorSink<Map> sink(bm, static_cast<size_t>(file_end - p));
    const HitConsts k;
#if FOSU_SIMD_X86
    if (use_simd) p = parse_hitobject_lines(sink, p, file_end, k);
    else
#else
    (void)use_simd;
#endif
        p = parse_hitobject_lines_scalar(sink, p, file_end, k);
    sink.publish();
    return p;
}

#if FOSU_SIMD_X86
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
                    static_cast<size_t>(file_end - line_end) / 16 + 1);
                p = parse_hitobjects_section(bm, nl ? nl + 1 : file_end, file_end,
                                             opts.use_simd);
                sec = Section::Unknown;
                continue;
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
            case Section::HitObjects:  // consumed by parse_hitobjects_section
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
