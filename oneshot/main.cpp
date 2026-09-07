// fosu one-shot: the fosu parser as a one-shot process — exec, read one .osu
// file, parse, write the canonical dump (oneshot/dump.hpp) to stdout,
// exit — with nothing between the kernel and the parser. Freestanding:
// no libc, no libstdc++, one anonymous arena (input, padding, timing
// buffer and output stream back to back, so multi-size THP can back the
// whole working set with one or two folios), hit objects written straight
// from the SIMD prefix store into the output stream, and everything else
// gathered into a trailer. Numeric kernels, metadata and hitobject
// framing are shared with the library. bench/oneshot_reference.cpp
// serializes the ordinary library for exact comparison.
#include <immintrin.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "runtime.hpp"
#define FASTFLOAT_ASSERT(x) ((void)0)
#define FASTFLOAT_DEBUG_ASSERT(x) ((void)0)
#include <fosu/detail/prefix.hpp>
#include <fosu/detail/object_tail.hpp>
#include <fosu/detail/timing.hpp>
#include <fosu/detail/slider.hpp>
#include <fosu/detail/metadata.hpp>
#include <fosu/detail/sections.hpp>
#include <fosu/detail/section_names.hpp>

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
    operator std::string_view() const { return {p, n}; }
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

// Shared bounded numeric kernels.
using fosu::detail::load_u32_le;
using fosu::detail::load_u64_le;
using fosu::detail::digit_run8;
using fosu::detail::swar_parse_u32;
using fosu::detail::swar_parse_u64;
using fosu::detail::swar_parse_u64_safe;
using fosu::detail::is_digit;
using fosu::detail::parse_u64;
using fosu::detail::parse_i64;
using fosu::detail::clamp_i32;
using fosu::detail::kPow10;
using fosu::detail::kPow10u;

using fosu::detail::parse_double;

// Both output representations use the library's prefix kernel. Numeric fields
// have the library layout; the final word is the sample length.
struct __attribute__((packed)) HO {
    i32 x, y;
    u32 type, hitsound;
    double time, end_time;
    u32 slider, hs_len;
    static constexpr u32 kNoSlider = 0xFFFFFFFF;
};
constexpr u32 kNoSlider = HO::kNoSlider;
using fosu::detail::fast_parse_prefix;
using fosu::detail::scalar_parse_prefix;
using fosu::detail::comma_mask32;
using fosu::detail::nondigit_mask32;

// ---------------------------------------------------------------- parse state (the trailer)
struct __attribute__((packed)) TPRec {
    double time, beat_length;
    i32 meter, sample_set, sample_index, volume;
    u8 uninherited;
    u32 effects;
};
static_assert(sizeof(TPRec) == 37);
struct Break { double start, end; };

struct State : fosu::BeatmapHeader {
    u32 malformed_lines = 0, storyboard_lines = 0, fast_path_lines = 0, slow_path_lines = 0;
    u32 n_hitobjects = 0, n_sliders = 0, n_points = 0, n_orphans = 0;
    bool ar_specified = false;
    // timing point blocks (one per [TimingPoints] section; real files have one)
    struct Block { const char* p; u32 n; } tp_blocks[8] = {};
    u32 n_tp_blocks = 0;
    char* tp_out = nullptr;  // cursor inside the current block
    char* tp_end = nullptr;
};
constexpr u32 kBreaksInline = 16, kColoursInline = 8;  // keeps Ctx inside one stack page
struct Ctx {
    char* out_begin;  // start of the output stream area
    char* out;        // cursor
    char* out_end;    // end of the arena
    char* spill;      // lazily mapped overflow arrays and orphaned points
    State st;
    u32 n_breaks, n_colours;
    Break breaks[kBreaksInline];
    u32 colours[kColoursInline];
};
#define S (g->st)
// Overflow storage is mapped lazily. Ordinary corpus files use inline
// breaks/colours and have no orphaned points, so this costs no VMA or pages.
constexpr u32 kOrphanCap = 1u << 20;  // pool points left by failed slider lines
constexpr size_t kSpillBytes = (1u << 15) * sizeof(Break) + (1u << 12) * sizeof(u32) + kOrphanCap * 12;
inline char* overflow_storage() {
    if (!g->spill) {
        g->spill = static_cast<char*>(rt::mmap(nullptr, kSpillBytes, 3, 0x22));
        if (reinterpret_cast<uintptr_t>(g->spill) >= uintptr_t(-4095)) rt::exit(1);
    }
    return g->spill;
}
inline Break& break_at(u32 i) {
    return i < kBreaksInline ? g->breaks[i] : reinterpret_cast<Break*>(overflow_storage())[i - kBreaksInline];
}
inline u32& colour_at(u32 i) {
    return i < kColoursInline ? g->colours[i]
                              : reinterpret_cast<u32*>(overflow_storage() + (1u << 15) * sizeof(Break))[i - kColoursInline];
}

inline void put_raw(const void* p, size_t n) { memcpy(g_out, p, n); g_out += n; }
inline void put_u8(u8 v) { *g_out++ = static_cast<char>(v); }
inline void put_u32(u32 v) { memcpy(g_out, &v, 4); g_out += 4; }
inline void put_i32(i32 v) { memcpy(g_out, &v, 4); g_out += 4; }
inline void put_i64(i64 v) { memcpy(g_out, &v, 8); g_out += 8; }
inline void put_f64(double v) { memcpy(g_out, &v, 8); g_out += 8; }
inline void put_str(sv s) { put_u32(static_cast<u32>(s.n)); memcpy(g_out, s.p, s.n); g_out += s.n; }
inline void put_str(std::string_view s) { put_str(sv{s.data(), s.size()}); }

void flush() {
    const char* p = g_out_begin;
    while (p < g_out) {
        const long w = rt::write(1, p, static_cast<size_t>(g_out - p));
        if (w == -4) continue;  // EINTR
        if (w <= 0) rt::exit(3);
        p += w;
    }
    g_out = g_out_begin;
}
// Guarantees n contiguous bytes at g_out (a record is written after one
// ensure, so a flush never splits it).
inline void ensure(size_t n) {
    if (__builtin_expect(n > static_cast<size_t>(g_out_end - g_out), 0)) {
        flush();
        if (n > static_cast<size_t>(g_out_end - g_out)) {
            const size_t len = (n + (1u << 20) + 4095) & ~size_t(4095);
            g_out_begin = g_out = static_cast<char*>(rt::mmap(nullptr, len, 3, 0x22));
            if (reinterpret_cast<uintptr_t>(g_out_begin) >= uintptr_t(-4095)) rt::exit(1);
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
using fosu::detail::kGeneral;
using fosu::detail::kEditor;
using fosu::detail::kMetadata;
using fosu::detail::kDifficulty;
template <size_t N>
inline void parse_kv_line(const fosu::detail::KvEntry (&table)[N], const char* p, size_t len) {
    S.ar_specified |= fosu::detail::parse_kv_line<parse_double>(S, table, p, len, &S.malformed_lines);
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
        const char* q = fosu::detail::parse_osu_double(rest, end, start);
        if (q == rest || q >= end || *q != ',') { ++S.malformed_lines; return; }
        const char* r = fosu::detail::parse_osu_double(q + 1, end, stop);
        if (r == q + 1 || r != end) { ++S.malformed_lines; return; }
        if (g->n_breaks == kBreaksInline + (1u << 15)) rt::exit(6);
        break_at(g->n_breaks++) = {start, stop};
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
    if (g->n_colours == kColoursInline + (1u << 12)) rt::exit(6);
    colour_at(g->n_colours++) = rgb;
}

// ---------------------------------------------------------------- timing points
// The initial size is a reserve hint, not a bound: old two-field lines
// can be much shorter. Rare growth leaves the original arena block intact.
__attribute__((noinline)) void grow_timing_block() {
    auto& block = S.tp_blocks[S.n_tp_blocks - 1];
    const size_t used = size_t(block.n) * sizeof(TPRec);
    const size_t bytes = (used * 2 + sizeof(TPRec) + 4095) & ~size_t(4095);
    char* p = static_cast<char*>(rt::mmap(nullptr, bytes, 3, 0x22));
    if (reinterpret_cast<uintptr_t>(p) >= uintptr_t(-4095)) rt::exit(1);
    memcpy(p, block.p, used);
    block.p = p;
    S.tp_out = p + used;
    S.tp_end = p + bytes;
}
inline TPRec& tp_slot() {
    if (__builtin_expect(S.tp_out + sizeof(TPRec) > S.tp_end, 0)) grow_timing_block();
    return *reinterpret_cast<TPRec*>(S.tp_out);
}
inline void tp_commit() { S.tp_out += sizeof(TPRec); ++S.tp_blocks[S.n_tp_blocks - 1].n; }

void parse_timing_point_line(const char* p, size_t len) {
    TPRec& tp = tp_slot();
    if (fosu::detail::parse_timing_fields(p, p + len, tp)) tp_commit();
    else ++S.malformed_lines;
}

using fosu::detail::TpGeom;
using fosu::detail::TpShapeCache;
using fosu::detail::TpShapeRow;
using fosu::detail::tp_shape_match;
using fosu::detail::tp_shape_insert;
using fosu::detail::tp_shape_convert;
using fosu::detail::fast_parse_timing_point_masked;

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
    if (g_out - g_out_begin <= 64) {
        const size_t keep = static_cast<size_t>(g_out - g_out_begin);
        char tmp[64];
        memcpy(tmp, g_out_begin, keep);
        S.tp_blocks[S.n_tp_blocks++] = {g_out_begin, 0};
        S.tp_out = g_out_begin;
        S.tp_end = g_out_begin + bytes;
        g_out_begin += bytes;
        memcpy(g_out_begin, tmp, keep);
        g_out = g_out_begin + keep;
    } else {
        S.tp_blocks[S.n_tp_blocks++] = {g_out, 0};
        S.tp_out = g_out;
        S.tp_end = g_out + bytes;
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
        if (fosu::detail::ignored_line(p, p + len)) { p = next_line; continue; }
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
using fosu::detail::parse_coord;
using fosu::detail::parse_slider_length;
using fosu::detail::parse_slider_points;

struct __attribute__((packed)) Pt { i32 x, y; };

// The library leaves a failed slider line's points in its pool once the
// point loop has finished; they are part of the output (trailer).
inline void keep_orphans(const Pt* p, u32 n) {
    if (S.n_orphans + n > kOrphanCap) rt::exit(6);
    if (!n) return;
    char* dst = overflow_storage() + (1u << 15) * sizeof(Break) +
                (1u << 12) * sizeof(u32) + size_t(S.n_orphans) * 12;
    for (u32 i = 0; i < n; ++i) {
        const u32 index = S.n_points - n + i;
        memcpy(dst, &index, 4);
        memcpy(dst + 4, p + i, 8);
        dst += 12;
    }
    S.n_orphans += n;
}

// Slider block: u32 point_begin, point_count, points, i32 slides, f64 length,
// u8 curve_type, str edge_sounds, str edge_sets. Returns false to reject
// the line (the caller rewinds the whole record). `hs` receives the
// trailing hit_sample.
bool parse_slider_params(const char* p, const char* end, sv& hs) {
    if (p >= end) return false;
    const char curve_type = *p++;
    put_u32(S.n_points);
    char* const count_slot = g_out;
    g_out += 4;
    Pt* w = reinterpret_cast<Pt*>(g_out);
    Pt* const w0 = w;
    if (!parse_slider_points(p, end, w)) return false;
    const auto npts = static_cast<u32>(w - w0);
    memcpy(count_slot, &npts, 4);
    g_out = reinterpret_cast<char*>(w);
    // From here on the library leaves the points in its pool on failure.
    S.n_points += npts;
    if (p >= end || *p != ',') { keep_orphans(w0, npts); return false; }
    ++p;
    const u32 srun = digit_run8(p);
    i32 slides;
    if (srun - 1 <= 6) {
        slides = static_cast<i32>(swar_parse_u64(p, srun));
        p = fosu::detail::skip_numeric_space(p + srun, end);
    } else {
        i64 value;
        const char* next = fosu::detail::parse_osu_int(p, end, value);
        if (next == p) { keep_orphans(w0, npts); return false; }
        slides = clamp_i32(value);
        p = next;
    }
    if (slides > 9000 || (p < end && *p != ',')) { keep_orphans(w0, npts); return false; }
    double length = 0;
    if (p < end) {
        const char* q = parse_slider_length(p + 1, length);
        if (!q) q = fosu::detail::parse_osu_double(p + 1, end, length, 131072);
        if (q != p + 1) q = fosu::detail::skip_numeric_space(q, end);
        if (q == p + 1 || (q < end && *q != ',')) { keep_orphans(w0, npts); return false; }
        p = q;
    }
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
    if (!fosu::detail::valid_sample({hs.p, hs.n}, true) ||
        !fosu::detail::valid_edge_sets({edge_sets.p, edge_sets.n}, slides)) {
        keep_orphans(w0, npts);
        return false;
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
    if (!(h.type & (1 | 2 | 8 | 128))) return false;
    if (p < end && *p != ',') return false;
    if (!(h.type & 1) && (h.type & 2)) {
        if (p >= end || *p != ',') return false;
        sv hs;
        if (!parse_slider_params(p + 1, end, hs)) return false;
        h.slider = S.n_sliders++;
        h.hs_len = static_cast<u32>(hs.n);
        memcpy(g_out, hs.p, hs.n);
        g_out += hs.n;
        return true;
    }
    std::string_view sample;
    if (!fosu::detail::parse_object_tail(h, p, end, sample)) return false;
    h.hs_len = static_cast<u32>(sample.size());
    memcpy(g_out, sample.data(), sample.size());
    g_out += sample.size();
    return true;
}

struct StreamHits {
    using HitObject = HO;
    HO& begin(size_t len) {
        ensure(4 * len + 128);
        auto& h = *reinterpret_cast<HO*>(g_out);
        g_out += sizeof(HO);
        return h;
    }
    bool finish(HO& h, const char* p, const char* end, size_t) {
        return finish_hitobject(h, p, end);
    }
    void finish_circle_sample8(HO& h, const char* sample) {
        h.hs_len = 8;
        memcpy(g_out, sample, 8);
        g_out += 8;
    }
    void commit(HO&) { ++S.n_hitobjects; }
    void rollback(HO& h) { g_out = reinterpret_cast<char*>(&h); }
    State& stats() { return S; }
};
const char* parse_hitobjects_section(const char* p, const char* file_end) {
    StreamHits sink;
    return fosu::detail::parse_hitobject_lines(sink, p, file_end);
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
        if (fosu::detail::ignored_line(line, line_end)) continue;
        if (c == ' ' || c == '_') { ++storyboard_lines; continue; }
        const auto len = static_cast<size_t>(line_end - line);
        if (len >= 2 && c == '/' && line[1] == '/') continue;
        parse_event_line(line, len);
    }
    S.storyboard_lines += storyboard_lines;
    return p;
}

// ---------------------------------------------------------------- sections / main loop
using fosu::detail::Section;
inline Section match_section(const char* p, size_t len) {
    return fosu::detail::match_section({p, len});
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
        if (*p == '\r' || *p == '\n') { ++p; continue; }
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
        if (fosu::detail::ignored_line(p, line_end)) goto next_line;
        switch (sec) {
            case Section::None: {
                if (const char* vp = find_version_tag(p, len)) {
                    i64 ver;
                    if (parse_i64(vp, line_end, ver) != vp) S.format_version = clamp_i32(ver);
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
                        S.audio_filename.size() + S.sample_set.size() + S.overlay_position.size() + S.skin_preference.size() + S.bookmarks.size() +
                        S.title.size() + S.title_unicode.size() + S.artist.size() + S.artist_unicode.size() + S.creator.size() + S.version.size() +
                        S.source.size() + S.tags.size() + S.background.size() + S.video.size() + 4 + 16 * g->n_breaks + 4 + 4 * g->n_colours + 4 +
                        tp_bytes + 8 + 22 * 4 + 20 + size_t(S.n_orphans) * 12;
    ensure(need);
    const char* trailer_begin = g_out;
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
    for (u32 i = 0; i < g->n_breaks; ++i) { put_f64(break_at(i).start); put_f64(break_at(i).end); }
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
    if (S.n_orphans)
        put_raw(g->spill + (1u << 15) * sizeof(Break) + (1u << 12) * sizeof(u32), size_t(S.n_orphans) * 12);
    put_i64(g_out - trailer_begin);
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
    if (argc != 2) rt::exit(2);
#ifdef ABLATE_EXIT_ONLY
    rt::exit(0);
#endif
    const long fd = rt::open_ro(argv[1]);
    if (fd < 0) rt::exit(1);
    const long ssize = rt::fstat_size(static_cast<int>(fd));
    if (ssize < 0) rt::exit(1);
    const auto size = static_cast<size_t>(ssize);
    if (size > 64u * 1024u * 1024u) rt::exit(6);
    const size_t est = size + 128 + size + size / 4 + size / 8 + 8192;
    size_t offset = 64u << 10;
    while (offset < est && offset < (1u << 20)) offset <<= 1;
    size_t len = (2u << 20) - offset;
    if (est > len) len += ((est - len + (2u << 20) - 1) / (2u << 20)) * (2u << 20);
    constexpr int kFlags = 0x22 | 0x4000 /*PRIVATE|ANON|NORESERVE*/ | 0x100000 /*FIXED_NOREPLACE*/;
    char* arena = static_cast<char*>(rt::mmap(reinterpret_cast<void*>(kArenaBase + offset), len, 3 /*RW*/, kFlags));
    if (reinterpret_cast<long>(arena) < 0 && reinterpret_cast<long>(arena) > -4096)
        arena = static_cast<char*>(rt::mmap(reinterpret_cast<void*>(kArenaBaseFar + offset), len, 3, kFlags));
    if (reinterpret_cast<long>(arena) < 0 && reinterpret_cast<long>(arena) > -4096) rt::exit(1);
#ifndef ABLATE_NO_MADVISE
    rt::madvise(arena, len, 14 /*MADV_HUGEPAGE*/);
#endif
    ctx.spill = nullptr;
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
    g_out_end = arena + len;
#else
    size_t got = 0;
    while (got < size) {
        const long r = rt::read(static_cast<int>(fd), arena + got, size - got);
        if (r == -4) continue;  // EINTR
        if (r <= 0) rt::exit(1);
        got += static_cast<size_t>(r);
    }
    // 128 zero bytes of padding follow the input; the output area starts after them.
    g_out_begin = g_out = arena + ((got + 128 + 63) & ~size_t(63));
    g_out_end = arena + len;
#endif
#ifdef ABLATE_AFTER_READ
    rt::exit(0);
#endif
    put_raw("FOSUDMP5", 8);
    parse(arena, got);
    emit_trailer();
#ifdef ABLATE_NO_WRITE
    rt::exit(0);
#endif
    flush();
    rt::exit(0);
}

}  // namespace

#ifndef FOSU_ONESHOT_HOSTED
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
// Hosted variant for experiments. GCC profiles from this runtime are not
// interchangeable with the freestanding build.
#include <cstdlib>
namespace rt { [[noreturn]] void exit(int code) { ::exit(code); } }
int main(int argc, char** argv) { run(argc, argv); }
#endif
