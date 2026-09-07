#pragma once

#include <array>
#include <cstddef>

#include "prefix.hpp"

namespace fosu::detail {

// Slider control point coordinate: overwhelmingly 1-4 plain digits, parsed
// branchlessly via SWAR. Signs, 5+ digit values, and empty fields take the
// general path. Returns the advanced pointer, or `p` unchanged on failure.
inline const char* parse_coord(const char* p, const char* end, int32_t& out) {
    const uint32_t run = digit_run8(p);
    if (run - 1 <= 3 && run <= static_cast<size_t>(end - p) &&
        (p[run] == ':' || p[run] == '|' || p[run] == ',')) {
        out = static_cast<int32_t>(swar_parse_u32(p, run));
        return p + run;
    }
    float v;
    const char* q = parse_osu_float(p, end, v, 131072);
    if (q == p) return p;
    out = static_cast<int32_t>(v);
    return q;
}

#if FOSU_SIMD_X86
// Slider length on the editor-emitted shape: up to 8 integer digits, an
// optional '.', up to 13 fraction digits, at most 18 digits in all. One
// 32-byte load classifies the whole number; the mantissa is assembled from
// the same SWAR pieces parse_double accumulates and divided by the same
// power of ten, so the result is bit-identical. Returns nullptr for any
// other shape (sign, exponent, longer or empty numbers) so the caller can
// run parse_double. The line terminator and buffer padding are non-digits,
// so the digit run can never cross the end of the line.
inline const char* parse_slider_length(const char* p, double& out, const HitConsts& k) {
    const __m256i v =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    const uint64_t nd = nondigit_mask32(v, k.bias, k.thr);
    const auto il = static_cast<uint32_t>(_tzcnt_u64(nd));  // 1..8 if valid
    if (il - 1 > 7) return nullptr;
    // Integer and fractional lengths alternate within a map, so the dot and
    // fraction handling is computed unconditionally: a zero-length fraction
    // contributes nothing and dividing by 10^0 is exact.
    const bool has_dot = p[il] == '.';  // il <= 32 stays inside the padding
    // Integer lengths skip the fraction and the division: a branchless form
    // with an unconditional divide measured about 7% slower on the corpus
    // when the input is cache-resident.
    const uint32_t fl =
        has_dot ? static_cast<uint32_t>(_tzcnt_u64(nd >> (il + 1))) : 0;
    if (fl > 13 || il + fl > 18) return nullptr;
    const uint32_t fl1 = fl <= 8 ? fl : 8;
    const uint32_t fl2 = fl - fl1;
    uint64_t mant = swar_parse_u64(p, il);
    const char* fp = p + il + 1;
    if (fl1) mant = mant * kPow10u[fl1] + swar_parse_u64(fp, fl1);
    if (fl2) mant = mant * kPow10u[fl2] + swar_parse_u64(fp + 8, fl2);
    if (mant > kMaxExactDoubleInteger) return nullptr;
    double d = static_cast<double>(mant);
    if (fl) d /= kPow10[fl];
    out = d;
    const char* q = has_dot ? fp + fl : p + il;
    return *q == 'e' || *q == 'E' ? nullptr : q;
}
inline const char* parse_slider_length(const char* p, double& out) {
    const HitConsts k;
    return parse_slider_length(p, out, k);
}

// One pshufb per control point right-aligns both coordinates' digits into
// two 4-byte groups (0x80 lanes read as zero); maddubs/madd then convert the
// pair together and store it as one 8-byte point. The source vector starts
// at the '|' that opens the point, so x digits sit at bytes 1..len_x and y
// digits follow the colon. Indexed by (len_x - 1, len_y - 1).
struct alignas(16) PointShuf {
    int8_t b[16];
};
consteval std::array<PointShuf, 16> make_point_shuf() {
    std::array<PointShuf, 16> out{};
    for (int xl = 1; xl <= 4; ++xl)
        for (int yl = 1; yl <= 4; ++yl) {
            PointShuf& m = out[(xl - 1) * 4 + (yl - 1)];
            for (auto& b : m.b) b = static_cast<int8_t>(0x80);
            for (int i = 0; i < xl; ++i) m.b[4 - xl + i] = static_cast<int8_t>(1 + i);
            for (int i = 0; i < yl; ++i) m.b[8 - yl + i] = static_cast<int8_t>(xl + 2 + i);
        }
    return out;
}
inline constexpr auto kPointShuf = make_point_shuf();

// `src` starts at a '|'; xl/yl are the digit counts (masked into range, so a
// speculative call on an invalid shape reads a valid table entry).
inline __m128i convert_point(__m128i src, uint32_t xl, uint32_t yl, const HitConsts& k) {
    const __m128i shuf = _mm_load_si128(reinterpret_cast<const __m128i*>(
        kPointShuf[((xl - 1) & 3) * 4 + ((yl - 1) & 3)].b));
    const __m128i placed =
        _mm_shuffle_epi8(_mm_sub_epi8(src, _mm256_castsi256_si128(k.zero)), shuf);
    return _mm_madd_epi16(_mm_maddubs_epi16(placed, k.pair_weights), k.word_weights);
}

// Up to two editor-shaped points ("|x:y", 1..4 digits each) from one 32-byte
// window, with no data-dependent loop exit: both are converted speculatively
// and the second is kept only when it is present and well-formed. Most
// sliders have one or two points, so this replaces a mispredicted loop exit
// per slider. Returns the count written (0: the first point is not
// editor-shaped, leaving `p` and `w` for the general loop) and moves `p` to
// the byte after the last written point. The caller guarantees room for two
// points at `w`.
template <typename Point>
inline uint32_t parse_point_pair(const char*& p, Point* w, const HitConsts& k) {
    const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    const uint32_t nd = nondigit_mask32(v, k.bias, k.thr);
    const auto colon = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, k.colon)));
    const auto pipe = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, k.pipe)));
    const auto comma = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, k.comma)));
    const uint32_t sep = pipe | comma;
    // Lengths are masked to 7 before feeding further shifts so every shift
    // count stays below the operand width on any byte pattern.
    const uint32_t xl1 = _tzcnt_u32(nd >> 1);
    const uint32_t c1 = xl1 & 7;
    const uint32_t yl1 = _tzcnt_u32(nd >> (2 + c1));
    const uint32_t d1 = yl1 & 7;
    const uint32_t end1 = 2 + c1 + d1;  // <= 16
    const bool ok1 = (pipe & 1) & (((xl1 - 1) | (yl1 - 1)) <= 3) &
                     ((colon >> (1 + c1)) & 1) & ((sep >> end1) & 1);
    const uint32_t xl2 = _tzcnt_u32(nd >> (end1 + 1));
    const uint32_t c2 = xl2 & 7;
    const uint32_t yl2 = _tzcnt_u32(nd >> (end1 + 2 + c2));
    const uint32_t d2 = yl2 & 7;
    const uint32_t end2 = end1 + 2 + c2 + d2;  // <= 32
    const bool ok2 = ((pipe >> end1) & 1) & (((xl2 - 1) | (yl2 - 1)) <= 3) &
                     ((colon >> (end1 + 1 + c2)) & 1) &
                     ((static_cast<uint64_t>(sep) >> end2) & 1);
    if (!ok1) return 0;
    static_assert(sizeof(Point) == 8 && offsetof(Point, x) == 0 && offsetof(Point, y) == 4);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(w),
                     convert_point(_mm256_castsi256_si128(v), c1, d1, k));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(w + 1),
                     convert_point(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p + end1)),
                                   c2, d2, k));
    p += ok2 ? end2 : end1;
    return 1 + ok2;
}
#endif

// Parses the "|x:y|x:y..." control points at `p` into `w`, leaving `p` on
// the byte after the last point (normally ','). `w` must have room for one
// point per four remaining line bytes plus two.
template <typename Point>
inline bool parse_slider_points(const char*& p, const char* end, Point*& w,
                                [[maybe_unused]] const HitConsts& k) {
#if FOSU_SIMD_X86
    w += parse_point_pair(p, w, k);
    // Third and later points (long Bezier sliders): one 16-byte load per
    // point classifies a whole pair. Anything else (signs, longer values,
    // empty fields) leaves the loop for the general one.
    while (*p == '|') {  // the byte at `end` is a line terminator, never '|'
        const __m128i v =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        const __m128i biased = _mm_add_epi8(v, _mm256_castsi256_si128(k.bias));
        const auto nd = static_cast<uint32_t>(_mm_movemask_epi8(
            _mm_cmpgt_epi8(biased, _mm256_castsi256_si128(k.thr))));
        const auto colon = static_cast<uint32_t>(_mm_movemask_epi8(
            _mm_cmpeq_epi8(v, _mm256_castsi256_si128(k.colon))));
        const uint32_t xl = _tzcnt_u32(nd >> 1);       // 31 when all digits
        const uint32_t c = xl & 7;
        const uint32_t yl = _tzcnt_u32(nd >> (2 + c));  // shift <= 9
        if (((xl - 1) | (yl - 1)) > 3 || !((colon >> (1 + c)) & 1)) break;
        const char after_y = p[2 + xl + yl];
        if (after_y != '|' && after_y != ',') break;
        _mm_storel_epi64(reinterpret_cast<__m128i*>(w), convert_point(v, xl, yl, k));
        ++w;
        p += 2 + xl + yl;
    }
#endif
    while (p < end && *p == '|') {
        int32_t px = 0, py = 0;  // keeps GCC PGO definite-assignment analysis quiet
        const char* q = parse_coord(p + 1, end, px);
        // A coord ending at the line end reads the terminator from the
        // padded buffer, never ':' — no explicit q < end check needed.
        if (q == p + 1 || *q != ':') {
            return false;
        }
        const char* r = parse_coord(q + 1, end, py);
        if (r == q + 1) {
            return false;
        }
        w->x = px;
        w->y = py;
        ++w;
        p = r;
    }
    return true;
}

}  // namespace fosu::detail
