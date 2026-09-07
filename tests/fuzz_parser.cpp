// libFuzzer + ASan/UBSan: exercise full files and force arbitrary bytes through
// the hit-object and timing-point parsers, comparing all materialized fields.
#include <cassert>
#include <string>
#include <fosu/parser.hpp>
#include <fosu/offset_beatmap.hpp>
#include "../oneshot/dump.hpp"

static void check(std::string_view data) {
    auto input = fosu::make_padded(data);
    auto a = fosu::parse(input, {.use_simd = false});
    fosu::OffsetBeatmap b;
    fosu::parse_into(input, b);
    a.stats.fast_path_lines = a.stats.slow_path_lines = 0;
    b.stats.fast_path_lines = b.stats.slow_path_lines = 0;
    std::string x, y;
    fosu_dump::dump(a, x);
    fosu_dump::dump(b, y);
    assert(x == y);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 65536) return 0;
    std::string text(reinterpret_cast<const char*>(data), size);
    check(text);
    check("[HitObjects]\n" + text);
    check("[TimingPoints]\n" + text);
    return 0;
}
