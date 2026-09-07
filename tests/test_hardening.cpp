#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <fosu/parser.hpp>
#include <fosu/offset_beatmap.hpp>
#include "../oneshot/dump.hpp"

static void check(const std::string& text) {
    auto input = fosu::make_padded(text);
    auto scalar = fosu::parse(input, {.use_simd = false});
    auto simd = fosu::parse(input);
    fosu::OffsetBeatmap offset;
    fosu::parse_into(input, offset);
    scalar.stats.fast_path_lines = scalar.stats.slow_path_lines = 0;
    simd.stats.fast_path_lines = simd.stats.slow_path_lines = 0;
    offset.stats.fast_path_lines = offset.stats.slow_path_lines = 0;
    std::string a, b, c;
    fosu_dump::dump(scalar, a);
    fosu_dump::dump(simd, b);
    fosu_dump::dump(offset, c);
    assert(a == b && a == c);
}

int main() {
    // The short sample shortcut must reject every non-digit/separator byte,
    // including high-bit bytes. Full parsing may accept other spellings via
    // its bounded fallback; all representations must still agree.
    for (size_t pos = 0; pos < 8; ++pos) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            std::string sample = "0:1:2:3:";
            sample[pos] = static_cast<char>(byte);
            auto padded = fosu::make_padded(sample);
            const bool exact_shape = pos % 2 ? byte == ':' : byte >= '0' && byte <= '9';
            assert(fosu::detail::short_sample(padded.data.get()) == exact_shape);
            check("[HitObjects]\n1,2,3,1,0," + sample);
        }
    }
    // Circle precedence applies even when slider/spinner/hold bits are set.
    for (int type : {1, 3, 9, 129, 255}) {
        const std::string line = "[HitObjects]\n1,2,3," + std::to_string(type) + ",0,0:1:2:3:";
        for (const std::string ending : {"", "\n", "\r\n"}) check(line + ending);
    }
    assert(fosu::parse(nullptr, 0).hit_objects.empty());
    assert(fosu::parse(fosu::make_padded({})).hit_objects.empty());
    auto embedded = fosu::make_padded(
        "[Metadata]\nTitle:[HitObjects]\n1,2,3,1,0\n[HitObjects]\n1,2,4,1,0\n");
    auto selected = fosu::parse(embedded, {.sections = fosu::kSectionHitObjects});
    assert(selected.hit_objects.size() == 1 && selected.hit_objects[0].time == 4);
    auto point_input = fosu::make_padded("[HitObjects]\n1,2,3,2,0,B|1:2.5|3:4e1,1,10\n");
    auto points = fosu::parse(point_input);
    assert(points.sliders.size() == 1 && points.slider_points.size() == 2);
    assert(points.slider_points[0].y == 2 && points.slider_points[1].y == 40);
    check("[TimingPoints]\n\r\r// comment\n0,500\n[HitObjects]\n\r\r// comment\n1,2,3,1,0");
    check("[Events]\n\n \n\t// comment\n");
    check("[HitObjects]\n0,2,3,3,0\r,");
    check("[HitObjects]\n1,2,3,2,0,B|1:2\v|3:4,1,10");
    for (const std::string decimal : {"111.99999999999987", "999.9999999999999",
                                     "99999.9999999999999"}) {
        const std::string text = "[TimingPoints]\n0,100.0000000000000,4,2,1,100,1,0\n1," +
            decimal + ",4,2,1,100,1,0\n[HitObjects]\n1,2,3,2,0,B|1:2,1," + decimal;
        check(text);
        auto input = fosu::make_padded(text);
        auto map = fosu::parse(input);
        const double expected = strtod(decimal.c_str(), nullptr);
        assert(map.timing_points[1].beat_length == expected);
        assert(map.sliders[0].length == expected);
    }
    for (bool simd : {false, true}) {
        auto input = fosu::make_padded(
            "[Metadata]\nTitle:real\nTitlX:wrong\nBeatmapID:-9223372036854775808\n"
            "[MetadataFake]\nTitle:wrong\n[Difficulty]\nApproachRate:1e309\n"
            "[TimingPoints]\n0,500\n1,NaN,4,2,1,100,0,0\n2,NaN,4,2,1,100,1,0\n"
            "[HitObjects]\n256.5,192,1000.5,1,0\n1,2,2000.25,8,0,3000.75\n"
            "1,2,4000,2,0,B|1.5:2.5,1,2.5e2\n"
            "[Events]\n2,1e309,100\n2,1.25,9.75\n");
        auto m = fosu::parse(input, {.use_simd = simd});
        assert(m.title == "real" && m.beatmap_id == -1);
        assert(m.ar == 5 && m.stats.malformed_lines == 4);
        assert(m.timing_points.size() == 2 && std::isnan(m.timing_points[1].beat_length));
        assert(!m.timing_points[1].uninherited);
        assert(m.hit_objects.size() == 3 && m.hit_objects[0].x == 256);
        assert(m.hit_objects[0].time == 1000.5 && m.hit_objects[1].end_time == 3000.75);
        assert(m.sliders[0].length == 250 && m.slider_points[0].x == 1);
        assert(m.breaks.size() == 1 && m.breaks[0].start == 1.25 && m.breaks[0].end == 9.75);
    }
    // Long digit runs used to execute shifts by >= the operand width in SIMD
    // slider parsing. Vary every truncation point, including EOF without LF.
    for (const std::string& line : {
            "[HitObjects]\n1,2,3,2,0,B|" + std::string(150, '9'),
            "[HitObjects]\n1,2,3,2,0,B|1:2,1," + std::string(150, '9'),
            "[TimingPoints]\n" + std::string(150, '9'),
            std::string("[Metadata]\nBeatmapID:-9223372036854775808\n"),
            std::string("[HitObjects]\n1,2,3,1,0\0junk", 27)}) {
        for (size_t size = 0; size <= line.size(); ++size) check(line.substr(0, size));
    }
    for (const std::string number : {"9223372036854775807", "9223372036854775808",
             "18446744073709551615", "99999999999999999999999999999"}) {
        check("[Metadata]\nBeatmapID:" + number);
        check("[Metadata]\nBeatmapID:-" + number);
    }
    // Explicit logical end must bound the slow numeric fallback too.
    auto input = fosu::make_padded("1.234567890123456789e2junk");
    double value;
    const char* start = input.data.get();
    assert(fosu::detail::parse_double(start, start + 20, value) <= start + 20);
    puts("Hardening: numeric boundaries, fractional times, NaN, framing and path parity passed");
}
