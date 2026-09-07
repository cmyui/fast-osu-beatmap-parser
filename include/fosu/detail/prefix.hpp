#pragma once

// Parsers for the hitobject line prefix "x,y,time,type,hitSound".
//
// The AVX2 path is based on a prototype by Flamme (fla.me): classify
// delimiters with one vector compare, derive a (len_x, len_y, len_time,
// len_type) signature, and use it to index precomputed permute+shuffle
// masks that normalize every digit into a fixed position, so a single
// maddubs/madd chain converts the whole prefix at once. This version adds:
//   - parallel delimiter extraction (blsr chain + independent tzcnts)
//     instead of a serialized tzcnt/shift chain
//   - the table index computed directly from delimiter positions:
//     ((x0*3+y0)*10+t0)*3+ty0  ==  60*p0 + 27*p1 + 2*p2 + p3 - 158
//   - permute and shuffle masks fused into one 64-byte (one cache line)
//     table entry, generated at compile time
//   - structural validation with scalar fallback instead of assuming
//     well-formed input
//   - 1-2 digit hitSound support (osu! hitsound bitflags go up to 15)
//
// Callers must guarantee kBufferPadding readable bytes past the end of
// the buffer (see io.hpp); all paths rely on it to load past short lines
// and speculate past field boundaries safely.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../scalar_parse.hpp"

#if defined(__AVX2__) && defined(__BMI__)
#define FOSU_SIMD_X86 1
#include <immintrin.h>
#else
#define FOSU_SIMD_X86 0
#endif

namespace fosu::detail {

#if !FOSU_SIMD_X86
struct HitConsts {};  // the scalar path has no vector constants
#endif

inline constexpr uint32_t kNPrefixVariants = 3 * 3 * 10 * 3;

// Lenient reference implementation: tolerates negative values, decimal
// coordinates (truncated), and values of any length. Returns the offset of
// the first byte after the hitSound field (a ',' or the line end), or -1.
template <typename H>
inline int scalar_parse_prefix(const char* line, size_t len, H& h) {
    const char* p = line;
    const char* end = line + len;
    float coord[2];
    for (int i = 0; i < 2; ++i) {
        const char* q = parse_osu_float(p, end, coord[i], 131072);
        if (q == p || q >= end || *q != ',') return -1;
        p = q + 1;
    }
    double time;
    const char* q = parse_osu_double(p, end, time);
    if (q == p || q >= end || *q != ',') return -1;
    p = q + 1;
    int64_t type, sound;
    q = parse_osu_int(p, end, type);
    if (q == p || q >= end || *q != ',') return -1;
    p = q + 1;
    q = parse_osu_int(p, end, sound);
    if (q == p || (q < end && *q != ',')) return -1;
    p = q;
    h.x = static_cast<int32_t>(coord[0]);
    h.y = static_cast<int32_t>(coord[1]);
    h.time = time;
    h.type = static_cast<uint32_t>(type);
    h.hitsound = static_cast<uint32_t>(sound);
    return static_cast<int>(p - line);
}

#if FOSU_SIMD_X86

// One entry per (len_x, len_y, len_time, len_type) combination. `perm`
// feeds vpermd to move each field's dwords into the lane that needs them;
// `shuf` then places digits at fixed offsets (0x80 lanes produce zero):
//   bytes  0-3   x   right-aligned  -> dword 0 after madd
//   bytes  4-7   y                  -> dword 1
//   bytes  8-11  type               -> dword 2
//   bytes 12-15  (zero; hitSound is parsed scalar since its length is
//                 not part of the table index)
//   bytes 16-31  time right-aligned -> dwords 5,6,7 = top2/mid4/low4 digits
struct alignas(64) LaneMasks {
    int32_t perm[8];
    int8_t shuf[32];
};
static_assert(sizeof(LaneMasks) == 64);

consteval std::array<LaneMasks, kNPrefixVariants> make_lane_masks() {
    std::array<LaneMasks, kNPrefixVariants> out{};
    for (int lx = 1; lx <= 3; ++lx)
    for (int ly = 1; ly <= 3; ++ly)
    for (int lt = 1; lt <= 10; ++lt)
    for (int lty = 1; lty <= 3; ++lty) {
        const int p0 = lx;
        const int p1 = p0 + 1 + ly;
        const int p2 = p1 + 1 + lt;
        const int p3 = p2 + 1 + lty;
        const int index = (((lx - 1) * 3 + (ly - 1)) * 10 + (lt - 1)) * 3 + (lty - 1);

        int src[32];
        for (auto& s : src) s = -1;
        for (int i = 0; i < lx; ++i) src[4 - lx + i] = i;
        for (int i = 0; i < ly; ++i) src[8 - ly + i] = p0 + 1 + i;
        for (int i = 0; i < lty; ++i) src[12 - lty + i] = p2 + 1 + i;
        for (int i = 0; i < lt; ++i) src[32 - lt + i] = p1 + 1 + i;

        LaneMasks& lm = out[index];
        // Low lane: x,y always live in source dwords 0-1; type spans at
        // most two more. High lane: time spans at most four dwords.
        lm.perm[0] = 0;
        lm.perm[1] = 1;
        lm.perm[2] = (p2 + 1) / 4;
        lm.perm[3] = (p3 - 1) / 4;
        const int td0 = (p1 + 1) / 4;
        const int td1 = (p2 - 1) / 4;
        for (int k = 0; k < 4; ++k)
            lm.perm[4 + k] = (td0 + k <= td1) ? td0 + k : td1;

        for (int b = 0; b < 32; ++b) {
            if (src[b] < 0) {
                lm.shuf[b] = static_cast<int8_t>(0x80);
                continue;
            }
            const int dw = src[b] / 4;
            const int off = src[b] % 4;
            const int lane_base = (b < 16) ? 0 : 4;
            int slot = -1;
            for (int j = lane_base; j < lane_base + 4; ++j) {
                if (lm.perm[j] == dw) {
                    slot = j;
                    break;
                }
            }
            if (slot < 0) __builtin_abort();
            lm.shuf[b] = static_cast<int8_t>((slot - lane_base) * 4 + off);
        }
    }
    return out;
}

inline constexpr auto kLaneMasks = make_lane_masks();

// Constant vectors materialized by one instruction from a static byte. The
// compiler cannot see the value, so it keeps or reloads the register instead
// of rebuilding a broadcast from a general register on every line (which GCC
// otherwise does whenever a loop body contains a call).
inline __m256i bcast256(const char& k) {
    __m256i v;
    __asm__("vpbroadcastb %1, %0" : "=x"(v) : "m"(k));
    return v;
}
inline __m128i bcast128(const char& k) {
    __m128i v;
    __asm__("vpbroadcastb %1, %0" : "=x"(v) : "m"(k));
    return v;
}
inline constexpr char kByteNewline = '\n', kByteComma = ',', kByteColon = ':',
                      kBytePipe = '|', kByteZero = '0', kByteBias = 80,
                      kByteThreshold = -119;

// Vector constants for one [HitObjects] section, built once and passed by
// reference so callees use them as memory operands instead of rebuilding
// them per call.
struct HitConsts {
    __m256i nl, comma, colon, pipe, bias, thr, zero;
    __m128i pair_weights, word_weights;  // slider point digit weights
    HitConsts()
        : nl(bcast256(kByteNewline)), comma(bcast256(kByteComma)),
          colon(bcast256(kByteColon)), pipe(bcast256(kBytePipe)),
          bias(bcast256(kByteBias)), thr(bcast256(kByteThreshold)),
          zero(bcast256(kByteZero)),
          pair_weights(_mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0)),
          word_weights(_mm_setr_epi16(100, 1, 100, 1, 0, 0, 0, 0)) {}
};

// Non-digit classification against caller-provided constants (see
// nondigit_mask32 for the bias trick).
inline uint32_t nondigit_mask32(__m256i ascii, __m256i bias, __m256i threshold) {
    return static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_cmpgt_epi8(_mm256_add_epi8(ascii, bias), threshold)));
}

inline uint32_t comma_mask32(__m256i ascii) {
    return static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_cmpeq_epi8(ascii, _mm256_set1_epi8(','))));
}

// No unsigned byte compare in AVX2: bias so '0'..'9' map to [-128, -119],
// making every non-digit byte compare greater.
inline uint32_t nondigit_mask32(__m256i ascii) {
    const __m256i biased = _mm256_add_epi8(ascii, _mm256_set1_epi8(80));
    const __m256i delims = _mm256_cmpgt_epi8(biased, _mm256_set1_epi8(-119));
    return static_cast<uint32_t>(_mm256_movemask_epi8(delims));
}

// Delimiter geometry of one hitobject prefix inside a 32-byte window, derived
// from the non-digit and comma masks alone. The four field lengths are packed
// into 16-bit lanes so one subtraction, one addition and one AND validate
// every length bound at once, and one multiply reduces the delimiter
// positions to the table index. `p4` is the first non-digit after hitSound
// (32 when the window holds none); the caller decides whether that byte ends
// the prefix. `index` is meaningful only when `ok`.
struct PrefixShape {
    uint32_t index;
    uint32_t time_span;  // p2 - p1: time digits + 1
    uint32_t p3, p4;
    bool ok;
};

inline PrefixShape classify_prefix(uint32_t nondig, uint32_t commas) {
    const uint32_t m1 = _blsr_u32(nondig);
    const uint32_t m2 = _blsr_u32(m1);
    const uint32_t m3 = _blsr_u32(m2);
    const uint32_t m4 = _blsr_u32(m3);
    const uint64_t p0 = _tzcnt_u32(nondig);
    const uint64_t p1 = _tzcnt_u32(m1);
    const uint64_t p2 = _tzcnt_u32(m2);
    const uint64_t p3 = _tzcnt_u32(m3);
    const uint32_t p4 = _tzcnt_u32(m4);
    // Lanes (low to high): p0, p1, p2, p3 — each at most 32.
    const uint64_t pk = p0 | p1 << 16 | p2 << 32 | p3 << 48;
    // Lanes: len_x, len_y, len_time, len_type. A lane below zero borrows from
    // the next one, but such a lane fails the sign test itself, and a borrow
    // can only shrink a neighbour, never rescue an invalid line.
    const uint64_t lens = (pk - (pk << 16)) - 0x0002000200020001ull;
    // Bounds 2, 2, 9, 2: adding 0x7FFF - bound sets bit 15 exactly when a
    // non-negative lane exceeds its bound.
    const uint64_t over = lens + 0x7FFD7FF67FFD7FFDull;
    const bool lens_ok = ((lens | over) & 0x8000800080008000ull) == 0;
    // The first four non-digits must be literal commas.
    const uint32_t through_p3 = static_cast<uint32_t>((2ull << p3) - 1);
    const bool commas_ok = ((nondig ^ commas) & through_p3) == 0;
    // hitSound: one or two digits.
    const uint32_t hl = p4 - static_cast<uint32_t>(p3) - 1;
    const bool hs_ok = hl - 1 <= 1;
    PrefixShape s;
    // 60*p0 + 27*p1 + 2*p2 + p3 lands in the top lane; lower lanes cannot carry.
    s.index = static_cast<uint32_t>((pk * 0x003C001B00020001ull) >> 48) - 158;
    s.time_span = static_cast<uint32_t>(p2 - p1);
    s.p3 = static_cast<uint32_t>(p3);
    s.p4 = p4;
    s.ok = lens_ok & commas_ok & hs_ok;
    return s;
}

// Converts a classified prefix: writes x, y, type, hitsound, time, end_time
// (0) and slider (kNoSlider); `type` receives the object type for dispatch.
// Returns false only for 9-10 digit timestamps above INT32_MAX, which the
// scalar parser then rejects.
template <typename H>
__attribute__((always_inline))
inline bool convert_prefix(__m256i ascii, __m256i zero, const PrefixShape& sh,
                           const char* line, H& h, uint32_t& type) {
    static_assert(offsetof(H, x) == 0 && offsetof(H, y) == 4 &&
                      offsetof(H, type) == 8 && offsetof(H, hitsound) == 12 &&
                      offsetof(H, time) == 16 && offsetof(H, end_time) == 24,
                  "the fast path stores numeric fields directly");
    const __m256i digits = _mm256_sub_epi8(ascii, zero);
    const LaneMasks& lm = kLaneMasks[sh.index];
    const __m256i perm =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.perm));
    const __m256i shuf =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.shuf));
    const __m256i placed =
        _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(digits, perm), shuf);
    const __m256i pair_weights = _mm256_setr_epi8(
        0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10, 1, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
    const __m256i word_weights = _mm256_setr_epi16(
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1);
    const __m256i words = _mm256_maddubs_epi16(placed, pair_weights);
    const __m256i dwords = _mm256_madd_epi16(words, word_weights);
    const __m128i lo = _mm256_castsi256_si128(dwords);
    // hitSound: the second byte is only a digit when the field has two.
    const uint32_t d0 = static_cast<uint8_t>(line[sh.p3 + 1] - '0');
    const uint32_t d1 = static_cast<uint8_t>(line[sh.p3 + 2] - '0');
    const uint32_t two = sh.p4 - sh.p3 - 2;  // 0 or 1 once validated
    const uint32_t hs = d0 + two * (d0 * 9 + d1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&h),
                     _mm_insert_epi32(lo, static_cast<int>(hs), 3));
    type = static_cast<uint32_t>(_mm_extract_epi32(lo, 2));
    const __m128i thi = _mm256_extracti128_si256(dwords, 1);
    if (sh.time_span <= 9) [[likely]] {
        // Up to eight time digits fit in signed int32. Combine and widen in
        // SIMD registers, then store {time, 0.0} without scalar extraction.
        const __m128i packed = _mm_packus_epi32(thi, thi);
        const __m128i combined = _mm_madd_epi16(
            packed, _mm_setr_epi16(0, 0, 10000, 1, 0, 0, 0, 0));
        const __m128i pair = _mm_shuffle_epi32(combined, _MM_SHUFFLE(0, 0, 0, 1));
        _mm_storeu_pd(reinterpret_cast<double*>(reinterpret_cast<char*>(&h) + 16),
                      _mm_cvtepi32_pd(pair));
    } else {
        const uint64_t t =
            static_cast<uint32_t>(_mm_extract_epi32(thi, 1)) * 100000000ull +
            static_cast<uint32_t>(_mm_extract_epi32(thi, 2)) * 10000ull +
            static_cast<uint32_t>(_mm_extract_epi32(thi, 3));
        if (t > INT32_MAX) return false;
        h.time = static_cast<double>(t);
        h.end_time = 0;
    }
    h.slider = H::kNoSlider;
    return true;
}

// Returns the offset of the first byte after hitSound, or -1 to request the
// scalar fallback (structurally unusual line: signs, decimals, empty or
// over-long fields, missing delimiters). The byte after hitSound must be a
// comma or end the line ('\0' covers the padded end of file); a '\r' must be
// followed by '\n' or '\0'.
//
// On success this writes x, y, type, hitsound, time, end_time (0), and
// slider (kNoSlider); on failure the caller owns re-initializing the
// fields before running the scalar fallback.
template <typename H>
inline int fast_parse_prefix(__m256i ascii, const char* line, H& h) {
    const PrefixShape sh = classify_prefix(nondigit_mask32(ascii), comma_mask32(ascii));
    if (!sh.ok) return -1;
    const char after = line[sh.p4];
    if (!(after == ',' || after == '\n' || after == '\0' ||
          (after == '\r' && (line[sh.p4 + 1] == '\n' || line[sh.p4 + 1] == '\0'))))
        return -1;
    uint32_t type;
    if (!convert_prefix(ascii, _mm256_set1_epi8('0'), sh, line, h, type)) return -1;
    return static_cast<int>(sh.p4);
}

// Public line-oriented entry point also returns the first newline mask.
template <typename H>
inline int fast_parse_prefix(const char* line, H& h, uint32_t& nl_mask) {
    const __m256i ascii = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(line));
    nl_mask = static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_cmpeq_epi8(ascii, _mm256_set1_epi8('\n'))));
    return fast_parse_prefix(ascii, line, h);
}

#endif  // FOSU_SIMD_X86

}  // namespace fosu::detail
