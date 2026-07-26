#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "beatmap.hpp"
#include "hitobject_prefix.hpp"
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

// Editor-emitted section names are unique on their second byte except
// [Editor]/[Events], which the third byte splits. No full compares.
inline Section match_section(std::string_view line) {
    if (line.size() < 3) return Section::Unknown;
    switch (line[1]) {
        case 'G': return Section::General;
        case 'E': return line[2] == 'd' ? Section::Editor : Section::Events;
        case 'M': return Section::Metadata;
        case 'D': return Section::Difficulty;
        case 'T': return Section::TimingPoints;
        case 'C': return Section::Colours;
        case 'H': return Section::HitObjects;
        default: return Section::Unknown;
    }
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

// Key-value lines are dispatched on their first four bytes loaded as one
// u32 — editor-emitted keys are unique on that prefix within a section
// (plus one disambiguating byte where two keys share it). The key length
// then locates the value with no memchr and no trim.
inline constexpr uint32_t key4(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

// Value after "Key:", tolerating the single space General/Editor emit.
inline std::string_view kv_value(const char* p, size_t len, size_t key_len) {
    size_t off = key_len + 1;
    if (off < len && p[off] == ' ') ++off;
    return {p + off, len > off ? len - off : 0};
}

inline void parse_general_line(Beatmap& bm, const char* p, size_t len) {
    switch (load_u32_le(p)) {
        case key4('A', 'u', 'd', 'i'):
            if (p[5] == 'F') bm.audio_filename = kv_value(p, len, 13);
            else if (p[5] == 'L')
                bm.audio_lead_in = parse_i32_field(kv_value(p, len, 11), 0);
            break;
        case key4('P', 'r', 'e', 'v'):
            bm.preview_time = parse_i32_field(kv_value(p, len, 11), -1);
            break;
        case key4('C', 'o', 'u', 'n'):
            if (p[9] == 'O')
                bm.countdown_offset = parse_i32_field(kv_value(p, len, 15), 0);
            else
                bm.countdown = parse_i32_field(kv_value(p, len, 9), 1);
            break;
        case key4('S', 'a', 'm', 'p'):
            if (p[6] == 'S') bm.sample_set = kv_value(p, len, 9);
            else
                bm.samples_match_playback_rate =
                    parse_bool(kv_value(p, len, 24));
            break;
        case key4('S', 't', 'a', 'c'):
            bm.stack_leniency = parse_f64_field(kv_value(p, len, 13), 0.7);
            break;
        case key4('M', 'o', 'd', 'e'):
            bm.mode = parse_i32_field(kv_value(p, len, 4), 0);
            break;
        case key4('L', 'e', 't', 't'):
            bm.letterbox_in_breaks = parse_bool(kv_value(p, len, 17));
            break;
        case key4('W', 'i', 'd', 'e'):
            bm.widescreen_storyboard = parse_bool(kv_value(p, len, 20));
            break;
        case key4('E', 'p', 'i', 'l'):
            bm.epilepsy_warning = parse_bool(kv_value(p, len, 15));
            break;
        case key4('S', 'p', 'e', 'c'):
            bm.special_style = parse_bool(kv_value(p, len, 12));
            break;
        case key4('U', 's', 'e', 'S'):
            bm.use_skin_sprites = parse_bool(kv_value(p, len, 14));
            break;
        case key4('O', 'v', 'e', 'r'):
            bm.overlay_position = kv_value(p, len, 15);
            break;
        case key4('S', 'k', 'i', 'n'):
            bm.skin_preference = kv_value(p, len, 14);
            break;
        default:
            break;
    }
}

inline void parse_editor_line(Beatmap& bm, const char* p, size_t len) {
    switch (load_u32_le(p)) {
        case key4('B', 'o', 'o', 'k'):
            bm.bookmarks = kv_value(p, len, 9);
            break;
        case key4('D', 'i', 's', 't'):
            bm.distance_spacing = parse_f64_field(kv_value(p, len, 15), 0);
            break;
        case key4('B', 'e', 'a', 't'):
            bm.beat_divisor = parse_i32_field(kv_value(p, len, 11), 4);
            break;
        case key4('G', 'r', 'i', 'd'):
            bm.grid_size = parse_i32_field(kv_value(p, len, 8), 4);
            break;
        case key4('T', 'i', 'm', 'e'):
            bm.timeline_zoom = parse_f64_field(kv_value(p, len, 12), 1);
            break;
        default:
            break;
    }
}

inline void parse_metadata_line(Beatmap& bm, const char* p, size_t len) {
    switch (load_u32_le(p)) {
        case key4('T', 'i', 't', 'l'):
            if (p[5] == 'U') bm.title_unicode = kv_value(p, len, 12);
            else bm.title = kv_value(p, len, 5);
            break;
        case key4('A', 'r', 't', 'i'):
            if (p[6] == 'U') bm.artist_unicode = kv_value(p, len, 13);
            else bm.artist = kv_value(p, len, 6);
            break;
        case key4('C', 'r', 'e', 'a'):
            bm.creator = kv_value(p, len, 7);
            break;
        case key4('V', 'e', 'r', 's'):
            bm.version = kv_value(p, len, 7);
            break;
        case key4('S', 'o', 'u', 'r'):
            bm.source = kv_value(p, len, 6);
            break;
        case key4('T', 'a', 'g', 's'):
            bm.tags = kv_value(p, len, 4);
            break;
        case key4('B', 'e', 'a', 't'): {
            const bool set_id = p[7] == 'S';
            const auto v = kv_value(p, len, set_id ? 12 : 9);
            int64_t id;
            if (parse_i64(v.data(), v.data() + v.size(), id) != v.data()) {
                if (set_id) bm.beatmap_set_id = id;
                else bm.beatmap_id = id;
            }
            break;
        }
        default:
            break;
    }
}

inline void parse_difficulty_line(Beatmap& bm, const char* p, size_t len,
                                  bool& ar_specified) {
    switch (load_u32_le(p)) {
        case key4('H', 'P', 'D', 'r'):
            bm.hp = parse_f64_field(kv_value(p, len, 11), 5);
            break;
        case key4('C', 'i', 'r', 'c'):
            bm.cs = parse_f64_field(kv_value(p, len, 10), 5);
            break;
        case key4('O', 'v', 'e', 'r'):
            bm.od = parse_f64_field(kv_value(p, len, 17), 5);
            break;
        case key4('A', 'p', 'p', 'r'):
            bm.ar = parse_f64_field(kv_value(p, len, 12), 5);
            ar_specified = true;
            break;
        case key4('S', 'l', 'i', 'd'):
            if (p[6] == 'M')
                bm.slider_multiplier = parse_f64_field(kv_value(p, len, 16), 1.4);
            else
                bm.slider_tick_rate = parse_f64_field(kv_value(p, len, 14), 1);
            break;
        default:
            break;
    }
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

#if FOSU_SIMD_X86
// One-pass timing point parse for the editor-emitted 8-field shape.
// Two preloaded 32-byte vectors cover the whole line (real max: 39
// bytes); one comma mask and one non-digit mask yield every field
// boundary; every value is computed speculatively and a single `valid`
// predicate — accumulated arithmetically, never branched on per field —
// decides. Structural surprises (old 2/7-field formats, decimal or >8
// digit offsets, junk bytes) return false to defer to the generic parser.
//
// All eight TimingPoint fields are written unconditionally; the caller
// discards the write by not advancing its cursor when this returns false.
// always_inline: gcc leaves this out of line otherwise — a call plus
// per-call constant rebuilds on every timing line (disassembly audit).
// Geometry derived by a successful parse, exported so the section loop's
// shape cache can replay identically-shaped lines without re-deriving it.
struct TpGeom {
    uint8_t c[7];                          // comma positions
    uint8_t bl_il, bl_fl1, bl_fl2, bl_frac;  // beatLength digit layout
    uint8_t bl_neg, bl_has_dot;
};

__attribute__((always_inline))
inline bool fast_parse_timing_point_masked(uint64_t commas, uint64_t nondig,
                                           const char* p, size_t len,
                                           TimingPoint& tp,
                                           TpGeom* geom = nullptr) {
    // Seven comma positions -> eight fields.
    const uint64_t m1 = _blsr_u64(commas);
    const uint64_t m2 = _blsr_u64(m1);
    const uint64_t m3 = _blsr_u64(m2);
    const uint64_t m4 = _blsr_u64(m3);
    const uint64_t m5 = _blsr_u64(m4);
    const uint64_t m6 = _blsr_u64(m5);
    const auto c0 = static_cast<uint32_t>(_tzcnt_u64(commas));
    const auto c1 = static_cast<uint32_t>(_tzcnt_u64(m1));
    const auto c2 = static_cast<uint32_t>(_tzcnt_u64(m2));
    const auto c3 = static_cast<uint32_t>(_tzcnt_u64(m3));
    const auto c4 = static_cast<uint32_t>(_tzcnt_u64(m4));
    const auto c5 = static_cast<uint32_t>(_tzcnt_u64(m5));
    const auto c6 = static_cast<uint32_t>(_tzcnt_u64(m6));

    bool valid = _mm_popcnt_u64(commas) == 7;

    // Offset: an integer with 1..8 digits — editor-emitted files never
    // produce negative or decimal offsets (those defer via the purity
    // check below). Speculative lengths are clamped into 1..8 so shifts
    // stay defined; `valid` already rules the clamped cases out.
    valid &= (c0 - 1) <= 7;
    tp.time = static_cast<double>(swar_parse_u64_safe(p, ((c0 - 1) & 7) + 1));

    // beatLength: [c0+1, c1), optional leading '-', optional fraction.
    const char* f = p + c0 + 1;
    const bool neg = *f == '-';
    f += neg;
    const uint32_t flen = c1 - c0 - 1 - neg;
    // Distance from f to the first non-digit: the '.' if present, else
    // the comma at c1.
    const auto int_len = static_cast<uint32_t>(_tzcnt_u64(nondig >> (f - p)));
    const bool has_dot = int_len < flen;
    // The purity popcount below counts "one extra non-digit" for the dot;
    // verify that byte actually is '.' (fuzz-found: any junk byte in the
    // field would otherwise be accepted as the decimal point).
    valid &= !has_dot || f[int_len] == '.';
    const uint32_t frac_len = flen - int_len - has_dot;
    valid &= (int_len - 1) <= 7;
    valid &= frac_len <= 13;  // real files: 0 (67%) or 12-13
    const uint32_t il = ((int_len - 1) & 7) + 1;
    const uint32_t fl1 = frac_len <= 8 ? frac_len : 8;
    const uint32_t fl2 = frac_len - fl1;
    const char* fp = f + il + 1;  // il, not int_len: bounds speculative
                                  // reads within kBufferPadding on garbage
    uint64_t mant = swar_parse_u64_safe(f, il);
    const uint64_t fm1 = fl1 ? swar_parse_u64_safe(fp, fl1) : 0;
    const uint64_t fm2 = fl2 ? swar_parse_u64_safe(fp + 8, fl2) : 0;
    mant = mant * kPow10u[fl1] + fm1;
    mant = mant * kPow10u[fl2 & 7] + fm2;  // fl2 <= 5 when valid
    double bl =
        static_cast<double>(mant) / kPow10[frac_len <= 13 ? frac_len : 0];
    // mant >= 0, so the sign bit can be OR'd in directly (no fp select).
    bl = std::bit_cast<double>(std::bit_cast<uint64_t>(bl) |
                               (static_cast<uint64_t>(neg) << 63));
    tp.beat_length = bl;

    // Whole-line digit purity in one predicate: the only non-digit bytes
    // allowed are the 7 commas, the optional dot, and the optional minus.
    valid &= _mm_popcnt_u64(nondig) ==
             7 + static_cast<int>(has_dot) + static_cast<int>(neg);

    // Six small-int tail fields, straight-line (no arrays, no loop — gcc
    // spills indexed locals to the stack).
    const uint32_t t0 = c2 - c1 - 1;
    const uint32_t t1 = c3 - c2 - 1;
    const uint32_t t2 = c4 - c3 - 1;
    const uint32_t t3 = c5 - c4 - 1;
    const uint32_t t4 = c6 - c5 - 1;
    const uint32_t t5 = static_cast<uint32_t>(len) - c6 - 1;
    valid &= ((t0 - 1) | (t1 - 1) | (t2 - 1) | (t3 - 1) | (t4 - 1) |
              (t5 - 1)) <= 7;
    tp.meter =
        static_cast<int32_t>(swar_parse_u64_safe(p + c1 + 1, ((t0 - 1) & 7) + 1));
    tp.sample_set =
        static_cast<int32_t>(swar_parse_u64_safe(p + c2 + 1, ((t1 - 1) & 7) + 1));
    tp.sample_index =
        static_cast<int32_t>(swar_parse_u64_safe(p + c3 + 1, ((t2 - 1) & 7) + 1));
    tp.volume =
        static_cast<int32_t>(swar_parse_u64_safe(p + c4 + 1, ((t3 - 1) & 7) + 1));
    tp.uninherited = swar_parse_u64_safe(p + c5 + 1, ((t4 - 1) & 7) + 1) != 0;
    tp.effects =
        static_cast<uint32_t>(swar_parse_u64_safe(p + c6 + 1, ((t5 - 1) & 7) + 1));

    if (geom && valid) {
        geom->c[0] = static_cast<uint8_t>(c0);
        geom->c[1] = static_cast<uint8_t>(c1);
        geom->c[2] = static_cast<uint8_t>(c2);
        geom->c[3] = static_cast<uint8_t>(c3);
        geom->c[4] = static_cast<uint8_t>(c4);
        geom->c[5] = static_cast<uint8_t>(c5);
        geom->c[6] = static_cast<uint8_t>(c6);
        geom->bl_il = static_cast<uint8_t>(il);
        geom->bl_fl1 = static_cast<uint8_t>(fl1);
        geom->bl_fl2 = static_cast<uint8_t>(fl2);
        geom->bl_frac = static_cast<uint8_t>(frac_len);
        geom->bl_neg = neg;
        geom->bl_has_dot = has_dot;
    }
    return valid;
}

// Compatibility entry (tests/fuzzers): computes the masks itself.
__attribute__((always_inline))
inline bool fast_parse_timing_point(__m256i a, __m256i b, const char* p,
                                    size_t len, TimingPoint& tp) {
    if (len > 64 || len < 15) return false;  // real lines: 20..39 bytes
    const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
    const uint64_t commas =
        (comma_mask32(a) | static_cast<uint64_t>(comma_mask32(b)) << 32) &
        line_mask;
    const uint64_t nondig =
        (nondigit_mask32(a) |
         static_cast<uint64_t>(nondigit_mask32(b)) << 32) &
        line_mask;
    return fast_parse_timing_point_masked(commas, nondig, p, len, tp);
}

// --- Timing-line shape cache -------------------------------------------
//
// A [TimingPoints] section reuses a handful of byte-level line layouts:
// on the 10k-map production census, the top 8 exact (comma mask, nondigit
// mask, length) shapes cover a median 98.1% of a file's timing lines. A
// line whose masks equal an already-accepted shape is structurally
// identical to it — same comma positions, same dot/minus placement, all
// other bytes digits — so validation collapses to the key comparison and
// every field converts at cached offsets. The six 1-2 digit tail fields
// convert together with one cached-shuffle maddubs when they fit a
// 16-byte window; wider shapes fall back to cached-offset SWAR.
struct TpShapeRow {
    uint64_t commas = 0, nondig = 0;
    uint32_t len = 0;  // 0 = empty slot (never matches: len >= 15)
    TpGeom g{};
    uint8_t simd_tails = 0;
    alignas(16) int8_t shuf[16];   // gathers tail digits, 2B lanes
    alignas(16) uint8_t subv[16];  // '0' on digit lanes, 0 on padding
};

struct TpShapeCache {
    TpShapeRow rows[16];
    static uint32_t slot(uint64_t commas) {
        return static_cast<uint32_t>((commas * 0x9E3779B97F4A7C15ull) >> 60);
    }
};

// The masks pin every byte's class (clear nondigit bit == digit, comma
// bit == literal comma), but not WHICH non-digit character occupies the
// beatLength's sign/dot slots — a fuzz-found hole. Two byte compares
// close it; everything else follows from mask equality.
inline bool tp_shape_match(const TpShapeRow& row, uint64_t commas,
                           uint64_t nondig, size_t len, const char* p) {
    if (row.commas != commas || row.nondig != nondig ||
        row.len != static_cast<uint32_t>(len))
        return false;
    const TpGeom& g = row.g;
    const bool neg_ok = !g.bl_neg || p[g.c[0] + 1] == '-';
    const bool dot_ok =
        !g.bl_has_dot || p[g.c[0] + 1 + g.bl_neg + g.bl_il] == '.';
    return neg_ok && dot_ok;
}

inline void tp_shape_insert(TpShapeCache& cache, uint64_t commas,
                            uint64_t nondig, size_t len, const TpGeom& g) {
    TpShapeRow& r = cache.rows[TpShapeCache::slot(commas)];
    r.commas = commas;
    r.nondig = nondig;
    r.len = static_cast<uint32_t>(len);
    r.g = g;
    // Tail SIMD layout: all six fields 1-2 digits and spanning <= 16
    // bytes from the first tail digit.
    const uint32_t base = g.c[1] + 1;
    const uint32_t span = static_cast<uint32_t>(len) - base;
    uint32_t maxlen = 0;
    uint8_t lens[6];
    for (int i = 0; i < 6; ++i) {
        const uint32_t hi = i < 5 ? g.c[i + 2] : static_cast<uint32_t>(len);
        lens[i] = static_cast<uint8_t>(hi - g.c[i + 1] - 1);
        maxlen = lens[i] > maxlen ? lens[i] : maxlen;
    }
    r.simd_tails = span <= 16 && maxlen <= 2;
    if (r.simd_tails) {
        for (int i = 0; i < 16; ++i) {
            r.shuf[i] = static_cast<int8_t>(0x80);
            r.subv[i] = 0;
        }
        for (int i = 0; i < 6; ++i) {
            const uint32_t off = g.c[i + 1] + 1 - base;
            // lane i bytes {2i, 2i+1} = {tens, ones}, weights {10, 1}
            for (int d = 0; d < lens[i]; ++d) {
                r.shuf[2 * i + (2 - lens[i]) + d] =
                    static_cast<int8_t>(off + d);
                r.subv[2 * i + (2 - lens[i]) + d] = '0';
            }
        }
    }
}

// Replays a cached shape. Arithmetic mirrors the one-pass parser exactly,
// so results are bit-identical (pinned by fuzz).
inline void tp_shape_convert(const TpShapeRow& r, const char* p,
                             TimingPoint& tp) {
    const TpGeom& g = r.g;
    tp.time = static_cast<double>(swar_parse_u64(p, g.c[0]));

    const char* f = p + g.c[0] + 1 + g.bl_neg;
    uint64_t mant = swar_parse_u64_safe(f, g.bl_il);
    const char* fp = f + g.bl_il + 1;
    const uint64_t fm1 = g.bl_fl1 ? swar_parse_u64_safe(fp, g.bl_fl1) : 0;
    const uint64_t fm2 =
        g.bl_fl2 ? swar_parse_u64_safe(fp + 8, g.bl_fl2) : 0;
    mant = mant * kPow10u[g.bl_fl1] + fm1;
    mant = mant * kPow10u[g.bl_fl2] + fm2;
    double bl = static_cast<double>(mant) / kPow10[g.bl_frac];
    bl = std::bit_cast<double>(std::bit_cast<uint64_t>(bl) |
                               (static_cast<uint64_t>(g.bl_neg) << 63));
    tp.beat_length = bl;

    if (r.simd_tails) {
        const __m128i v = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p + g.c[1] + 1));
        const __m128i gathered = _mm_shuffle_epi8(
            v, _mm_load_si128(reinterpret_cast<const __m128i*>(r.shuf)));
        const __m128i digits = _mm_sub_epi8(
            gathered,
            _mm_load_si128(reinterpret_cast<const __m128i*>(r.subv)));
        const __m128i vals =
            _mm_maddubs_epi16(digits, _mm_set1_epi16(0x010A));
        alignas(16) uint16_t t[8];
        _mm_store_si128(reinterpret_cast<__m128i*>(t), vals);
        tp.meter = t[0];
        tp.sample_set = t[1];
        tp.sample_index = t[2];
        tp.volume = t[3];
        tp.uninherited = t[4] != 0;
        tp.effects = t[5];
    } else {
        const uint32_t len = r.len;
        const uint32_t t0 = g.c[2] - g.c[1] - 1;
        const uint32_t t1 = g.c[3] - g.c[2] - 1;
        const uint32_t t2 = g.c[4] - g.c[3] - 1;
        const uint32_t t3 = g.c[5] - g.c[4] - 1;
        const uint32_t t4 = g.c[6] - g.c[5] - 1;
        const uint32_t t5 = len - g.c[6] - 1;
        tp.meter = static_cast<int32_t>(swar_parse_u64(p + g.c[1] + 1, t0));
        tp.sample_set =
            static_cast<int32_t>(swar_parse_u64(p + g.c[2] + 1, t1));
        tp.sample_index =
            static_cast<int32_t>(swar_parse_u64(p + g.c[3] + 1, t2));
        tp.volume = static_cast<int32_t>(swar_parse_u64(p + g.c[4] + 1, t3));
        tp.uninherited = swar_parse_u64(p + g.c[5] + 1, t4) != 0;
        tp.effects =
            static_cast<uint32_t>(swar_parse_u64(p + g.c[6] + 1, t5));
    }
}

// Fused [TimingPoints] section loop: the same two loads serve the newline
// scan and the parser, and the section is sized exactly once — the next
// '[' bounds it, so reserve never over-allocates for short sections nor
// grows for marathon ones (growth reallocs plus resize's value-init
// memsets measured worse than the push_back they replaced). Returns the
// position after the section.
inline const char* parse_timing_points_section(Beatmap& bm, const char* p,
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

        TimingPoint tp;
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

    // Points are written straight into the pool through a raw cursor —
    // one bounds ensure per slider instead of a checked push per point.
    // A point pair costs at least 4 bytes ("|x:y"), which bounds the count.
    auto& pts = bm.slider_points;
    const size_t base = pts.size();
    pts.resize(base + static_cast<size_t>(end - p) / 4 + 1);
    SliderPoint* w = pts.data() + base;
    while (p < end && *p == '|') {
        int32_t px, py;
        const char* q = parse_coord(p + 1, end, px);
        // A coord ending at the line end reads the terminator from the
        // padded buffer, never ':' — no explicit q < end check needed.
        if (q == p + 1 || *q != ':') {
            pts.resize(base);
            return false;
        }
        const char* r = parse_coord(q + 1, end, py);
        if (r == q + 1) {
            pts.resize(base);
            return false;
        }
        *w++ = {px, py};
        p = r;
    }
    pts.resize(static_cast<size_t>(w - pts.data()));
    s.point_begin = static_cast<uint32_t>(base);
    s.point_count = static_cast<uint32_t>(pts.size() - base);

    if (p >= end || *p != ',') return false;
    ++p;
    const uint32_t srun = digit_run8(p);  // slides: a bare small integer
    if (srun - 1 > 6) return false;
    s.slides = static_cast<int32_t>(swar_parse_u64(p, srun));
    p += srun;
    if (p >= end || *p != ',') return false;
    const char* q = parse_double(p + 1, end, s.length);
    if (q == p + 1) return false;
    p = q;

    // Optional: edgeSounds, edgeSets, hitSample (assigned positionally).
    std::string_view extra[3];
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
                extra[0] = {p, span};
            } else if (c1 >= span) {
                extra[0] = {p, c0};
                extra[1] = {p + c0 + 1, span - c0 - 1};
            } else {
                extra[0] = {p, c0};
                extra[1] = {p + c0 + 1, c1 - c0 - 1};
                extra[2] = {p + c1 + 1, span - c1 - 1};
            }
        } else
#endif
        {
            int n = 0;
            while (n < 3 && p < end) {
                const auto* c =
                    static_cast<const char*>(memchr(p, ',', end - p));
                const char* fend = c ? c : end;
                extra[n++] = {p, static_cast<size_t>(fend - p)};
                p = fend + 1;
            }
        }
    }
    s.edge_sounds = extra[0];
    s.edge_sets = extra[1];
    h.hit_sample = extra[2];
    h.slider = static_cast<uint32_t>(bm.sliders.size());
    bm.sliders.push_back(s);
    return true;
}

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, trailing hitSample.
inline bool finish_hitobject(Beatmap& bm, HitObject& h, const char* p,
                             const char* end, size_t bytes_remaining) {
    if (h.type & 2) {  // slider
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
}

// Scalar-only per-line path; the SIMD build routes [HitObjects] through
// parse_hitobjects_section instead.
inline void parse_hitobject_line(Beatmap& bm, const char* line, size_t len,
                                 size_t bytes_remaining) {
    bm.hit_objects.emplace_back(HitObject::uninit_t{});
    HitObject& h = bm.hit_objects.back();
    h.end_time = 0;
    h.slider = HitObject::kNoSlider;
    h.hit_sample = {};

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
// Fused [HitObjects] section loop: one 32-byte load per line yields the
// prefix delimiter mask AND the newline position (5-field circle lines —
// the majority in real maps — never touch memchr). Returns the position
// after the section.
inline const char* parse_hitobjects_section(Beatmap& bm, const char* p,
                                            const char* file_end) {
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        if (c == '[') return p;

        bm.hit_objects.emplace_back(HitObject::uninit_t{});
        HitObject& h = bm.hit_objects.back();
        h.hit_sample = {};  // the fast prefix writes every other field

        uint32_t nl_mask;
        const int next = fast_parse_prefix(p, h, nl_mask);

        const char* nl = nl_mask
                             ? p + _tzcnt_u32(nl_mask)
                             : static_cast<const char*>(memchr(
                                   p + 32, '\n',
                                   file_end - p > 32
                                       ? static_cast<size_t>(file_end - p) - 32
                                       : 0));
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const char* next_line = nl ? nl + 1 : file_end;

        bool ok;
        if (next >= 0) {
            ++bm.stats.fast_path_lines;
            ok = finish_hitobject(bm, h, p + next, line_end,
                                  static_cast<size_t>(file_end - p));
        } else {
            h.end_time = 0;
            h.slider = HitObject::kNoSlider;
            const int sn = scalar_parse_prefix(
                p, static_cast<size_t>(line_end - p), h);
            if (sn >= 0) {
                ++bm.stats.slow_path_lines;
                ok = finish_hitobject(bm, h, p + sn, line_end,
                                      static_cast<size_t>(file_end - p));
            } else {
                ok = false;
            }
        }
        if (!ok) {
            bm.hit_objects.pop_back();
            ++bm.stats.malformed_lines;
        }
        p = next_line;
    }
    return p;
}
#endif  // FOSU_SIMD_X86

// Resets bm for reuse: every field returns to its default, but vector
// capacity is kept, so the steady state of a parse-many loop allocates
// nothing and touches no new pages.
inline void reset_for_reuse(Beatmap& bm) {
    auto breaks = std::move(bm.breaks);
    auto colours = std::move(bm.combo_colours);
    auto tps = std::move(bm.timing_points);
    auto objs = std::move(bm.hit_objects);
    auto sliders = std::move(bm.sliders);
    auto points = std::move(bm.slider_points);
    bm = Beatmap{};
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
inline void parse_into(const char* data, size_t size, Beatmap& bm,
                       [[maybe_unused]] ParseOptions opts = {}) {
    using namespace detail;
    reset_for_reuse(bm);
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

inline void parse_into(const FileBuffer& buf, Beatmap& bm,
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
