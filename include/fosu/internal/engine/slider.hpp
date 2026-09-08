#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <optional>

#include "prefix.hpp"

namespace fosu::internal {

struct ParsedSliderCoordinate {
    int32_t value;
    const char* next;
};

template <typename Point>
struct ParsedSliderPoint {
    Point value;
    const char* next;
};

// Slider control point coordinate: overwhelmingly 1-4 plain digits, parsed
// branchlessly via SWAR. Signs, 5+ digit values, and empty fields take the
// general path.
inline std::optional<ParsedSliderCoordinate> parse_slider_coordinate(
    const char* p, const char* end) {
    const uint32_t run = digit_run8(p);
    if (run - 1 <= 3 && run <= static_cast<size_t>(end - p) &&
        (p[run] == ':' || p[run] == '|' || p[run] == ',')) {
        return ParsedSliderCoordinate{
            static_cast<int32_t>(swar_parse_u32(p, run)), p + run};
    }
    float v;
    const char* q = parse_osu_float(p, end, v, 131072);
    if (q == p) return std::nullopt;
    return ParsedSliderCoordinate{static_cast<int32_t>(v), q};
}

#if FOSU_SIMD
// Slider length on the editor-emitted shape: up to 8 integer digits, an
// optional '.', up to 13 fraction digits, at most 18 digits in all. One
// 32-byte load classifies the whole number; the mantissa is assembled from
// the same SWAR pieces parse_double accumulates and divided by the same
// power of ten, so the result is bit-identical. Returns nullptr for any
// other shape (sign, exponent, longer or empty numbers) so the caller can
// run parse_double. Values above the official length bound also defer.
// The line terminator and buffer padding are non-digits,
// so the digit run can never cross the end of the line.
struct ParsedSliderLength {
    double value;
    const char* next;
};

inline std::optional<ParsedSliderLength> try_parse_slider_length_fast(
    const char* p, const HitObjectParseConstants& k) {
    const Bytes32 v = load32(p);
    const uint64_t nd = nondigit_mask32(v, k.bias, k.thr);
    const auto il = static_cast<uint32_t>(trailing_zeros(nd));  // 1..8 if valid
    if (il - 1 > 7) return std::nullopt;
    // Integer and fractional lengths alternate within a map, so the dot and
    // fraction handling is computed unconditionally: a zero-length fraction
    // contributes nothing and dividing by 10^0 is exact.
    const bool has_dot = p[il] == '.';  // il <= 32 stays inside the padding
    // Integer lengths skip the fraction and the division: a branchless form
    // with an unconditional divide measured about 7% slower on the corpus
    // when the input is cache-resident.
    const uint32_t fl =
        has_dot ? static_cast<uint32_t>(trailing_zeros(nd >> (il + 1))) : 0;
    if (fl > 13 || il + fl > 18) return std::nullopt;
    const uint32_t fl1 = fl <= 8 ? fl : 8;
    const uint32_t fl2 = fl - fl1;
    uint64_t mant = swar_parse_u64(p, il);
    const char* fp = p + il + 1;
    if (fl1) mant = mant * kPow10u[fl1] + swar_parse_u64(fp, fl1);
    if (fl2) mant = mant * kPow10u[fl2] + swar_parse_u64(fp + 8, fl2);
    if (mant > kMaxExactDoubleInteger) return std::nullopt;
    double d = static_cast<double>(mant);
    if (fl) d /= kPow10[fl];
    if (d > 131072) return std::nullopt;
    const char* q = has_dot ? fp + fl : p + il;
    if (*q == 'e' || *q == 'E') return std::nullopt;
    return ParsedSliderLength{d, q};
}

// One pshufb per control point right-aligns both coordinates' digits into
// two 4-byte groups (0x80 lanes read as zero); maddubs/madd then decode the
// pair together into one 8-byte point. The source vector starts
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

// `src` starts at a '|'; callers validate that both digit counts are 1..4.
#if FOSU_SIMD_X86
template <typename Point>
inline Point decode_slider_point(__m128i src, uint32_t xl, uint32_t yl,
                                 const HitObjectParseConstants& k) {
    static_assert(sizeof(Point) == 8 && offsetof(Point, x) == 0 && offsetof(Point, y) == 4);
    const __m128i shuf = _mm_load_si128(reinterpret_cast<const __m128i*>(
        kPointShuf[(xl - 1) * 4 + (yl - 1)].b));
    const __m128i placed =
        _mm_shuffle_epi8(_mm_sub_epi8(src, _mm256_castsi256_si128(k.zero)), shuf);
    const auto coordinates = _mm_madd_epi16(_mm_maddubs_epi16(placed, k.pair_weights), k.word_weights);
    return std::bit_cast<Point>(static_cast<uint64_t>(_mm_cvtsi128_si64(coordinates)));
}

#else
template <typename Point>
inline Point decode_slider_point(uint8x16_t src, uint32_t xl, uint32_t yl,
                                 const HitObjectParseConstants& k) {
    static_assert(sizeof(Point) == 8 && offsetof(Point, x) == 0 && offsetof(Point, y) == 4);
    const auto* shuf = reinterpret_cast<const uint8_t*>(
        kPointShuf[(xl - 1) * 4 + (yl - 1)].b);
    const auto coordinates = decimal_groups(vqtbl1q_u8(vsubq_u8(src, k.zero), vld1q_u8(shuf)));
    return std::bit_cast<Point>(vgetq_lane_u64(vreinterpretq_u64_u32(coordinates), 0));
}
#endif

template <typename Point>
struct ParsedSliderPointPrefix {
    Point first;
    Point second;  // Meaningful only when has_second is true.
    const char* next;
    bool has_second;
};

// Up to two editor-shaped points ("|x:y", 1..4 digits each) from one 32-byte
// window. Decode only validated points; if the second is absent or needs
// general parsing, return the first and leave the cursor at its end. Most
// sliders have one or two points, avoiding the general point loop entirely.
// Returns coordinates and the next input position, or nullopt
// if the first point needs the general parser. No destination is modified.
template <typename Point>
inline std::optional<ParsedSliderPointPrefix<Point>>
try_parse_slider_point_prefix_fast(
    const char* p, const HitObjectParseConstants& k) {
    const Bytes32 v = load32(p);
    const uint32_t nd = nondigit_mask32(v, k.bias, k.thr);
    const auto colon = equal_mask32(v, k.colon);
    const auto pipe = equal_mask32(v, k.pipe);
#if FOSU_SIMD_X86
    const uint32_t sep = static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_or_si256(_mm256_cmpeq_epi8(v, k.pipe), _mm256_cmpeq_epi8(v, k.comma))));
#else
    const uint32_t sep =
        byte_mask16(vorrq_u8(vceqq_u8(v.val[0], k.pipe), vceqq_u8(v.val[0], k.comma))) |
        (byte_mask16(vorrq_u8(vceqq_u8(v.val[1], k.pipe), vceqq_u8(v.val[1], k.comma)))
         << 16);
#endif
    // Skip the opening '|', then locate ':', separator, ':', separator.
    // Clearing each boundary bit lets later positions be found without
    // waiting for the preceding coordinate length. Missing boundaries are 32;
    // use 64-bit mask tests below so that sentinel remains a defined shift.
    uint32_t boundaries = nd & ~1u;
    const uint32_t first_colon = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t first_end = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t second_colon = trailing_zeros(boundaries);
    boundaries &= boundaries - 1;
    const uint32_t second_end = trailing_zeros(boundaries);
    const uint32_t first_x_digits = first_colon - 1;
    const uint32_t first_y_digits = first_end - first_colon - 1;
    const bool first_valid =
        (pipe & 1) & (((first_x_digits - 1) | (first_y_digits - 1)) <= 3) &
        ((static_cast<uint64_t>(colon) >> first_colon) & 1) &
        ((static_cast<uint64_t>(sep) >> first_end) & 1);
    const uint32_t second_x_digits = second_colon - first_end - 1;
    const uint32_t second_y_digits = second_end - second_colon - 1;
    const bool second_valid =
        ((static_cast<uint64_t>(pipe) >> first_end) & 1) &
        (((second_x_digits - 1) | (second_y_digits - 1)) <= 3) &
        ((static_cast<uint64_t>(colon) >> second_colon) & 1) &
        ((static_cast<uint64_t>(sep) >> second_end) & 1);
    if (!first_valid) return std::nullopt;
#if FOSU_SIMD_X86
    const auto first = decode_slider_point<Point>(
        _mm256_castsi256_si128(v), first_x_digits, first_y_digits, k);
#else
    const auto first = decode_slider_point<Point>(
        v.val[0], first_x_digits, first_y_digits, k);
#endif
    if (!second_valid)
        return ParsedSliderPointPrefix<Point>{first, {}, p + first_end, false};
#if FOSU_SIMD_X86
    const auto second = decode_slider_point<Point>(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + first_end)),
        second_x_digits, second_y_digits, k);
#else
    const auto second = decode_slider_point<Point>(
        vld1q_u8(reinterpret_cast<const uint8_t*>(p + first_end)),
        second_x_digits, second_y_digits, k);
#endif
    return ParsedSliderPointPrefix<Point>{
        first, second, p + second_end, true};
}
#endif

// Decode one "|x:y" point without modifying input or destination storage.
// The common 1-4 digit shape uses SIMD; other spellings use bounded parsing.
template <typename Point>
inline std::optional<ParsedSliderPoint<Point>> parse_slider_point(
    const char* p, const char* end,
    [[maybe_unused]] const HitObjectParseConstants& k) {
    if (p >= end || *p != '|') return std::nullopt;
#if FOSU_SIMD
#if FOSU_SIMD_X86
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    const __m128i biased = _mm_add_epi8(v, _mm256_castsi256_si128(k.bias));
    const auto nd = static_cast<uint32_t>(_mm_movemask_epi8(
        _mm_cmpgt_epi8(biased, _mm256_castsi256_si128(k.thr))));
    const auto colon = static_cast<uint32_t>(_mm_movemask_epi8(
        _mm_cmpeq_epi8(v, _mm256_castsi256_si128(k.colon))));
#else
    const auto v = vld1q_u8(reinterpret_cast<const uint8_t*>(p));
    const auto nd = nondigit_mask16(v);
    const auto colon = byte_mask16(vceqq_u8(v, k.colon));
#endif
    const uint32_t xl = trailing_zeros(nd >> 1);
    const uint32_t c = xl & 7;
    const uint32_t yl = trailing_zeros(nd >> (2 + c));
    if (((xl - 1) | (yl - 1)) <= 3 && ((colon >> (1 + c)) & 1)) {
        const char* next = p + 2 + xl + yl;
        if (*next == '|' || *next == ',')
            return ParsedSliderPoint<Point>{
                decode_slider_point<Point>(v, xl, yl, k), next};
    }
#endif
    const auto x = parse_slider_coordinate(p + 1, end);
    if (!x || *x->next != ':') return std::nullopt;
    const auto y = parse_slider_coordinate(x->next + 1, end);
    if (!y) return std::nullopt;
    return ParsedSliderPoint<Point>{Point{x->value, y->value}, y->next};
}

}  // namespace fosu::internal
