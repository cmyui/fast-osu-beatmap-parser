#pragma once

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
inline const char* parse_slider_length(const char* p, double& out) {
    const __m256i v =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    const uint64_t nd = nondigit_mask32(v);
    const auto il = static_cast<uint32_t>(_tzcnt_u64(nd));  // 1..8 if valid
    if (il - 1 > 7) return nullptr;
    const bool has_dot = p[il] == '.';  // il <= 32 stays inside the padding
    const uint32_t fl =
        has_dot ? static_cast<uint32_t>(_tzcnt_u64(nd >> (il + 1))) : 0;
    if ((il - 1) > 7 || fl > 13 || il + fl > 18) return nullptr;
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
#endif

template <typename Point>
inline bool parse_slider_points(const char*& p, const char* end, Point*& w) {
#if FOSU_SIMD_X86
    // Editor-emitted points are "|x:y" with 1..4 plain digits per
    // coordinate, so one 16-byte load classifies a whole pair: the
    // non-digit mask yields both digit counts, the ':' mask validates the
    // separator without a dependent byte load. Anything else (signs,
    // longer values, empty fields) leaves the loop for the general one.
    while (p < end && *p == '|') {
        const __m128i v =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 1));
        const __m128i biased = _mm_add_epi8(v, _mm_set1_epi8(80));
        const auto nd = static_cast<uint32_t>(_mm_movemask_epi8(
            _mm_cmpgt_epi8(biased, _mm_set1_epi8(-119))));
        const auto colon = static_cast<uint32_t>(_mm_movemask_epi8(
            _mm_cmpeq_epi8(v, _mm_set1_epi8(':'))));
        const uint32_t xl = _tzcnt_u32(nd);
        if (xl - 1 > 3) break;
        const uint32_t yl = _tzcnt_u32(nd >> (xl + 1));
        if ((xl - 1) > 3 || (yl - 1) > 3 || !((colon >> xl) & 1)) break;
        const char after_y = p[2 + xl + yl];
        if (after_y != '|' && after_y != ',') break;
        w->x = static_cast<int32_t>(swar_parse_u32(p + 1, xl));
        w->y = static_cast<int32_t>(swar_parse_u32(p + 2 + xl, yl));
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
