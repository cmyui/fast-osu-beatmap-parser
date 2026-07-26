#pragma once

#include <cstdint>
#include <cstdlib>

namespace fosu::detail {

inline bool is_digit(char c) {
    return static_cast<uint8_t>(c - '0') <= 9;
}

// All parse_* helpers return the advanced pointer, or `p` unchanged on failure.

inline const char* parse_u64(const char* p, const char* end, uint64_t& out) {
    const char* start = p;
    uint64_t v = 0;
    int digits = 0;
    while (p < end && is_digit(*p)) {
        if (digits < 19) {
            v = v * 10 + static_cast<uint64_t>(*p - '0');
            ++digits;
        }
        ++p;
    }
    if (p == start) return start;
    out = v;
    return p;
}

inline const char* parse_i64(const char* p, const char* end, int64_t& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && *p == '-') {
        neg = true;
        ++p;
    }
    uint64_t mag;
    const char* q = parse_u64(p, end, mag);
    if (q == p) return start;
    out = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
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

// Fast decimal parse for the values that appear in .osu files. Values with
// exponents or more than 18 significant digits fall back to strtod, which
// requires the buffer to be followed by a parse-terminating byte (the padded
// file buffer guarantees this).
inline const char* parse_double(const char* p, const char* end, double& out) {
    const char* start = p;
    bool neg = false;
    if (p < end && *p == '-') {
        neg = true;
        ++p;
    }
    uint64_t mant = 0;
    int digits = 0;
    int frac = 0;
    bool any = false;
    bool overflow = false;
    while (p < end && is_digit(*p)) {
        any = true;
        if (digits < 18) {
            mant = mant * 10 + static_cast<uint64_t>(*p - '0');
            ++digits;
        } else {
            overflow = true;
        }
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        while (p < end && is_digit(*p)) {
            any = true;
            if (digits < 18) {
                mant = mant * 10 + static_cast<uint64_t>(*p - '0');
                ++digits;
                ++frac;
            }
            ++p;
        }
    }
    if (!any) return start;
    if (overflow || (p < end && (*p == 'e' || *p == 'E'))) {
        char* e;
        out = strtod(start, &e);
        return e;
    }
    double v = static_cast<double>(mant);
    if (frac) v /= kPow10[frac];
    out = neg ? -v : v;
    return p;
}

}  // namespace fosu::detail
