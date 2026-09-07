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
    return libc_number(p, end, out, true);
}
}

#define parse_double libc_double
#define parse_beat_length libc_beat_length
#include <fosu/parser.hpp>
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
