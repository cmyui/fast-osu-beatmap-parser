#pragma once

// Scalar/SWAR numeric parsing. Like the rest of the parser, these helpers
// assume the buffer is followed by kBufferPadding readable zero bytes.

#include <cstdint>
#include <cmath>
#include <limits>
#include "detail/fast_float.h"

#include "swar.hpp"

namespace fosu::detail {

inline bool is_digit(char c) {
    return static_cast<uint8_t>(c - '0') <= 9;
}

// All parse_* helpers return the advanced pointer, or `p` unchanged on failure.

inline const char* parse_u64(const char* p, const char* end, uint64_t& out) {
    const char* start = p;
    uint64_t v = 0;
    while (p < end && is_digit(*p)) {
        const uint64_t digit = static_cast<unsigned>(*p - '0');
        if (v > UINT64_MAX / 10 || (v == UINT64_MAX / 10 && digit > UINT64_MAX % 10)) {
            do { ++p; } while (p < end && is_digit(*p));
            out = UINT64_MAX;
            return p;
        }
        v = v * 10 + digit;
        ++p;
    }
    if (p == start) return start;
    out = v;
    return p;
}

inline const char* parse_i64(const char* p, const char* end, int64_t& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) {
        neg = *p == '-';
        ++p;
    }
    uint64_t mag;
    const char* q = parse_u64(p, end, mag);
    if (q == p) return start;
    // Convert only representable magnitudes; negating INT64_MIN is undefined.
    if (neg) out = mag >= uint64_t(INT64_MAX) + 1 ? INT64_MIN : -static_cast<int64_t>(mag);
    else out = mag > uint64_t(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(mag);
    return q;
}

inline int32_t clamp_i32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(v);
}

inline constexpr double kPow10[20] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
};

inline constexpr uint64_t kPow10u[9] = {
    1,       10,       100,       1000,     10000,
    100000,  1000000,  10000000,  100000000,
};
inline constexpr uint64_t kMaxExactDoubleInteger = 1ull << 53;

// Fast decimal parse for the values that appear in .osu files. Digit runs
// are consumed 8 at a time with SWAR conversion instead of byte loops.
// Values with exponents or more than 18 significant digits fall back to
// a bounded, locale-independent conversion.
template <auto Fallback>
inline const char* parse_double_impl(const char* p, const char* end, double& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) {
        neg = *p == '-';
        ++p;
    }
    uint64_t mant = 0;
    int digits = 0;
    int frac = 0;
    bool any = false;
    for (;;) {
        uint32_t run = digit_run8(p);
        if (run > static_cast<uint64_t>(end - p))
            run = static_cast<uint32_t>(end - p);
        if (!run) break;
        any = true;
        if (digits + static_cast<int>(run) > 18) {
            return Fallback(start, end, out);
        }
        mant = mant * kPow10u[run] + swar_parse_u64(p, run);
        digits += static_cast<int>(run);
        p += run;
        if (run < 8) break;
    }
    if (p < end && *p == '.') {
        ++p;
        for (;;) {
            uint32_t run = digit_run8(p);
            if (run > static_cast<uint64_t>(end - p))
                run = static_cast<uint32_t>(end - p);
            if (!run) break;
            any = true;
            if (digits + static_cast<int>(run) > 18) {
                return Fallback(start, end, out);
            }
            mant = mant * kPow10u[run] + swar_parse_u64(p, run);
            digits += static_cast<int>(run);
            frac += static_cast<int>(run);
            p += run;
            if (run < 8) break;
        }
    }
    if (!any) return Fallback(start, end, out);
    if (p < end && (*p == 'e' || *p == 'E')) {
        return Fallback(start, end, out);
    }
    // Rounding an inexact integer mantissa before division can move the
    // result by one ULP. The fallback rounds the original decimal once.
    if (mant > kMaxExactDoubleInteger) return Fallback(start, end, out);
    double v = static_cast<double>(mant);
    if (frac) v /= kPow10[frac];
    out = neg ? -v : v;
    return p;
}

inline const char* bounded_double(const char* start, const char* end, double& value) {
    const char* p = start;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p < end && *p == '+') {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) return start;
    }
    const auto r = fast_float::from_chars(p, end, value);
    // .NET's numeric parser accepts underflow rounded to signed zero.
    return r.ec == std::errc() || (r.ec == std::errc::result_out_of_range && value == 0)
        ? r.ptr : start;
}

inline const char* parse_double(const char* p, const char* end, double& out) {
    const char* q = parse_double_impl<bounded_double>(p, end, out);
    return q != p && std::isfinite(out) ? q : p;
}

inline bool is_numeric_space(char c) {
    return c == ' ' || (static_cast<unsigned char>(c) - 9u <= 4u);
}

inline const char* skip_numeric_space(const char* p, const char* end) {
    // The input is padded even at end. Digits and field separators take one
    // byte comparison; uncommon control/space bytes use the bounded loop.
    if (static_cast<unsigned char>(*p) > 32) return p;
    while (p < end && is_numeric_space(*p)) ++p;
    return p;
}

inline bool ignored_line(const char* p, const char* end) {
    p = skip_numeric_space(p, end);
    return p == end || (end - p >= 2 && p[0] == '/' && p[1] == '/');
}

// Numeric acceptance follows osu.Game Parsing, independently of gameplay
// clamping or our raw-field storage types. These run only outside digit-only
// fast paths whose field widths already prove the same limits.
inline const char* parse_osu_int(const char* p, const char* end, int64_t& out) {
    const char* first = skip_numeric_space(p, end);
    const char* q = parse_i64(first, end, out);
    if (q == first || out < -INT32_MAX || out > INT32_MAX) return p;
    return skip_numeric_space(q, end);
}

inline const char* parse_osu_double(const char* p, const char* end, double& out,
                                   double limit = INT32_MAX) {
    const char* first = skip_numeric_space(p, end);
    const char* q = parse_double_impl<bounded_double>(first, end, out);
    // One absolute-value bound also rejects infinities and NaN.
    if (q == first || !(std::abs(out) <= limit)) return p;
    return skip_numeric_space(q, end);
}

inline const char* parse_osu_float(const char* p, const char* end, float& out,
                                  float limit = float(INT32_MAX)) {
    const char* first = skip_numeric_space(p, end);
    if (first < end && *first == '+') {
        ++first;
        if (first < end && (*first == '+' || *first == '-')) return p;
    }
    const auto r = fast_float::from_chars(first, end, out);
    if (r.ec != std::errc() && !(r.ec == std::errc::result_out_of_range && out == 0)) return p;
    if (!(std::abs(out) <= limit)) return p;
    return skip_numeric_space(r.ptr, end);
}

// NaN has a defined gameplay meaning only for inherited timing points.
inline const char* parse_beat_length(const char* p, const char* end, double& out) {
    const char* first = skip_numeric_space(p, end);
    if (first < end && (*first == '+' || *first == '-')) ++first;
    if (end - first >= 3 && (first[0] | 32) == 'n' && (first[1] | 32) == 'a' && (first[2] | 32) == 'n') {
        out = std::numeric_limits<double>::quiet_NaN();
        return skip_numeric_space(first + 3, end);
    }
    return parse_osu_double(p, end, out);
}

}  // namespace fosu::detail
