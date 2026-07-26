#pragma once

// Scalar/SWAR numeric parsing. Like the rest of the parser, these helpers
// assume the buffer is followed by kBufferPadding readable zero bytes.

#include <cstdint>
#include <cstdlib>

#include "swar.hpp"

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

inline constexpr uint64_t kPow10u[9] = {
    1,       10,       100,       1000,     10000,
    100000,  1000000,  10000000,  100000000,
};

// Fast decimal parse for the values that appear in .osu files. Digit runs
// are consumed 8 at a time with SWAR conversion instead of byte loops.
// Values with exponents or more than 18 significant digits fall back to
// strtod (the buffer padding guarantees strtod terminates).
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
    for (;;) {
        uint32_t run = digit_run8(p);
        if (run > static_cast<uint64_t>(end - p))
            run = static_cast<uint32_t>(end - p);
        if (!run) break;
        any = true;
        if (digits + static_cast<int>(run) > 18) {
            char* e;
            out = strtod(start, &e);
            return e;
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
                char* e;
                out = strtod(start, &e);
                return e;
            }
            mant = mant * kPow10u[run] + swar_parse_u64(p, run);
            digits += static_cast<int>(run);
            frac += static_cast<int>(run);
            p += run;
            if (run < 8) break;
        }
    }
    if (!any) return start;
    if (p < end && (*p == 'e' || *p == 'E')) {
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
