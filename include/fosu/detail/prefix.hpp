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

// Returns the offset of the first byte after hitSound, or -1 to request the
// scalar fallback (structurally unusual line: signs, decimals, empty or
// over-long fields, missing delimiters). `nl_mask` receives the positions
// of any '\n' inside the same 32-byte window — most circle lines fit
// entirely in it, so the caller usually gets the line end for free.
//
// On success this writes x, y, type, hitsound, time, end_time (0), and
// slider (kNoSlider); on failure the caller owns re-initializing the
// fields before running the scalar fallback.
template <typename H>
inline int fast_parse_prefix(__m256i ascii, const char* line, H& h) {
    const __m256i digits = _mm256_sub_epi8(ascii, _mm256_set1_epi8('0'));

    const uint32_t mask = nondigit_mask32(ascii);

    const uint32_t m1 = _blsr_u32(mask);
    const uint32_t m2 = _blsr_u32(m1);
    const uint32_t m3 = _blsr_u32(m2);
    const uint32_t p0 = _tzcnt_u32(mask);
    const uint32_t p1 = _tzcnt_u32(m1);
    const uint32_t p2 = _tzcnt_u32(m2);
    const uint32_t p3 = _tzcnt_u32(m3);

    const uint32_t index = 60 * p0 + 27 * p1 + 2 * p2 + p3 - 158;

    // The index is a mixed-radix encoding, so it only identifies the field
    // lengths when every length is within its base — an oversized field can
    // carry into the next radix digit and alias a valid index. Bound each
    // length explicitly (unsigned compare also catches empty fields, which
    // wrap negative); in-range lengths guarantee index < kNPrefixVariants.
    // The delimiters must also be actual commas, not just any non-digit.
    if ((p0 - 1) > 2 || (p1 - p0 - 2) > 2 || (p2 - p1 - 2) > 9 ||
        (p3 - p2 - 2) > 2)
        return -1;
    if (!(line[p0] == ',' && line[p1] == ',' && line[p2] == ',' &&
          line[p3] == ','))
        return -1;

    const LaneMasks& lm = kLaneMasks[index];
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

    static_assert(offsetof(H, x) == 0 && offsetof(H, y) == 4 &&
                      offsetof(H, type) == 8 && offsetof(H, hitsound) == 12 &&
                      offsetof(H, time) == 16 && offsetof(H, end_time) == 24,
                  "the fast path stores numeric fields directly");
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&h), _mm256_castsi256_si128(dwords));
    const __m128i thi = _mm256_extracti128_si256(dwords, 1);
    if (p2 - p1 <= 9) [[likely]] {
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
        if (t > INT32_MAX) return -1;
        h.time = static_cast<double>(t);
        h.end_time = 0;
    }
    h.slider = H::kNoSlider;

    const auto d1 = static_cast<uint8_t>(line[p3 + 1] - '0');
    if (d1 > 9) return -1;
    uint32_t hs = d1;
    uint32_t next = p3 + 2;
    const auto d2 = static_cast<uint8_t>(line[next] - '0');
    if (d2 <= 9) {
        hs = hs * 10 + d2;
        ++next;
    }
    // Match the scalar path's rule: hitSound must be followed by ',' or
    // the end of the line ('\0' covers the padded end of file).
    const char after = line[next];
    if (!(after == ',' || after == '\r' || after == '\n' || after == '\0'))
        return -1;
    if (after == '\r' && line[next + 1] != '\n' && line[next + 1] != '\0') return -1;
    h.hitsound = hs;
    return static_cast<int>(next);
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
