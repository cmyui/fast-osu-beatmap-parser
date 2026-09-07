// speedrun: the fosu parser as a one-shot process — exec, read one .osu
// file, parse, write the canonical dump (speedrun/dump.hpp) to stdout,
// exit — with nothing between the kernel and the parser. Freestanding:
// no libc, no libstdc++, one anonymous arena (input, padding, timing
// buffer and output stream back to back, so multi-size THP can back the
// whole working set with one or two folios), hit objects written straight
// from the SIMD prefix store into the output stream, and everything else
// gathered into a trailer. Parsing logic is transplanted from
// include/fosu/*.hpp (same fast paths, same fallbacks); the reference
// output is produced by speedrun/run1.cpp on the library.
#include <immintrin.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "rt.hpp"

#define FASTFLOAT_ASSERT(x) ((void)0)
#define FASTFLOAT_DEBUG_ASSERT(x) ((void)0)
#include "third_party/fast_float.h"

namespace {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;
using u64 = uint64_t;
using i64 = int64_t;

struct sv {
    const char* p = nullptr;
    size_t n = 0;
};
inline bool sv_eq(sv a, const char* lit) {
    size_t i = 0;
    for (; i < a.n; ++i)
        if (lit[i] == 0 || lit[i] != a.p[i]) return false;
    return lit[i] == 0;
}

// ---------------------------------------------------------------- context
// All mutable state lives in one block on the initial stack page (already
// resident: the kernel wrote argv there) reached through a global register,
// so .data is never written (no copy-on-write fault) and .bss never
// touched. State is defined further down; Ctx is completed after it.
struct Ctx;
register Ctx* g __asm__("r15");
#define g_out_begin (g->out_begin)
#define g_out (g->out)
#define g_out_end (g->out_end)

void flush();
inline void ensure(size_t n);
inline void put_raw(const void* p, size_t n);
inline void put_u8(u8 v);
inline void put_u32(u32 v);
inline void put_i32(i32 v);
inline void put_i64(i64 v);
inline void put_f64(double v);
inline void put_str(sv s);

// ---------------------------------------------------------------- SWAR / scalar numerics
inline u32 load_u32_le(const char* p) { u32 v; memcpy(&v, p, 4); return v; }
inline u64 load_u64_le(const char* p) { u64 v; memcpy(&v, p, 8); return v; }

inline u32 digit_run8(const char* p) {
    const u64 chunk = load_u64_le(p);
    constexpr u64 kHi = 0xF0F0F0F0F0F0F0F0ull, kThrees = 0x3030303030303030ull;
    const u64 nondigit = (((chunk & kHi) ^ kThrees) | (((chunk + 0x0606060606060606ull) & kHi) ^ kThrees));
    if (nondigit == 0) return 8;
    return static_cast<u32>(__builtin_ctzll(nondigit)) >> 3;
}
inline u32 swar_parse_u32(const char* p, u32 len) {
    u32 c = load_u32_le(p) & 0x0F0F0F0F;
    c <<= 8 * (4 - len);
    c = (c * 2561u) >> 8;
    return ((c & 0x00FF00FF) * 6553601u) >> 16;
}
inline u64 swar_parse_u64_safe(const char* p, u32 len) {
    u64 c = load_u64_le(p) & 0x0F0F0F0F0F0F0F0Full;
    c <<= (8 * (8 - len)) & 63;
    c = (c * 2561ull) >> 8;
    c = ((c & 0x00FF00FF00FF00FFull) * 6553601ull) >> 16;
    return ((c & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;
}
inline u64 swar_parse_u64(const char* p, u32 len) {
    u64 c = load_u64_le(p) & 0x0F0F0F0F0F0F0F0Full;
    c <<= 8 * (8 - len);
    c = (c * 2561ull) >> 8;
    c = ((c & 0x00FF00FF00FF00FFull) * 6553601ull) >> 16;
    return ((c & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;
}
inline bool is_digit(char c) { return static_cast<u8>(c - '0') <= 9; }

inline const char* parse_u64(const char* p, const char* end, u64& out) {
    const char* start = p;
    u64 v = 0;
    int digits = 0;
    while (p < end && is_digit(*p)) {
        if (digits < 19) { v = v * 10 + static_cast<u64>(*p - '0'); ++digits; }
        ++p;
    }
    if (p == start) return start;
    out = v;
    return p;
}
inline const char* parse_i64(const char* p, const char* end, i64& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && *p == '-') { neg = true; ++p; }
    u64 mag;
    const char* q = parse_u64(p, end, mag);
    if (q == p) return start;
    out = neg ? -static_cast<i64>(mag) : static_cast<i64>(mag);
    return q;
}
inline i32 clamp_i32(i64 v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return static_cast<i32>(v);
}
constexpr double kPow10[20] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
                               1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19};
constexpr u64 kPow10u[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

// strtod replacement for the rare general case (>18 digits or exponent):
// fast_float is correctly rounded like glibc's strtod, so results agree.
inline const char* strtod_like(const char* start, const char* end, double& out) {
    const auto r = fast_float::from_chars(start, end, out);
    if (r.ec != std::errc()) { out = 0; return start; }
    return r.ptr;
}

inline const char* parse_double(const char* p, const char* end, double& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && *p == '-') { neg = true; ++p; }
    u64 mant = 0;
    int digits = 0, frac = 0;
    bool any = false;
    for (;;) {
        u32 run = digit_run8(p);
        if (run > static_cast<u64>(end - p)) run = static_cast<u32>(end - p);
        if (!run) break;
        any = true;
        if (digits + static_cast<int>(run) > 18) return strtod_like(start, end, out);
        mant = mant * kPow10u[run] + swar_parse_u64(p, run);
        digits += static_cast<int>(run);
        p += run;
        if (run < 8) break;
    }
    if (p < end && *p == '.') {
        ++p;
        for (;;) {
            u32 run = digit_run8(p);
            if (run > static_cast<u64>(end - p)) run = static_cast<u32>(end - p);
            if (!run) break;
            any = true;
            if (digits + static_cast<int>(run) > 18) return strtod_like(start, end, out);
            mant = mant * kPow10u[run] + swar_parse_u64(p, run);
            digits += static_cast<int>(run);
            frac += static_cast<int>(run);
            p += run;
            if (run < 8) break;
        }
    }
    if (!any) return start;
    if (p < end && (*p == 'e' || *p == 'E')) return strtod_like(start, end, out);
    double v = static_cast<double>(mant);
    if (frac) v /= kPow10[frac];
    out = neg ? -v : v;
    return p;
}

// ---------------------------------------------------------------- hitobject prefix (AVX2)
constexpr u32 kNPrefixVariants = 3 * 3 * 10 * 3;
struct alignas(64) LaneMasks {
    i32 perm[8];
    int8_t shuf[32];
};
consteval std::array<LaneMasks, kNPrefixVariants> make_lane_masks() {
    std::array<LaneMasks, kNPrefixVariants> out{};
    for (int lx = 1; lx <= 3; ++lx)
    for (int ly = 1; ly <= 3; ++ly)
    for (int lt = 1; lt <= 10; ++lt)
    for (int lty = 1; lty <= 3; ++lty) {
        const int p0 = lx, p1 = p0 + 1 + ly, p2 = p1 + 1 + lt, p3 = p2 + 1 + lty;
        const int index = (((lx - 1) * 3 + (ly - 1)) * 10 + (lt - 1)) * 3 + (lty - 1);
        int src[32];
        for (auto& s : src) s = -1;
        for (int i = 0; i < lx; ++i) src[4 - lx + i] = i;
        for (int i = 0; i < ly; ++i) src[8 - ly + i] = p0 + 1 + i;
        for (int i = 0; i < lty; ++i) src[12 - lty + i] = p2 + 1 + i;
        for (int i = 0; i < lt; ++i) src[32 - lt + i] = p1 + 1 + i;
        LaneMasks& lm = out[index];
        lm.perm[0] = 0;
        lm.perm[1] = 1;
        lm.perm[2] = (p2 + 1) / 4;
        lm.perm[3] = (p3 - 1) / 4;
        const int td0 = (p1 + 1) / 4, td1 = (p2 - 1) / 4;
        for (int k = 0; k < 4; ++k) lm.perm[4 + k] = (td0 + k <= td1) ? td0 + k : td1;
        for (int b = 0; b < 32; ++b) {
            if (src[b] < 0) { lm.shuf[b] = static_cast<int8_t>(0x80); continue; }
            const int dw = src[b] / 4, off = src[b] % 4, lane_base = (b < 16) ? 0 : 4;
            int slot = -1;
            for (int j = lane_base; j < lane_base + 4; ++j)
                if (lm.perm[j] == dw) { slot = j; break; }
            lm.shuf[b] = static_cast<int8_t>((slot - lane_base) * 4 + off);
        }
    }
    return out;
}
constexpr auto kLaneMasks = make_lane_masks();

inline u32 comma_mask32(__m256i a) {
    return static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, _mm256_set1_epi8(','))));
}
inline u32 nondigit_mask32(__m256i a) {
    const __m256i biased = _mm256_add_epi8(a, _mm256_set1_epi8(80));
    return static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpgt_epi8(biased, _mm256_set1_epi8(-119))));
}

// Output record head: identical to the first 32 bytes of fosu::HitObject
// so the prefix vector store lands directly in the stream. Bytes 28..31
// (hit_sample view in the library) hold the hit_sample length here.
struct HO {
    i32 x, y;
    u32 type, hitsound;
    i32 time, end_time;
    u32 slider;
    u32 hs_len;
};
constexpr u32 kNoSlider = 0xFFFFFFFF;

inline int fast_parse_prefix(__m256i ascii, const char* line, HO& h) {
    const __m256i digits = _mm256_sub_epi8(ascii, _mm256_set1_epi8('0'));
    const u32 mask = nondigit_mask32(ascii);
    const u32 m1 = _blsr_u32(mask), m2 = _blsr_u32(m1), m3 = _blsr_u32(m2);
    const u32 p0 = _tzcnt_u32(mask), p1 = _tzcnt_u32(m1), p2 = _tzcnt_u32(m2), p3 = _tzcnt_u32(m3);
    const u32 index = 60 * p0 + 27 * p1 + 2 * p2 + p3 - 158;
    if ((p0 - 1) > 2 || (p1 - p0 - 2) > 2 || (p2 - p1 - 2) > 9 || (p3 - p2 - 2) > 2) return -1;
    if (!(line[p0] == ',' && line[p1] == ',' && line[p2] == ',' && line[p3] == ',')) return -1;
    const LaneMasks& lm = kLaneMasks[index];
    const __m256i perm = _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.perm));
    const __m256i shuf = _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.shuf));
    const __m256i placed = _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(digits, perm), shuf);
    const __m256i pair_weights = _mm256_setr_epi8(0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10, 1, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 0, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
    const __m256i word_weights = _mm256_setr_epi16(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1);
    const __m256i words = _mm256_maddubs_epi16(placed, pair_weights);
    const __m256i dwords = _mm256_madd_epi16(words, word_weights);
    if (p2 - p1 - 1 <= 8) {
        const __m256i packed = _mm256_packus_epi32(dwords, dwords);
        const __m256i time_weights = _mm256_setr_epi16(1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 10000, 1, 0, 0, 0, 0);
        const __m256i combined = _mm256_madd_epi16(packed, time_weights);
        const __m256i arrange = _mm256_setr_epi32(0, 2, 1, 3, 5, 3, 3, 3);
        const __m256i arranged = _mm256_permutevar8x32_epi32(combined, arrange);
        const __m256i no_slider = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, -1, 0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&h), _mm256_blend_epi32(arranged, no_slider, 0x40));
    } else {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&h), _mm256_castsi256_si128(dwords));
        const __m128i thi = _mm256_extracti128_si256(dwords, 1);
        const u64 t = static_cast<u32>(_mm_extract_epi32(thi, 1)) * 100000000ull +
                      static_cast<u32>(_mm_extract_epi32(thi, 2)) * 10000ull +
                      static_cast<u32>(_mm_extract_epi32(thi, 3));
        if (t > INT32_MAX) return -1;
        h.time = static_cast<i32>(t);
        h.end_time = 0;
        h.slider = kNoSlider;
        h.hs_len = 0;
    }
    const auto d1 = static_cast<u8>(line[p3 + 1] - '0');
    if (d1 > 9) return -1;
    u32 hs = d1;
    u32 next = p3 + 2;
    const auto d2 = static_cast<u8>(line[next] - '0');
    if (d2 <= 9) { hs = hs * 10 + d2; ++next; }
    const char after = line[next];
    if (!(after == ',' || after == '\r' || after == '\n' || after == '\0')) return -1;
    h.hitsound = hs;
    return static_cast<int>(next);
}

inline int scalar_parse_prefix(const char* line, size_t len, HO& h) {
    const char* p = line;
    const char* end = line + len;
    i64 v[5];
    for (int i = 0; i < 5; ++i) {
        const char* q = parse_i64(p, end, v[i]);
        if (q == p) return -1;
        p = q;
        if (p < end && *p == '.') { ++p; while (p < end && is_digit(*p)) ++p; }
        if (i < 4) { if (p >= end || *p != ',') return -1; ++p; }
    }
    if (p < end && *p != ',') return -1;
    h.x = clamp_i32(v[0]);
    h.y = clamp_i32(v[1]);
    h.time = clamp_i32(v[2]);
    h.type = static_cast<u32>(v[3]);
    h.hitsound = static_cast<u32>(v[4]);
    return static_cast<int>(p - line);
}

// ---------------------------------------------------------------- parse state (the trailer)
struct __attribute__((packed)) TPRec {
    double time, beat_length;
    i32 meter, sample_set, sample_index, volume;
    u8 uninherited;
    u32 effects;
};
static_assert(sizeof(TPRec) == 37);
struct Break { i32 start, end; };

struct State {
    int format_version = 14;
    sv audio_filename; i32 audio_lead_in = 0, preview_time = -1, countdown = 1;
    sv sample_set{"Normal", 6}; double stack_leniency = 0.7; i32 mode = 0;
    bool letterbox_in_breaks = false, widescreen_storyboard = false, epilepsy_warning = false,
         special_style = false, use_skin_sprites = false, samples_match_playback_rate = false;
    i32 countdown_offset = 0; sv overlay_position, skin_preference;
    sv bookmarks; double distance_spacing = 0; i32 beat_divisor = 4, grid_size = 4; double timeline_zoom = 1;
    sv title, title_unicode, artist, artist_unicode, creator, version, source, tags;
    i64 beatmap_id = -1, beatmap_set_id = -1;
    double hp = 5, cs = 5, od = 5, ar = 5, slider_multiplier = 1.4, slider_tick_rate = 1;
    sv background, video;
    u32 malformed_lines = 0, storyboard_lines = 0, fast_path_lines = 0, slow_path_lines = 0;
    u32 n_hitobjects = 0, n_sliders = 0, n_points = 0, n_orphans = 0;
    bool ar_specified = false;
    // timing point blocks (one per [TimingPoints] section; real files have one)
    struct Block { const char* p; u32 n; } tp_blocks[8] = {};
    u32 n_tp_blocks = 0;
    char* tp_out = nullptr;  // cursor inside the current block
};
constexpr u32 kBreaksInline = 16, kColoursInline = 8;  // keeps Ctx inside one stack page
struct Ctx {
    char* out_begin;  // start of the output stream area
    char* out;        // cursor
    char* out_end;    // end of the arena
    char* spill;      // overflow arrays and orphaned points (arena tail)
    bool stream_started;  // a hit object record has been written
    State st;
    u32 n_breaks, n_colours;
    Break breaks[kBreaksInline];
    u32 colours[kColoursInline];
};
#define S (g->st)
// Overflow storage for absurd break/colour counts sits at the far end of
// the arena (address space only; never touched on real maps), so the
// binary has no .bss and exec creates no extra VMA for it.
constexpr u32 kOrphanCap = 1u << 20;  // pool points left by failed slider lines
constexpr size_t kSpillBytes = (1u << 15) * sizeof(Break) + (1u << 12) * sizeof(u32) + kOrphanCap * 8;
inline Break& break_at(u32 i) {
    return i < kBreaksInline ? g->breaks[i] : reinterpret_cast<Break*>(g->spill)[i - kBreaksInline];
}
inline u32& colour_at(u32 i) {
    return i < kColoursInline ? g->colours[i]
                              : reinterpret_cast<u32*>(g->spill + (1u << 15) * sizeof(Break))[i - kColoursInline];
}

inline void put_raw(const void* p, size_t n) { memcpy(g_out, p, n); g_out += n; }
inline void put_u8(u8 v) { *g_out++ = static_cast<char>(v); }
inline void put_u32(u32 v) { memcpy(g_out, &v, 4); g_out += 4; }
inline void put_i32(i32 v) { memcpy(g_out, &v, 4); g_out += 4; }
inline void put_i64(i64 v) { memcpy(g_out, &v, 8); g_out += 8; }
inline void put_f64(double v) { memcpy(g_out, &v, 8); g_out += 8; }
inline void put_str(sv s) { put_u32(static_cast<u32>(s.n)); memcpy(g_out, s.p, s.n); g_out += s.n; }

void flush() {
    const char* p = g_out_begin;
    while (p < g_out) {
        const long w = rt::write(1, p, static_cast<size_t>(g_out - p));
        if (w <= 0) rt::exit(3);
        p += w;
    }
    g_out = g_out_begin;
}
// Guarantees n contiguous bytes at g_out (a record is written after one
// ensure, so a flush never splits it).
inline void ensure(size_t n) {
    if (__builtin_expect(g_out + n > g_out_end, 0)) {
        flush();
        if (g_out + n > g_out_end) {
            const size_t len = (n + (1u << 20) + 4095) & ~size_t(4095);
            g_out_begin = g_out = static_cast<char*>(rt::mmap(nullptr, len, 3, 0x22));
            g_out_end = g_out_begin + len;
        }
    }
}

// ---------------------------------------------------------------- key/value sections
inline sv trim(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return {p, static_cast<size_t>(end - p)};
}
inline bool split_kv(const char* p, size_t len, sv& key, sv& val) {
    const auto* colon = static_cast<const char*>(memchr(p, ':', len));
    if (!colon) return false;
    key = trim(p, colon);
    val = trim(colon + 1, p + len);
    return key.n != 0;
}
inline bool parse_bool(sv v) { return v.n && v.p[0] == '1'; }
inline i32 parse_i32_field(sv v, i32 fallback) {
    i64 out;
    const char* q = parse_i64(v.p, v.p + v.n, out);
    return q == v.p ? fallback : clamp_i32(out);
}
inline double parse_f64_field(sv v, double fallback) {
    double out;
    const char* q = parse_double(v.p, v.p + v.n, out);
    return q == v.p ? fallback : out;
}
constexpr u32 key4(char a, char b, char c, char d) {
    return static_cast<u32>(static_cast<u8>(a)) | static_cast<u32>(static_cast<u8>(b)) << 8 |
           static_cast<u32>(static_cast<u8>(c)) << 16 | static_cast<u32>(static_cast<u8>(d)) << 24;
}
inline sv kv_value(const char* p, size_t len, size_t key_len) {
    size_t off = key_len + 1;
    if (off < len && p[off] == ' ') ++off;
    return {p + off, len > off ? len - off : 0};
}
// Key/value sections. Editor-emitted keys are unique on their first four
// bytes within a section, plus one disambiguating byte where two keys
// share them. One table and one code path for every field: in a fresh
// process each kv line would otherwise execute its own cold switch arm.
enum class KT : u8 { Str, I32, F64, Bool, I64 };
struct KvEntry {
    u32 key;      // first four bytes of the key
    u8 dis_pos;   // 0: no disambiguation; else byte index to test
    char dis_ch;  // expected byte at dis_pos for THIS entry
    u8 key_len;
    KT type;
    u16 off;      // offset of the field in State
    i32 dflt_i;
    double dflt_f;
};
#define KV(k, pos, ch, len, t, field, di, df) \
    KvEntry{key4(k[0], k[1], k[2], k[3]), pos, ch, len, KT::t, static_cast<u16>(__builtin_offsetof(State, field)), di, df}
constexpr KvEntry kGeneral[] = {
    KV("Audi", 5, 'F', 13, Str, audio_filename, 0, 0),
    KV("Audi", 5, 'L', 11, I32, audio_lead_in, 0, 0),
    KV("Prev", 0, 0, 11, I32, preview_time, -1, 0),
    KV("Coun", 9, 'O', 15, I32, countdown_offset, 0, 0),
    KV("Coun", 9, 0, 9, I32, countdown, 1, 0),  // dis_ch 0: matches when the other did not
    KV("Samp", 6, 'S', 9, Str, sample_set, 0, 0),
    KV("Samp", 6, 0, 24, Bool, samples_match_playback_rate, 0, 0),
    KV("Stac", 0, 0, 13, F64, stack_leniency, 0, 0.7),
    KV("Mode", 0, 0, 4, I32, mode, 0, 0),
    KV("Lett", 0, 0, 17, Bool, letterbox_in_breaks, 0, 0),
    KV("Wide", 0, 0, 20, Bool, widescreen_storyboard, 0, 0),
    KV("Epil", 0, 0, 15, Bool, epilepsy_warning, 0, 0),
    KV("Spec", 0, 0, 12, Bool, special_style, 0, 0),
    KV("UseS", 0, 0, 14, Bool, use_skin_sprites, 0, 0),
    KV("Over", 0, 0, 15, Str, overlay_position, 0, 0),
    KV("Skin", 0, 0, 14, Str, skin_preference, 0, 0),
};
constexpr KvEntry kEditor[] = {
    KV("Book", 0, 0, 9, Str, bookmarks, 0, 0),
    KV("Dist", 0, 0, 15, F64, distance_spacing, 0, 0),
    KV("Beat", 0, 0, 11, I32, beat_divisor, 4, 0),
    KV("Grid", 0, 0, 8, I32, grid_size, 4, 0),
    KV("Time", 0, 0, 12, F64, timeline_zoom, 0, 1),
};
constexpr KvEntry kMetadata[] = {
    KV("Titl", 5, 'U', 12, Str, title_unicode, 0, 0),
    KV("Titl", 5, 0, 5, Str, title, 0, 0),
    KV("Arti", 6, 'U', 13, Str, artist_unicode, 0, 0),
    KV("Arti", 6, 0, 6, Str, artist, 0, 0),
    KV("Crea", 0, 0, 7, Str, creator, 0, 0),
    KV("Vers", 0, 0, 7, Str, version, 0, 0),
    KV("Sour", 0, 0, 6, Str, source, 0, 0),
    KV("Tags", 0, 0, 4, Str, tags, 0, 0),
    KV("Beat", 7, 'S', 12, I64, beatmap_set_id, 0, 0),
    KV("Beat", 7, 0, 9, I64, beatmap_id, 0, 0),
};
constexpr KvEntry kDifficulty[] = {
    KV("HPDr", 0, 0, 11, F64, hp, 0, 5),
    KV("Circ", 0, 0, 10, F64, cs, 0, 5),
    KV("Over", 0, 0, 17, F64, od, 0, 5),
    KV("Appr", 0, 0, 12, F64, ar, 0, 5),
    KV("Slid", 6, 'M', 16, F64, slider_multiplier, 0, 1.4),
    KV("Slid", 6, 0, 14, F64, slider_tick_rate, 0, 1),
};
#undef KV

// Mirrors the library's per-key behaviour: the "Audi" key with an
// unexpected fifth byte stores nothing (both entries carry a dis_ch), a
// key whose second entry has dis_ch 0 takes it whenever the first fails.
template <size_t N>
inline void parse_kv_line(const KvEntry (&table)[N], const char* p, size_t len) {
    const u32 key = load_u32_le(p);
    for (size_t i = 0; i < N; ++i) {
        const KvEntry& e = table[i];
        if (e.key != key) continue;
        if (e.dis_pos && e.dis_ch && p[e.dis_pos] != e.dis_ch) continue;
        const sv v = kv_value(p, len, e.key_len);
        char* const f = reinterpret_cast<char*>(&S) + e.off;
        switch (e.type) {
            case KT::Str: *reinterpret_cast<sv*>(f) = v; break;
            case KT::I32: *reinterpret_cast<i32*>(f) = parse_i32_field(v, e.dflt_i); break;
            case KT::F64: *reinterpret_cast<double*>(f) = parse_f64_field(v, e.dflt_f); break;
            case KT::Bool: *reinterpret_cast<bool*>(f) = parse_bool(v); break;
            case KT::I64: {
                i64 id;
                if (parse_i64(v.p, v.p + v.n, id) != v.p) *reinterpret_cast<i64*>(f) = id;
                break;
            }
        }
        if (e.off == __builtin_offsetof(State, ar)) S.ar_specified = true;
        return;
    }
}
inline sv strip_quotes(sv v) {
    if (v.n >= 2 && v.p[0] == '"' && v.p[v.n - 1] == '"') return {v.p + 1, v.n - 2};
    return v;
}
void parse_event_line(const char* p, size_t len) {
    if (len == 0 || *p == ' ' || *p == '_') { ++S.storyboard_lines; return; }
    const char* end = p + len;
    const auto* c1 = static_cast<const char*>(memchr(p, ',', len));
    if (!c1) { ++S.storyboard_lines; return; }
    const sv f0{p, static_cast<size_t>(c1 - p)};
    const char* rest = c1 + 1;
    if (sv_eq(f0, "0")) {
        const auto* c2 = static_cast<const char*>(memchr(rest, ',', end - rest));
        if (!c2) return;
        const char* fname = c2 + 1;
        const auto* c3 = static_cast<const char*>(memchr(fname, ',', end - fname));
        S.background = strip_quotes(trim(fname, c3 ? c3 : end));
    } else if (sv_eq(f0, "1") || sv_eq(f0, "Video")) {
        const auto* c2 = static_cast<const char*>(memchr(rest, ',', end - rest));
        if (!c2) return;
        const char* fname = c2 + 1;
        const auto* c3 = static_cast<const char*>(memchr(fname, ',', end - fname));
        S.video = strip_quotes(trim(fname, c3 ? c3 : end));
    } else if (sv_eq(f0, "2") || sv_eq(f0, "Break")) {
        double start, stop;
        const char* q = parse_double(rest, end, start);
        if (q == rest || q >= end || *q != ',') return;
        const char* r = parse_double(q + 1, end, stop);
        if (r == q + 1) return;
        if (g->n_breaks < kBreaksInline + (1u << 15))
            break_at(g->n_breaks++) = {clamp_i32(static_cast<i64>(start)), clamp_i32(static_cast<i64>(stop))};
    } else {
        ++S.storyboard_lines;
    }
}
void parse_colour_kv(sv k, sv v) {
    if (k.n < 5 || memcmp(k.p, "Combo", 5) != 0) return;
    const char* p = v.p;
    const char* end = p + v.n;
    u32 rgb = 0;
    for (int i = 0; i < 3; ++i) {
        i64 c;
        const char* q = parse_i64(p, end, c);
        if (q == p) return;
        p = q;
        if (i < 2) {
            if (p >= end || *p != ',') return;
            ++p;
            while (p < end && *p == ' ') ++p;
        }
        rgb = (rgb << 8) | (static_cast<u32>(c) & 0xFF);
    }
    if (g->n_colours < kColoursInline + (1u << 12)) colour_at(g->n_colours++) = rgb;
}

// ---------------------------------------------------------------- timing points
inline TPRec& tp_slot() { return *reinterpret_cast<TPRec*>(S.tp_out); }
inline void tp_commit() { S.tp_out += sizeof(TPRec); ++S.tp_blocks[S.n_tp_blocks - 1].n; }

void parse_timing_point_line(const char* p, size_t len) {
    const char* end = p + len;
    TPRec& tp = tp_slot();
    double time, beat_length;
    const char* q = parse_double(p, end, time);
    if (q == p) { ++S.malformed_lines; return; }
    p = q;
    if (p >= end || *p != ',') { ++S.malformed_lines; return; }
    q = parse_double(++p, end, beat_length);
    if (q == p) { ++S.malformed_lines; return; }
    p = q;
    tp.time = time;
    tp.beat_length = beat_length;
    i64 rest[6] = {4, 0, 0, 100, 1, 0};
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
    tp.effects = static_cast<u32>(rest[5]);
    tp_commit();
}

struct TpGeom {
    u8 c[7];
    u8 bl_il, bl_fl1, bl_fl2, bl_frac;
    u8 bl_neg, bl_has_dot;
};
__attribute__((always_inline))
inline bool fast_parse_timing_point_masked(u64 commas, u64 nondig, const char* p, size_t len, TPRec& tp, TpGeom* geom) {
    const u64 m1 = _blsr_u64(commas), m2 = _blsr_u64(m1), m3 = _blsr_u64(m2), m4 = _blsr_u64(m3), m5 = _blsr_u64(m4), m6 = _blsr_u64(m5);
    const auto c0 = static_cast<u32>(_tzcnt_u64(commas)), c1 = static_cast<u32>(_tzcnt_u64(m1)), c2 = static_cast<u32>(_tzcnt_u64(m2)),
               c3 = static_cast<u32>(_tzcnt_u64(m3)), c4 = static_cast<u32>(_tzcnt_u64(m4)), c5 = static_cast<u32>(_tzcnt_u64(m5)),
               c6 = static_cast<u32>(_tzcnt_u64(m6));
    bool valid = _mm_popcnt_u64(commas) == 7;
    valid &= (c0 - 1) <= 7;
    tp.time = static_cast<double>(swar_parse_u64_safe(p, ((c0 - 1) & 7) + 1));
    const char* f = p + c0 + 1;
    const bool neg = *f == '-';
    f += neg;
    const u32 flen = c1 - c0 - 1 - neg;
    const auto int_len = static_cast<u32>(_tzcnt_u64(nondig >> (f - p)));
    const bool has_dot = int_len < flen;
    valid &= !has_dot || f[int_len] == '.';
    const u32 frac_len = flen - int_len - has_dot;
    valid &= (int_len - 1) <= 7;
    valid &= frac_len <= 13;
    const u32 il = ((int_len - 1) & 7) + 1;
    const u32 fl1 = frac_len <= 8 ? frac_len : 8;
    const u32 fl2 = frac_len - fl1;
    const char* fp = f + il + 1;
    u64 mant = swar_parse_u64_safe(f, il);
    const u64 fm1 = fl1 ? swar_parse_u64_safe(fp, fl1) : 0;
    const u64 fm2 = fl2 ? swar_parse_u64_safe(fp + 8, fl2) : 0;
    mant = mant * kPow10u[fl1] + fm1;
    mant = mant * kPow10u[fl2 & 7] + fm2;
    double bl = static_cast<double>(mant) / kPow10[frac_len <= 13 ? frac_len : 0];
    bl = std::bit_cast<double>(std::bit_cast<u64>(bl) | (static_cast<u64>(neg) << 63));
    tp.beat_length = bl;
    valid &= _mm_popcnt_u64(nondig) == 7 + static_cast<int>(has_dot) + static_cast<int>(neg);
    const u32 t0 = c2 - c1 - 1, t1 = c3 - c2 - 1, t2 = c4 - c3 - 1, t3 = c5 - c4 - 1, t4 = c6 - c5 - 1,
              t5 = static_cast<u32>(len) - c6 - 1;
    valid &= ((t0 - 1) | (t1 - 1) | (t2 - 1) | (t3 - 1) | (t4 - 1) | (t5 - 1)) <= 7;
    tp.meter = static_cast<i32>(swar_parse_u64_safe(p + c1 + 1, ((t0 - 1) & 7) + 1));
    tp.sample_set = static_cast<i32>(swar_parse_u64_safe(p + c2 + 1, ((t1 - 1) & 7) + 1));
    tp.sample_index = static_cast<i32>(swar_parse_u64_safe(p + c3 + 1, ((t2 - 1) & 7) + 1));
    tp.volume = static_cast<i32>(swar_parse_u64_safe(p + c4 + 1, ((t3 - 1) & 7) + 1));
    tp.uninherited = swar_parse_u64_safe(p + c5 + 1, ((t4 - 1) & 7) + 1) != 0;
    tp.effects = static_cast<u32>(swar_parse_u64_safe(p + c6 + 1, ((t5 - 1) & 7) + 1));
    if (geom && valid) {
        for (int i = 0; i < 7; ++i) geom->c[i] = 0;
        geom->c[0] = static_cast<u8>(c0); geom->c[1] = static_cast<u8>(c1); geom->c[2] = static_cast<u8>(c2);
        geom->c[3] = static_cast<u8>(c3); geom->c[4] = static_cast<u8>(c4); geom->c[5] = static_cast<u8>(c5);
        geom->c[6] = static_cast<u8>(c6);
        geom->bl_il = static_cast<u8>(il); geom->bl_fl1 = static_cast<u8>(fl1); geom->bl_fl2 = static_cast<u8>(fl2);
        geom->bl_frac = static_cast<u8>(frac_len); geom->bl_neg = neg; geom->bl_has_dot = has_dot;
    }
    return valid;
}

struct TpShapeRow {
    u64 commas = 0, nondig = 0;
    u32 len = 0;
    TpGeom g{};
    u8 simd_tails = 0;
    alignas(16) int8_t shuf[16];
    alignas(16) u8 subv[16];
};
struct TpShapeCache {
    TpShapeRow rows[16];
    static u32 slot(u64 commas) { return static_cast<u32>((commas * 0x9E3779B97F4A7C15ull) >> 60); }
};
inline bool tp_shape_match(const TpShapeRow& row, u64 commas, u64 nondig, size_t len, const char* p) {
    if (row.commas != commas || row.nondig != nondig || row.len != static_cast<u32>(len)) return false;
    const TpGeom& g = row.g;
    const bool neg_ok = !g.bl_neg || p[g.c[0] + 1] == '-';
    const bool dot_ok = !g.bl_has_dot || p[g.c[0] + 1 + g.bl_neg + g.bl_il] == '.';
    return neg_ok && dot_ok;
}
inline void tp_shape_insert(TpShapeCache& cache, u64 commas, u64 nondig, size_t len, const TpGeom& g) {
    TpShapeRow& r = cache.rows[TpShapeCache::slot(commas)];
    r.commas = commas; r.nondig = nondig; r.len = static_cast<u32>(len); r.g = g;
    const u32 base = g.c[1] + 1;
    const u32 span = static_cast<u32>(len) - base;
    u64 shuf_lo = 0, shuf_hi = 0, sub_lo = 0, sub_hi = 0;
    u32 maxlen = 0;
    for (int i = 0; i < 6; ++i) {
        const u32 hi = i < 5 ? g.c[i + 2] : static_cast<u32>(len);
        const u32 off = g.c[i + 1] + 1 - base;
        const u32 flen = hi - g.c[i + 1] - 1;
        maxlen = flen > maxlen ? flen : maxlen;
        const u64 pair = flen == 2 ? (off | ((off + 1) << 8)) : (0x80u | (off << 8));
        const u64 sub = flen == 2 ? 0x3030u : 0x3000u;
        if (i < 4) { shuf_lo |= pair << (16 * i); sub_lo |= sub << (16 * i); }
        else { shuf_hi |= pair << (16 * (i - 4)); sub_hi |= sub << (16 * (i - 4)); }
    }
    r.simd_tails = span <= 16 && maxlen <= 2;
    shuf_hi |= 0x8080808000000000ull;
    memcpy(r.shuf, &shuf_lo, 8); memcpy(r.shuf + 8, &shuf_hi, 8);
    memcpy(r.subv, &sub_lo, 8); memcpy(r.subv + 8, &sub_hi, 8);
}
inline void tp_shape_convert(const TpShapeRow& r, const char* p, TPRec& tp) {
    const TpGeom& g = r.g;
    tp.time = static_cast<double>(swar_parse_u64(p, g.c[0]));
    const char* f = p + g.c[0] + 1 + g.bl_neg;
    u64 mant = swar_parse_u64_safe(f, g.bl_il);
    const char* fp = f + g.bl_il + 1;
    const u64 fm1 = g.bl_fl1 ? swar_parse_u64_safe(fp, g.bl_fl1) : 0;
    const u64 fm2 = g.bl_fl2 ? swar_parse_u64_safe(fp + 8, g.bl_fl2) : 0;
    mant = mant * kPow10u[g.bl_fl1] + fm1;
    mant = mant * kPow10u[g.bl_fl2] + fm2;
    double bl = static_cast<double>(mant) / kPow10[g.bl_frac];
    bl = std::bit_cast<double>(std::bit_cast<u64>(bl) | (static_cast<u64>(g.bl_neg) << 63));
    tp.beat_length = bl;
    if (r.simd_tails) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + g.c[1] + 1));
        const __m128i gathered = _mm_shuffle_epi8(v, _mm_load_si128(reinterpret_cast<const __m128i*>(r.shuf)));
        const __m128i digits = _mm_sub_epi8(gathered, _mm_load_si128(reinterpret_cast<const __m128i*>(r.subv)));
        const __m128i vals = _mm_maddubs_epi16(digits, _mm_set1_epi16(0x010A));
        alignas(16) uint16_t t[8];
        _mm_store_si128(reinterpret_cast<__m128i*>(t), vals);
        tp.meter = t[0]; tp.sample_set = t[1]; tp.sample_index = t[2]; tp.volume = t[3];
        tp.uninherited = t[4] != 0; tp.effects = t[5];
    } else {
        const u32 len = r.len;
        const u32 t0 = g.c[2] - g.c[1] - 1, t1 = g.c[3] - g.c[2] - 1, t2 = g.c[4] - g.c[3] - 1,
                  t3 = g.c[5] - g.c[4] - 1, t4 = g.c[6] - g.c[5] - 1, t5 = len - g.c[6] - 1;
        tp.meter = static_cast<i32>(swar_parse_u64(p + g.c[1] + 1, t0));
        tp.sample_set = static_cast<i32>(swar_parse_u64(p + g.c[2] + 1, t1));
        tp.sample_index = static_cast<i32>(swar_parse_u64(p + g.c[3] + 1, t2));
        tp.volume = static_cast<i32>(swar_parse_u64(p + g.c[4] + 1, t3));
        tp.uninherited = swar_parse_u64(p + g.c[5] + 1, t4) != 0;
        tp.effects = static_cast<u32>(swar_parse_u64(p + g.c[6] + 1, t5));
    }
}

// Allocates a timing block sized like the library's reserve (section
// bytes / 17 + 4 entries) from the output arena; if the hitobject stream
// has already started, it is flushed first so the block stays contiguous.
void begin_timing_block(const char* p, const char* file_end) {
    const auto* bracket = static_cast<const char*>(memchr(p, '[', static_cast<size_t>(file_end - p)));
    const char* section_end = bracket ? bracket : file_end;
    const size_t bytes = (static_cast<size_t>(section_end - p) / 17 + 4) * sizeof(TPRec) + 64;
    // Normally only the 8-byte magic is pending: move it past the block
    // instead of spending a write syscall on it.
    const size_t pending = static_cast<size_t>(g_out - g_out_begin);
    if (pending > 64) flush();
    ensure(bytes + 64);
    if (S.n_tp_blocks == 8) rt::exit(4);
    char* const block = g_out_begin + (g_out - g_out_begin > 64 ? 0 : 0);
    (void)block;
    if (g_out - g_out_begin <= 64) {
        const size_t keep = static_cast<size_t>(g_out - g_out_begin);
        char tmp[64];
        memcpy(tmp, g_out_begin, keep);
        S.tp_blocks[S.n_tp_blocks++] = {g_out_begin, 0};
        S.tp_out = g_out_begin;
        g_out_begin += bytes;
        memcpy(g_out_begin, tmp, keep);
        g_out = g_out_begin + keep;
    } else {
        S.tp_blocks[S.n_tp_blocks++] = {g_out, 0};
        S.tp_out = g_out;
        g_out += bytes;
        g_out_begin = g_out;
    }
}

const char* parse_timing_points_section(const char* p, const char* file_end) {
    begin_timing_block(p, file_end);
    TpShapeCache cache{};
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
        const u64 nl = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, _mm256_set1_epi8('\n')))) |
                       static_cast<u64>(static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n'))))) << 32;
        const char* next_line;
        size_t len;
        if (nl) {
            const auto pos = static_cast<u32>(_tzcnt_u64(nl));
            len = pos - (pos > 0 && p[pos - 1] == '\r');
            next_line = p + pos + 1;
        } else {
            const auto* m = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(file_end - p)));
            const char* le = m ? m : file_end;
            len = static_cast<size_t>(le - p) - (le > p && le[-1] == '\r');
            next_line = m ? m + 1 : file_end;
        }
        if (len <= 64 && len >= 15) [[likely]] {
            const u64 line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
            const u64 commas = (comma_mask32(a) | static_cast<u64>(comma_mask32(b)) << 32) & line_mask;
            const u64 nondig = (nondigit_mask32(a) | static_cast<u64>(nondigit_mask32(b)) << 32) & line_mask;
            const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
            TPRec& tp = tp_slot();
            if (tp_shape_match(row, commas, nondig, len, p)) {
                tp_shape_convert(row, p, tp);
                tp_commit();
            } else {
                TpGeom geom;
                if (fast_parse_timing_point_masked(commas, nondig, p, len, tp, &geom)) {
                    tp_shape_insert(cache, commas, nondig, len, geom);
                    tp_commit();
                } else {
                    parse_timing_point_line(p, len);
                }
            }
        } else {
            parse_timing_point_line(p, len);
        }
        p = next_line;
    }
    return p;
}

// ---------------------------------------------------------------- hit objects
inline const char* parse_coord(const char* p, const char* end, i32& out) {
    const u32 run = digit_run8(p);
    if (run - 1 <= 3) { out = static_cast<i32>(swar_parse_u32(p, run)); return p + run; }
    i64 v;
    const char* q = parse_i64(p, end, v);
    if (q == p) return p;
    out = clamp_i32(v);
    return q;
}
inline const char* parse_slider_length(const char* p, double& out) {
    const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    const u64 nd = nondigit_mask32(v);
    const auto il = static_cast<u32>(_tzcnt_u64(nd));
    const bool has_dot = p[il] == '.';
    const u32 fl = has_dot ? static_cast<u32>(_tzcnt_u64(nd >> (il + 1))) : 0;
    if ((il - 1) > 7 || fl > 13 || il + fl > 18) return nullptr;
    const u32 fl1 = fl <= 8 ? fl : 8, fl2 = fl - fl1;
    u64 mant = swar_parse_u64(p, il);
    const char* fp = p + il + 1;
    if (fl1) mant = mant * kPow10u[fl1] + swar_parse_u64(fp, fl1);
    if (fl2) mant = mant * kPow10u[fl2] + swar_parse_u64(fp + 8, fl2);
    double d = static_cast<double>(mant);
    if (fl) d /= kPow10[fl];
    out = d;
    return has_dot ? fp + fl : p + il;
}

struct Pt { i32 x, y; };

// The library leaves a failed slider line's points in its pool once the
// point loop has finished; they are part of the output (trailer).
inline void keep_orphans(const Pt* p, u32 n) {
    if (S.n_orphans + n > kOrphanCap) rt::exit(6);
    memcpy(g->spill + (1u << 15) * sizeof(Break) + (1u << 12) * sizeof(u32) + size_t(S.n_orphans) * 8, p, size_t(n) * 8);
    S.n_orphans += n;
}

// Slider block: u32 point_count, points, i32 slides, f64 length,
// u8 curve_type, str edge_sounds, str edge_sets. Returns false to reject
// the line (the caller rewinds the whole record). `hs` receives the
// trailing hit_sample.
bool parse_slider_params(const char* p, const char* end, sv& hs) {
    if (p >= end) return false;
    const char curve_type = *p++;
    char* const count_slot = g_out;
    g_out += 4;
    Pt* w = reinterpret_cast<Pt*>(g_out);
    Pt* const w0 = w;
    while (p < end && *p == '|') {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 1));
        const __m128i biased = _mm_add_epi8(v, _mm_set1_epi8(80));
        const auto nd = static_cast<u32>(_mm_movemask_epi8(_mm_cmpgt_epi8(biased, _mm_set1_epi8(-119))));
        const auto colon = static_cast<u32>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_set1_epi8(':'))));
        const u32 xl = _tzcnt_u32(nd);
        const u32 yl = _tzcnt_u32(nd >> (xl + 1));
        if ((xl - 1) > 3 || (yl - 1) > 3 || !((colon >> xl) & 1)) break;
        w->x = static_cast<i32>(swar_parse_u32(p + 1, xl));
        w->y = static_cast<i32>(swar_parse_u32(p + 2 + xl, yl));
        ++w;
        p += 2 + xl + yl;
    }
    while (p < end && *p == '|') {
        i32 px, py;
        const char* q = parse_coord(p + 1, end, px);
        if (q == p + 1 || *q != ':') return false;
        const char* r = parse_coord(q + 1, end, py);
        if (r == q + 1) return false;
        w->x = px; w->y = py; ++w;
        p = r;
    }
    const auto npts = static_cast<u32>(w - w0);
    memcpy(count_slot, &npts, 4);
    g_out = reinterpret_cast<char*>(w);
    // From here on the library leaves the points in its pool on failure.
    S.n_points += npts;
    if (p >= end || *p != ',') { keep_orphans(w0, npts); return false; }
    ++p;
    const u32 srun = digit_run8(p);
    if (srun - 1 > 6) { keep_orphans(w0, npts); return false; }
    const auto slides = static_cast<i32>(swar_parse_u64(p, srun));
    p += srun;
    if (p >= end || *p != ',') { keep_orphans(w0, npts); return false; }
    double length;
    const char* q = parse_slider_length(p + 1, length);
    if (!q) q = parse_double(p + 1, end, length);
    if (q == p + 1) { keep_orphans(w0, npts); return false; }
    p = q;
    sv edge_sounds{}, edge_sets{};
    hs = {};
    if (p < end && *p == ',') {
        ++p;
        const auto span = static_cast<size_t>(end - p);
        if (span <= 32) {
            const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
            const auto cm = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(',')))) &
                            static_cast<u32>((1ull << span) - 1);
            const u32 c0 = _tzcnt_u32(cm);
            const u32 c1 = _tzcnt_u32(_blsr_u32(cm));
            if (c0 >= span) {
                edge_sounds = {p, span};
            } else if (c1 >= span) {
                edge_sounds = {p, c0};
                edge_sets = {p + c0 + 1, span - c0 - 1};
            } else {
                edge_sounds = {p, c0};
                edge_sets = {p + c0 + 1, c1 - c0 - 1};
                hs = {p + c1 + 1, span - c1 - 1};
            }
        } else {
            sv extra[3];
            int n = 0;
            while (n < 3 && p < end) {
                const auto* c = static_cast<const char*>(memchr(p, ',', end - p));
                const char* fend = c ? c : end;
                extra[n++] = {p, static_cast<size_t>(fend - p)};
                p = fend + 1;
            }
            edge_sounds = extra[0]; edge_sets = extra[1]; hs = extra[2];
        }
    }
    put_i32(slides);
    put_f64(length);
    put_u8(static_cast<u8>(curve_type));
    put_str(edge_sounds);
    put_str(edge_sets);
    return true;
}

// Everything after the prefix; appends the slider block (if any) and the
// hit_sample bytes, sets h.slider / h.hs_len / h.end_time as the library.
bool finish_hitobject(HO& h, const char* p, const char* end) {
    if (h.type & 2) {
        if (p >= end || *p != ',') return false;
        sv hs;
        if (!parse_slider_params(p + 1, end, hs)) return false;
        h.slider = S.n_sliders++;
        h.hs_len = static_cast<u32>(hs.n);
        memcpy(g_out, hs.p, hs.n);
        g_out += hs.n;
        return true;
    }
    sv hs{};
    if (h.type & 8 || h.type & 128) {
        if (p >= end || *p != ',') return false;
        i64 t;
        const char* q = parse_i64(p + 1, end, t);
        if (q == p + 1) return false;
        h.end_time = clamp_i32(t);
        p = q;
        if ((h.type & 128) && p < end && *p == ':') ++p;
        else if (p < end && *p == ',') ++p;
        else {
            h.hs_len = 0;
            return true;
        }
        hs = {p, static_cast<size_t>(end - p)};
    } else if (p < end && *p == ',') {
        hs = {p + 1, static_cast<size_t>(end - (p + 1))};
    }
    h.hs_len = static_cast<u32>(hs.n);
    memcpy(g_out, hs.p, hs.n);
    g_out += hs.n;
    return true;
}

const char* parse_hitobjects_section(const char* p, const char* file_end) {
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        const __m256i ascii = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const auto nl_mask = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(ascii, _mm256_set1_epi8('\n'))));
        const char* nl;
        if (nl_mask) {
            nl = p + _tzcnt_u32(nl_mask);
        } else {
            const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
            const auto nl2 = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n'))));
            nl = nl2 ? p + 32 + _tzcnt_u32(nl2)
                     : static_cast<const char*>(memchr(p + 64, '\n', file_end - p > 64 ? static_cast<size_t>(file_end - p) - 64 : 0));
        }
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const char* next_line = nl ? nl + 1 : file_end;
        const auto len = static_cast<size_t>(line_end - p);
        // Worst case record: 32-byte head + points (8 bytes per >=4 input
        // bytes) + slider scalars + strings (<= line) + counts.
        ensure(4 * len + 128);
        char* const rec = g_out;
        HO& h = *reinterpret_cast<HO*>(rec);
        const int next = fast_parse_prefix(ascii, p, h);
        g_out += sizeof(HO);
        bool ok;
        if (next >= 0) {
            ++S.fast_path_lines;
            ok = finish_hitobject(h, p + next, line_end);
        } else {
            h.end_time = 0;
            h.slider = kNoSlider;
            h.hs_len = 0;
            const int sn = scalar_parse_prefix(p, len, h);
            if (sn >= 0) {
                ++S.slow_path_lines;
                ok = finish_hitobject(h, p + sn, line_end);
            } else {
                ok = false;
            }
        }
        if (ok) {
            ++S.n_hitobjects;
        } else {
            g_out = rec;
            ++S.malformed_lines;
        }
        p = next_line;
    }
    return p;
}

const char* parse_events_section(const char* p, const char* file_end) {
    u32 storyboard_lines = 0;
    while (p < file_end) {
        const char c = *p;
        if (c == '\r' || c == '\n') { ++p; continue; }
        if (c == '[') break;
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32));
        const u64 nl = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, _mm256_set1_epi8('\n')))) |
                       static_cast<u64>(static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n'))))) << 32;
        const char* line = p;
        const char* next_line;
        const char* line_end;
        if (nl) {
            line_end = p + _tzcnt_u64(nl);
            next_line = line_end + 1;
        } else {
            const auto* m = static_cast<const char*>(memchr(p + 64, '\n', file_end - p > 64 ? static_cast<size_t>(file_end - p) - 64 : 0));
            line_end = m ? m : file_end;
            next_line = m ? m + 1 : file_end;
        }
        if (line_end[-1] == '\r') --line_end;
        p = next_line;
        if (c == ' ' || c == '_') { ++storyboard_lines; continue; }
        const auto len = static_cast<size_t>(line_end - line);
        if (len >= 2 && c == '/' && line[1] == '/') continue;
        parse_event_line(line, len);
    }
    S.storyboard_lines += storyboard_lines;
    return p;
}

// ---------------------------------------------------------------- sections / main loop
enum class Section : u8 { None, General, Editor, Metadata, Difficulty, Events, TimingPoints, Colours, HitObjects, Unknown };
inline Section match_section(const char* p, size_t len) {
    if (len < 3) return Section::Unknown;
    switch (p[1]) {
        case 'G': return Section::General;
        case 'E': return p[2] == 'd' ? Section::Editor : Section::Events;
        case 'M': return Section::Metadata;
        case 'D': return Section::Difficulty;
        case 'T': return Section::TimingPoints;
        case 'C': return Section::Colours;
        case 'H': return Section::HitObjects;
        default: return Section::Unknown;
    }
}
inline const char* find_version_tag(const char* p, size_t len) {
    static constexpr char tag[] = "osu file format v";
    constexpr size_t tl = sizeof tag - 1;
    if (len < tl) return nullptr;
    for (size_t i = 0; i + tl <= len; ++i)
        if (p[i] == 'o' && memcmp(p + i, tag, tl) == 0) return p + i + tl;
    return nullptr;
}

void parse(const char* data, size_t size) {
    const char* p = data;
    const char* file_end = data + size;
    if (size >= 3 && static_cast<u8>(p[0]) == 0xEF && static_cast<u8>(p[1]) == 0xBB && static_cast<u8>(p[2]) == 0xBF) p += 3;
    Section sec = Section::None;
    while (p < file_end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', file_end - p));
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const size_t len = static_cast<size_t>(line_end - p);
        if (len == 0) goto next_line;
        if (*p == '[') {
            sec = match_section(p, len);
            if (sec == Section::HitObjects) {
                p = parse_hitobjects_section(nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
            }
            if (sec == Section::TimingPoints) {
                p = parse_timing_points_section(nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
            }
            if (sec == Section::Events) {
                p = parse_events_section(nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
            }
            goto next_line;
        }
        if (len >= 2 && p[0] == '/' && p[1] == '/') goto next_line;
        switch (sec) {
            case Section::None: {
                if (const char* vp = find_version_tag(p, len)) {
                    i64 ver;
                    if (parse_i64(vp, line_end, ver) != vp) S.format_version = static_cast<int>(ver);
                }
                break;
            }
            case Section::General: if (len >= 5) parse_kv_line(kGeneral, p, len); break;
            case Section::Editor: if (len >= 5) parse_kv_line(kEditor, p, len); break;
            case Section::Metadata: if (len >= 5) parse_kv_line(kMetadata, p, len); break;
            case Section::Difficulty: if (len >= 5) parse_kv_line(kDifficulty, p, len); break;
            case Section::Colours: {
                sv k, v;
                if (split_kv(p, len, k, v)) parse_colour_kv(k, v);
                break;
            }
            default: break;  // Events / TimingPoints / HitObjects have their own loops; Unknown ignored
        }
    next_line:
        p = nl ? nl + 1 : file_end;
    }
    if (!S.ar_specified) S.ar = S.od;
}

void emit_trailer() {
    size_t tp_bytes = 0;
    for (u32 i = 0; i < S.n_tp_blocks; ++i) tp_bytes += S.tp_blocks[i].n * sizeof(TPRec);
    const size_t need = 4 + 12 + 4 + 8 * 6 + 4 * 6 + 8 * 16 + 8 * 2 + 64 +
                        S.audio_filename.n + S.sample_set.n + S.overlay_position.n + S.skin_preference.n + S.bookmarks.n +
                        S.title.n + S.title_unicode.n + S.artist.n + S.artist_unicode.n + S.creator.n + S.version.n +
                        S.source.n + S.tags.n + S.background.n + S.video.n + 4 + 8 * g->n_breaks + 4 + 4 * g->n_colours + 4 +
                        tp_bytes + 8 + 22 * 4 + 12 + size_t(S.n_orphans) * 8;
    ensure(need);
    put_raw("TRLR", 4);
    put_u32(S.n_hitobjects);
    put_u32(S.n_sliders);
    put_u32(S.n_points);
    put_i32(S.format_version);
    put_str(S.audio_filename);
    put_i32(S.audio_lead_in);
    put_i32(S.preview_time);
    put_i32(S.countdown);
    put_str(S.sample_set);
    put_f64(S.stack_leniency);
    put_i32(S.mode);
    put_u8(S.letterbox_in_breaks);
    put_u8(S.widescreen_storyboard);
    put_u8(S.epilepsy_warning);
    put_u8(S.special_style);
    put_u8(S.use_skin_sprites);
    put_u8(S.samples_match_playback_rate);
    put_i32(S.countdown_offset);
    put_str(S.overlay_position);
    put_str(S.skin_preference);
    put_str(S.bookmarks);
    put_f64(S.distance_spacing);
    put_i32(S.beat_divisor);
    put_i32(S.grid_size);
    put_f64(S.timeline_zoom);
    put_str(S.title);
    put_str(S.title_unicode);
    put_str(S.artist);
    put_str(S.artist_unicode);
    put_str(S.creator);
    put_str(S.version);
    put_str(S.source);
    put_str(S.tags);
    put_i64(S.beatmap_id);
    put_i64(S.beatmap_set_id);
    put_f64(S.hp);
    put_f64(S.cs);
    put_f64(S.od);
    put_f64(S.ar);
    put_f64(S.slider_multiplier);
    put_f64(S.slider_tick_rate);
    put_str(S.background);
    put_str(S.video);
    put_u32(g->n_breaks);
    for (u32 i = 0; i < g->n_breaks; ++i) { put_i32(break_at(i).start); put_i32(break_at(i).end); }
    put_u32(g->n_colours);
    for (u32 i = 0; i < g->n_colours; ++i) put_u32(colour_at(i));
    u32 ntp = 0;
    for (u32 i = 0; i < S.n_tp_blocks; ++i) ntp += S.tp_blocks[i].n;
    put_u32(ntp);
    for (u32 i = 0; i < S.n_tp_blocks; ++i) put_raw(S.tp_blocks[i].p, S.tp_blocks[i].n * sizeof(TPRec));
    put_u32(S.malformed_lines);
    put_u32(S.storyboard_lines);
    put_u32(S.fast_path_lines);
    put_u32(S.slow_path_lines);
    put_u32(S.n_orphans);
    put_raw(g->spill + (1u << 15) * sizeof(Break) + (1u << 12) * sizeof(u32), size_t(S.n_orphans) * 8);
}

// Arena placement. With multi-size THP in madvise mode, an anonymous
// fault takes the largest enabled folio order whose aligned block lies
// inside the VMA and is still unpopulated. The arena is therefore mapped
// at a 2 MB-aligned base plus `offset`, where offset is the folio size
// that should hold the whole working set (input + padding + ~1.3x output),
// and the VMA stops short of the next 2 MB boundary: the first touch gets
// exactly one `offset`-sized folio, a working set that outgrows it walks
// up through the next sizes (2x, 4x, ...) instead of falling back to 4 KB
// pages, and no 2 MB folio (65 us to zero) can be chosen inside the first
// window. Files whose working set exceeds ~2 MB get more windows.
// 2 MB-aligned and just above the binary (0x400000 + <1 MB), so the arena
// shares the binary's upper page-table pages instead of allocating and
// zeroing its own; the far address is the fallback if that range is taken.
constexpr uintptr_t kArenaBase = 0x800000ull;
constexpr uintptr_t kArenaBaseFar = 0x100000000000ull;

[[noreturn]] void run(int argc, char** argv) {
    Ctx ctx;
    g = &ctx;
    ctx.st = State{};
    ctx.n_breaks = ctx.n_colours = 0;
    if (argc < 2) rt::exit(2);
#ifdef ABLATE_EXIT_ONLY
    rt::exit(0);
#endif
    const long fd = rt::open_ro(argv[1]);
    if (fd < 0) rt::exit(1);
    const long ssize = rt::fstat_size(static_cast<int>(fd));
    if (ssize < 0) rt::exit(1);
    const auto size = static_cast<size_t>(ssize);
    const size_t est = size + 128 + size + size / 4 + size / 8 + 8192;
    size_t offset = 64u << 10;
    while (offset < est && offset < (1u << 20)) offset <<= 1;
    size_t len = (2u << 20) - offset;
    if (est + kSpillBytes > len) len += ((est + kSpillBytes - len + (2u << 20) - 1) / (2u << 20)) * (2u << 20);
    constexpr int kFlags = 0x22 | 0x4000 /*PRIVATE|ANON|NORESERVE*/ | 0x100000 /*FIXED_NOREPLACE*/;
    char* arena = static_cast<char*>(rt::mmap(reinterpret_cast<void*>(kArenaBase + offset), len, 3 /*RW*/, kFlags));
    if (reinterpret_cast<long>(arena) < 0 && reinterpret_cast<long>(arena) > -4096)
        arena = static_cast<char*>(rt::mmap(reinterpret_cast<void*>(kArenaBaseFar + offset), len, 3, kFlags));
    if (reinterpret_cast<long>(arena) < 0 && reinterpret_cast<long>(arena) > -4096) rt::exit(1);
#ifndef ABLATE_NO_MADVISE
    rt::madvise(arena, len, 14 /*MADV_HUGEPAGE*/);
#endif
    ctx.spill = arena + len - kSpillBytes;
    ctx.stream_started = false;
#ifdef INPUT_MMAP
    // Experiment: map the page-cache pages over the arena start instead of
    // copying them in. The anonymous arena continues right after the last
    // file page, so the 128-byte padding past EOF is zero either way; the
    // output starts at the next 64 KB boundary so its first touch is still
    // folio-eligible.
    const size_t got = size;
    if (size && rt::mmap(arena, size, 1 /*READ*/, 0x02 /*PRIVATE*/ | 0x10 /*FIXED*/ | 0x8000 /*POPULATE*/, static_cast<int>(fd)) != arena)
        rt::exit(1);
    g_out_begin = g_out = arena + ((got + 128 + 65535) & ~size_t(65535));
    g_out_end = ctx.spill;
#else
    size_t got = 0;
    while (got < size) {
        const long r = rt::read(static_cast<int>(fd), arena + got, size - got);
        if (r <= 0) break;
        got += static_cast<size_t>(r);
    }
    // 128 zero bytes of padding follow the input; the output area starts after them.
    g_out_begin = g_out = arena + ((got + 128 + 63) & ~size_t(63));
    g_out_end = ctx.spill;
#endif
#ifdef ABLATE_AFTER_READ
    rt::exit(0);
#endif
    put_raw("FOSUDMP3", 8);
    parse(arena, got);
    emit_trailer();
#ifdef ABLATE_NO_WRITE
    rt::exit(0);
#endif
    flush();
    rt::exit(0);
}

}  // namespace

#ifndef SPEEDRUN_HOSTED
void* fosu_memchr(const void* s, int c, size_t n) __asm__("memchr");
void* fosu_memchr(const void* s, int c, size_t n) {
    const char* p = static_cast<const char*>(s);
    const char* end = p + n;
    const __m256i v = _mm256_set1_epi8(static_cast<char>(c));
    while (p < end) {
        const u32 m = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), v)));
        const size_t rem = static_cast<size_t>(end - p);
        const u32 mm = rem >= 32 ? m : (m & ((1u << rem) - 1));
        if (mm) return const_cast<char*>(p + _tzcnt_u32(mm));
        p += 32;
    }
    return nullptr;
}

extern "C" [[noreturn]] void __assert_fail(const char*, const char*, unsigned, const char*) { rt::exit(5); }

extern "C" [[noreturn]] void main_entry(long* sp) {
    run(static_cast<int>(sp[0]), reinterpret_cast<char**>(sp + 1));
}

__asm__(R"(
.text
.global _start
_start:
    xor %rbp, %rbp
    mov %rsp, %rdi
    and $-16, %rsp
    call main_entry
    hlt
)");
#else
// Hosted twin for profile-guided training: identical parser code, linked
// against glibc so gcov can flush its counters at exit.
#include <cstdlib>
namespace rt { [[noreturn]] void exit(int code) { ::exit(code); } }
int main(int argc, char** argv) { run(argc, argv); }
#endif
