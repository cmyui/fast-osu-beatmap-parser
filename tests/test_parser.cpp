#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <fosu/parser.hpp>

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        auto va = (a);                                                       \
        auto vb = (b);                                                       \
        if (!(va == vb)) {                                                   \
            printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__,        \
                   __LINE__, #a, #b, (long long)va, (long long)vb);          \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static fosu::Beatmap parse_str(const std::string& s, bool use_simd = true) {
    static fosu::FileBuffer buf;  // keep alive for string_view fields
    buf = fosu::make_padded(s);
    return fosu::parse(buf, {.use_simd = use_simd});
}

static const char* kFullMap =
    "\xEF\xBB\xBFosu file format v14\r\n"
    "\r\n"
    "[General]\r\n"
    "AudioFilename: audio.mp3\r\n"
    "AudioLeadIn: 0\r\n"
    "PreviewTime: 53342\r\n"
    "Countdown: 0\r\n"
    "SampleSet: Soft\r\n"
    "StackLeniency: 0.7\r\n"
    "Mode: 0\r\n"
    "LetterboxInBreaks: 0\r\n"
    "WidescreenStoryboard: 1\r\n"
    "\r\n"
    "[Editor]\r\n"
    "Bookmarks: 11240,22540\r\n"
    "DistanceSpacing: 1.1\r\n"
    "BeatDivisor: 4\r\n"
    "GridSize: 32\r\n"
    "TimelineZoom: 2.4\r\n"
    "\r\n"
    "[Metadata]\r\n"
    "Title:Painters of the Tempest\r\n"
    "TitleUnicode:Painters of the Tempest\r\n"
    "Artist:Ne Obliviscaris\r\n"
    "ArtistUnicode:Ne Obliviscaris\r\n"
    "Creator:cmyui\r\n"
    "Version:Extreme\r\n"
    "Source:\r\n"
    "Tags:prog metal akatsuki\r\n"
    "BeatmapID:1193177\r\n"
    "BeatmapSetID:562454\r\n"
    "\r\n"
    "[Difficulty]\r\n"
    "HPDrainRate:5.5\r\n"
    "CircleSize:4\r\n"
    "OverallDifficulty:9\r\n"
    "ApproachRate:9.3\r\n"
    "SliderMultiplier:1.8\r\n"
    "SliderTickRate:1\r\n"
    "\r\n"
    "[Events]\r\n"
    "//Background and Video events\r\n"
    "0,0,\"bg.jpg\",0,0\r\n"
    "Video,-320,\"intro.mp4\"\r\n"
    "//Break Periods\r\n"
    "2,133342,140010\r\n"
    "//Storyboard Layer 0 (Background)\r\n"
    "Sprite,Background,Centre,\"sb/flash.png\",320,240\r\n"
    " F,0,133342,,1,0\r\n"
    "\r\n"
    "[TimingPoints]\r\n"
    "1240,342.857142857143,4,2,1,60,1,0\r\n"
    "11240,-83.3333333333333,4,2,1,60,0,1\r\n"
    "\r\n"
    "[Colours]\r\n"
    "Combo1 : 255,128,0\r\n"
    "Combo2 : 0,64,255\r\n"
    "\r\n"
    "[HitObjects]\r\n"
    "256,192,11240,1,0,0:0:0:0:\r\n"
    "100,100,11583,5,12,0:0:0:0:\r\n"
    "52,84,11926,2,0,B|172:44|292:84,1,240,2|0,0:0|0:0,0:0:0:0:\r\n"
    "256,192,13297,12,4,15354,0:0:0:0:\r\n"
    "448,320,15697,6,2,P|384:236|306:222,2,180.599999999999\r\n"
    "77,406,17068,1,2\r\n";

static void test_full_map() {
    for (bool simd : {true, false}) {
        auto bm = parse_str(kFullMap, simd);
        CHECK_EQ(bm.format_version, 14);
        CHECK(bm.audio_filename == "audio.mp3");
        CHECK_EQ(bm.preview_time, 53342);
        CHECK(bm.sample_set == "Soft");
        CHECK(bm.widescreen_storyboard);
        CHECK(!bm.letterbox_in_breaks);
        CHECK(bm.bookmarks == "11240,22540");
        CHECK_EQ(bm.grid_size, 32);
        CHECK(bm.title == "Painters of the Tempest");
        CHECK(bm.creator == "cmyui");
        CHECK_EQ(bm.beatmap_id, 1193177);
        CHECK_EQ(bm.beatmap_set_id, 562454);
        CHECK(std::abs(bm.hp - 5.5) < 1e-9);
        CHECK(std::abs(bm.ar - 9.3) < 1e-9);
        CHECK(std::abs(bm.slider_multiplier - 1.8) < 1e-9);
        CHECK(bm.background == "bg.jpg");
        CHECK(bm.video == "intro.mp4");
        CHECK_EQ(bm.breaks.size(), 1u);
        CHECK_EQ(bm.breaks[0].start, 133342);
        CHECK_EQ(bm.breaks[0].end, 140010);
        CHECK_EQ(bm.stats.storyboard_lines, 2u);
        CHECK_EQ(bm.combo_colours.size(), 2u);
        CHECK_EQ(bm.combo_colours[0], 0xFF8000u);
        CHECK_EQ(bm.combo_colours[1], 0x0040FFu);

        CHECK_EQ(bm.timing_points.size(), 2u);
        CHECK(std::abs(bm.timing_points[0].beat_length - 342.857142857143) < 1e-6);
        CHECK(bm.timing_points[0].uninherited);
        CHECK(!bm.timing_points[1].uninherited);
        CHECK_EQ(bm.timing_points[1].effects, 1u);
        CHECK_EQ(bm.timing_points[1].volume, 60);

        CHECK_EQ(bm.hit_objects.size(), 6u);
        const auto& circ = bm.hit_objects[0];
        CHECK_EQ(circ.x, 256);
        CHECK_EQ(circ.y, 192);
        CHECK_EQ(circ.time, 11240);
        CHECK_EQ(circ.type, 1u);
        CHECK_EQ(circ.hitsound, 0u);
        CHECK(circ.hit_sample == "0:0:0:0:");

        CHECK_EQ(bm.hit_objects[1].hitsound, 12u);  // 2-digit hitsound

        const auto& sl = bm.hit_objects[2];
        CHECK(sl.is_slider());
        CHECK(sl.slider != fosu::HitObject::kNoSlider);
        const auto& s = bm.sliders[sl.slider];
        CHECK_EQ(s.curve_type, 'B');
        CHECK_EQ(s.point_count, 2u);
        CHECK_EQ(bm.slider_points[s.point_begin].x, 172);
        CHECK_EQ(bm.slider_points[s.point_begin].y, 44);
        CHECK_EQ(bm.slider_points[s.point_begin + 1].x, 292);
        CHECK_EQ(s.slides, 1);
        CHECK(std::abs(s.length - 240) < 1e-9);
        CHECK(s.edge_sounds == "2|0");
        CHECK(s.edge_sets == "0:0|0:0");
        CHECK(sl.hit_sample == "0:0:0:0:");

        const auto& sp = bm.hit_objects[3];
        CHECK(sp.is_spinner());
        CHECK_EQ(sp.end_time, 15354);

        const auto& sl2 = bm.hit_objects[4];
        CHECK(sl2.is_slider());
        CHECK(std::abs(bm.sliders[sl2.slider].length - 180.599999999999) < 1e-6);
        CHECK_EQ(bm.sliders[sl2.slider].slides, 2);

        const auto& bare = bm.hit_objects[5];  // v3-style, no hitSample
        CHECK_EQ(bare.x, 77);
        CHECK_EQ(bare.hitsound, 2u);
        CHECK(bare.hit_sample.empty());

        CHECK_EQ(bm.stats.malformed_lines, 0u);
#if FOSU_SIMD_X86
        if (simd) CHECK_EQ(bm.stats.fast_path_lines, 6u);
#endif
    }
}

static void test_old_format() {
    auto bm = parse_str(
        "osu file format v3\n"
        "[General]\n"
        "AudioFilename: old.mp3\n"
        "[Difficulty]\n"
        "HPDrainRate:6\n"
        "CircleSize:4\n"
        "OverallDifficulty:7\n"
        "SliderMultiplier: 1.4\n"
        "SliderTickRate: 1\n"
        "[TimingPoints]\n"
        "8074.13793103448,344.827586206897\n"
        "[HitObjects]\n"
        "56,120,8074,1,4\n"
        "200,72,8419,2,0,B|248:8|320:40,1,140\n");
    CHECK_EQ(bm.format_version, 3);
    CHECK(std::abs(bm.ar - 7.0) < 1e-9);  // AR falls back to OD
    CHECK_EQ(bm.timing_points.size(), 1u);
    CHECK_EQ(bm.timing_points[0].meter, 4);
    CHECK_EQ(bm.timing_points[0].volume, 100);
    CHECK(bm.timing_points[0].uninherited);
    CHECK_EQ(bm.hit_objects.size(), 2u);
    CHECK_EQ(bm.hit_objects[0].hitsound, 4u);
}

static void test_mania_hold() {
    auto bm = parse_str(
        "osu file format v14\n"
        "[General]\n"
        "Mode: 3\n"
        "[HitObjects]\n"
        "320,192,16504,128,0,16999:0:0:0:0:\n");
    CHECK_EQ(bm.hit_objects.size(), 1u);
    const auto& h = bm.hit_objects[0];
    CHECK(h.is_hold());
    CHECK_EQ(h.end_time, 16999);
    CHECK(h.hit_sample == "0:0:0:0:");
}

static void test_aspire_edge_cases() {
    auto bm = parse_str(
        "osu file format v14\n"
        "[HitObjects]\n"
        "-48,192,1000,1,0\n"                       // negative x -> fallback
        "640,-24,2000,1,0\n"                       // negative y
        "256,192,-1000,1,0\n"                      // negative time
        "5120,192,3000,1,0\n"                      // 4-digit x
        "256.5,112.2,4000,1,0\n"                   // decimal coords
        "0,0,4294967290,1,0\n"                     // time > INT32_MAX
        "100,100,5000,2,0,B|-64:-32|700:512,1,600\n");  // negative ctrl points
    CHECK_EQ(bm.hit_objects.size(), 7u);
    CHECK_EQ(bm.hit_objects[0].x, -48);
    CHECK_EQ(bm.hit_objects[1].y, -24);
    CHECK_EQ(bm.hit_objects[2].time, -1000);
    CHECK_EQ(bm.hit_objects[3].x, 5120);
    CHECK_EQ(bm.hit_objects[4].x, 256);   // truncated
    CHECK_EQ(bm.hit_objects[4].y, 112);
    CHECK_EQ(bm.hit_objects[5].time, INT32_MAX);  // saturated
    const auto& s = bm.sliders[bm.hit_objects[6].slider];
    CHECK_EQ(bm.slider_points[s.point_begin].x, -64);
    CHECK_EQ(bm.slider_points[s.point_begin].y, -32);
    CHECK_EQ(bm.stats.malformed_lines, 0u);
#if FOSU_SIMD_X86
    CHECK_EQ(bm.stats.fast_path_lines, 1u);  // only the slider line is regular
#endif
}

static void test_malformed() {
    auto bm = parse_str(
        "osu file format v14\n"
        "[HitObjects]\n"
        "\n"
        ",,,,\n"
        "abc\n"
        "12,,123,4,0\n"                 // empty field (index-alias trap)
        "256,192\n"                     // truncated line
        "256,192,1000,2,0,B|\n"         // truncated slider
        "256,192,1000,1,0\n");          // one valid line
    CHECK_EQ(bm.hit_objects.size(), 1u);
    CHECK_EQ(bm.hit_objects[0].time, 1000);
    CHECK_EQ(bm.stats.malformed_lines, 5u);
}

#if FOSU_SIMD_X86
static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint64_t rng() {
    rng_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Generate a value that renders with exactly `digits` decimal digits.
static uint64_t value_with_digits(int digits, uint64_t max) {
    const uint64_t lo = digits == 1 ? 0 : fosu::detail::kPow10[digits - 1] < 1e19
                            ? (uint64_t)fosu::detail::kPow10[digits - 1]
                            : 0;
    uint64_t hi = (uint64_t)fosu::detail::kPow10[digits] - 1;
    if (hi > max) hi = max;
    if (lo > hi) return hi;
    return lo + rng() % (hi - lo + 1);
}

static void test_fuzz_equivalence() {
    char buf[128];
    int fast_taken = 0;
    for (int iter = 0; iter < 300000; ++iter) {
        const int lx = 1 + (int)(rng() % 3);
        const int ly = 1 + (int)(rng() % 3);
        const int lt = 1 + (int)(rng() % 10);
        const int lty = 1 + (int)(rng() % 3);
        const uint64_t x = value_with_digits(lx, 999);
        const uint64_t y = value_with_digits(ly, 999);
        const uint64_t t = value_with_digits(lt, 9999999999ull);
        const uint64_t ty = value_with_digits(lty, 255);
        const uint64_t hs = rng() % 100;
        int len = snprintf(buf, sizeof buf,
                           "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                           ",%" PRIu64 ",0:0:0:0:",
                           x, y, t, ty, hs);
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        // Randomly corrupt some lines; the invariant is that whenever the
        // fast path accepts, it must agree exactly with the scalar path.
        if (rng() % 4 == 0) {
            const int pos = (int)(rng() % (uint64_t)len);
            const char junk[] = {'-', '.', ',', 'x', ' ', '|'};
            buf[pos] = junk[rng() % sizeof junk];
        }

        fosu::HitObject fast{}, ref{};
        const int fn = fosu::detail::fast_parse_prefix(buf, fast);
        const int rn = fosu::detail::scalar_parse_prefix(buf, strlen(buf), ref);
        if (fn < 0) continue;
        ++fast_taken;
        CHECK(rn >= 0);
        CHECK_EQ(fn, rn);
        CHECK_EQ(fast.x, ref.x);
        CHECK_EQ(fast.y, ref.y);
        CHECK_EQ(fast.time, ref.time);
        CHECK_EQ(fast.type, ref.type);
        CHECK_EQ(fast.hitsound, ref.hitsound);
        if (g_failures) {
            printf("  failing line: %s\n", buf);
            return;
        }
    }
    printf("  fuzz: fast path accepted %d lines\n", fast_taken);
    CHECK(fast_taken > 100000);
}
#endif

int main() {
    test_full_map();
    test_old_format();
    test_mania_hold();
    test_aspire_edge_cases();
    test_malformed();
#if FOSU_SIMD_X86
    test_fuzz_equivalence();
    printf("SIMD path: enabled\n");
#else
    printf("SIMD path: not built (non-x86 target)\n");
#endif
    if (g_failures) {
        printf("%d FAILURES\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
