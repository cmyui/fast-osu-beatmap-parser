// Test-only DSO: retain record/framing logic but replace scalar decimal
// conversion with libc over a bounded copy. Compile without AVX2 and with
// hidden visibility so inline parser definitions cannot interpose on the test.
#include <cmath>
#include <cstdlib>
#include <string>
#include <fosu/scalar_parse.hpp>

namespace fosu::detail {
inline const char* libc_number(const char* p, const char* end, double& out, bool nan) {
    std::string bounded(p, end);
    char* next;
    out = strtod(bounded.c_str(), &next);
    return std::isfinite(out) || (nan && std::isnan(out))
        ? p + (next - bounded.c_str()) : p;
}
inline const char* libc_double(const char* p, const char* end, double& out) {
    return libc_number(p, end, out, false);
}
inline const char* libc_beat_length(const char* p, const char* end, double& out) {
    const char* q = libc_number(p, end, out, true);
    return q != p && !(out < -INT32_MAX || out > INT32_MAX)
        ? skip_numeric_space(q, end) : p;
}
inline const char* libc_osu_double(const char* p, const char* end, double& out,
                                  double limit = INT32_MAX) {
    const char* q = libc_number(p, end, out, false);
    return q != p && !(out < -limit || out > limit) ? skip_numeric_space(q, end) : p;
}
inline const char* libc_osu_float(const char* p, const char* end, float& out,
                                 float limit = float(INT32_MAX)) {
    std::string bounded(p, end);
    char* next;
    out = strtof(bounded.c_str(), &next);
    if (next == bounded.c_str() || !std::isfinite(out) || out < -limit || out > limit) return p;
    return skip_numeric_space(p + (next - bounded.c_str()), end);
}
}

#define parse_double libc_double
#define parse_beat_length libc_beat_length
#define parse_osu_double libc_osu_double
#define parse_osu_float libc_osu_float
#include <fosu/parser.hpp>
#undef parse_osu_float
#undef parse_osu_double
#undef parse_beat_length
#undef parse_double
#include "../oneshot/dump.hpp"
static_assert(!FOSU_SIMD_X86, "the independent numeric oracle must use scalar parsing");

extern "C" __attribute__((visibility("default")))
void fosu_numeric_oracle(const char* data, size_t size, std::string& output) {
    auto map = fosu::parse(data, size, {.use_simd = false});
    map.stats.fast_path_lines = map.stats.slow_path_lines = 0;
    fosu_dump::dump(map, output);
}
