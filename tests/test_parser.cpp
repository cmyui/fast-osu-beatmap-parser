#include <unistd.h>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

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

static void test_long_timing_offsets() {
    // Offsets past the 8-byte SWAR window (>27h marathons) must defer to
    // the generic parser, not vanish as malformed.
    for (bool simd : {true, false}) {
        auto bm = parse_str(
            "osu file format v14\n"
            "[TimingPoints]\n"
            "123456789,300.5,4,2,1,60,1,0\n"
            "4123456789,-50,4,2,1,60,0,0\n",
            simd);
        CHECK_EQ(bm.timing_points.size(), 2u);
        CHECK(bm.timing_points[0].time == 123456789.0);
        CHECK(bm.timing_points[1].time == 4123456789.0);
        CHECK_EQ(bm.timing_points[0].volume, 60);
        CHECK_EQ(bm.stats.malformed_lines, 0u);
    }
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

// Reference: the pre-SWAR byte-loop parse_double, kept verbatim as the
// behavioral baseline for <=18 significant digit inputs.
static const char* reference_parse_double(const char* p, const char* end,
                                          double& out) {
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
    while (p < end && fosu::detail::is_digit(*p)) {
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
        while (p < end && fosu::detail::is_digit(*p)) {
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
    if (frac) v /= fosu::detail::kPow10[frac];
    out = neg ? -v : v;
    return p;
}

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint64_t rng() {
    rng_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Fuzz the SWAR parse_double against the byte-loop reference: results must
// be bit-identical (identical mantissa accumulation) for <=16 significant
// digits, and identical to strtod beyond that (both delegate).
static void test_fuzz_parse_double() {
    char buf[96];
    for (int iter = 0; iter < 300000; ++iter) {
        const int int_digits = 1 + (int)(rng() % 9);
        const int frac_digits = (int)(rng() % 8);
        int len = 0;
        if (rng() % 3 == 0) buf[len++] = '-';
        for (int i = 0; i < int_digits; ++i)
            buf[len++] = char('0' + (i == 0 ? rng() % 9 + (int_digits > 1)
                                            : rng() % 10));
        if (frac_digits || rng() % 4 == 0) {
            buf[len++] = '.';
            for (int i = 0; i < frac_digits; ++i)
                buf[len++] = char('0' + rng() % 10);
        }
        const char* tail = ",4,2\r\n";
        const int payload = len;
        for (const char* t = tail; *t; ++t) buf[len++] = *t;
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        double got = -1, want = -2;
        const char* gp =
            fosu::detail::parse_double(buf, buf + payload, got);
        const char* wp = reference_parse_double(buf, buf + payload, want);
        CHECK_EQ(gp - buf, wp - buf);
        CHECK(got == want);
        if (g_failures) {
            printf("  failing double: %.*s\n", payload, buf);
            return;
        }
    }
    // Long-mantissa inputs delegate to strtod and must match it exactly.
    const char* long_cases[] = {"342.857142857142857142857",
                                "123456789012345678901", "0.6999999999999999556"};
    for (const char* c : long_cases) {
        std::string padded(c);
        padded.append(64, '\0');
        double got = 0;
        fosu::detail::parse_double(padded.data(), padded.data() + strlen(c), got);
        CHECK(got == strtod(c, nullptr));
    }
}

// Fuzz the SWAR slider coordinate parser against the general path.
static void test_fuzz_parse_coord() {
    char buf[64];
    for (int iter = 0; iter < 300000; ++iter) {
        int len = 0;
        const uint64_t kind = rng() % 16;
        if (kind == 0) buf[len++] = '-';
        if (kind != 1) {
            const int digits = 1 + (int)(rng() % (kind < 12 ? 4 : 8));
            for (int i = 0; i < digits; ++i)
                buf[len++] = char('0' + rng() % 10);
        }
        const int payload = len;
        buf[len++] = (rng() % 2) ? ':' : '|';
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        int32_t got = -777, want = -777;
        const char* gp = fosu::detail::parse_coord(buf, buf + payload, got);
        int64_t v;
        const char* wp = fosu::detail::parse_i64(buf, buf + payload, v);
        if (wp != buf) want = fosu::detail::clamp_i32(v);
        CHECK_EQ(gp - buf, wp - buf);
        CHECK_EQ(got, want);
        if (g_failures) {
            printf("  failing coord: %.*s\n", payload, buf);
            return;
        }
    }
}

#if FOSU_SIMD_X86
// Fuzz the one-pass timing point parser against the generic reference:
// whenever it accepts a line, every field must be bitwise identical.
// Shapes: 8-field editor lines plus old 2..7-field forms, decimal and
// negative offsets, integer and long-fraction beatLengths, and injected
// junk bytes (a '|' posing as the decimal point caught a real bug here).
static void test_fuzz_timing_point() {
    char buf[256];
    size_t accepted = 0;
    for (int iter = 0; iter < 400000; ++iter) {
        int len = 0;
        const uint64_t shape = rng() % 10;
        if (shape == 9) buf[len++] = '-';
        const int od = 1 + (int)(rng() % 11);
        for (int i = 0; i < od; ++i) buf[len++] = char('0' + rng() % 10);
        if (shape == 8) {
            buf[len++] = '.';
            for (int i = 0; i < 3; ++i) buf[len++] = char('0' + rng() % 10);
        }
        buf[len++] = ',';
        if (rng() % 2) buf[len++] = '-';
        const int bi = 1 + (int)(rng() % 4);
        for (int i = 0; i < bi; ++i) buf[len++] = char('0' + rng() % 10);
        if (rng() % 2) {
            buf[len++] = '.';
            const int bf = 1 + (int)(rng() % 15);
            for (int i = 0; i < bf; ++i) buf[len++] = char('0' + rng() % 10);
        }
        const int nf = shape == 7 ? (int)(rng() % 6) : 6;
        for (int f = 0; f < nf; ++f) {
            buf[len++] = ',';
            const int fd = 1 + (int)(rng() % 3);
            for (int i = 0; i < fd; ++i) buf[len++] = char('0' + rng() % 10);
        }
        if (rng() % 3 == 0) {
            const char junk[] = {'-', '.', ',', 'x', ' ', ':', '|', ','};
            buf[rng() % (uint64_t)len] = junk[rng() % 8];
        }
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        fosu::TimingPoint tp{};
        const auto a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));
        const auto b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + 32));
        if (!fosu::detail::fast_parse_timing_point(a, b, buf, (size_t)len, tp))
            continue;
        ++accepted;
        fosu::Beatmap ref;
        fosu::detail::parse_timing_point_line(ref, buf, (size_t)len);
        CHECK(!ref.timing_points.empty());
        if (!ref.timing_points.empty()) {
            const fosu::TimingPoint& w = ref.timing_points[0];
            CHECK(memcmp(&tp.time, &w.time, 8) == 0);
            CHECK(memcmp(&tp.beat_length, &w.beat_length, 8) == 0);
            CHECK_EQ(tp.meter, w.meter);
            CHECK_EQ(tp.sample_set, w.sample_set);
            CHECK_EQ(tp.sample_index, w.sample_index);
            CHECK_EQ(tp.volume, w.volume);
            CHECK_EQ(tp.uninherited, w.uninherited);
            CHECK_EQ(tp.effects, w.effects);
        }
        if (g_failures) {
            printf("  failing timing line: %.*s\n", len, buf);
            return;
        }
    }
    printf("  timing fuzz: fast path accepted %zu lines\n", accepted);
    CHECK(accepted > 80000);
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
        uint32_t nl_mask;
        const int fn = fosu::detail::fast_parse_prefix(buf, fast, nl_mask);
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

// --- parse_into / read_into reuse ---------------------------------------

static uint64_t fp_mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}
static uint64_t fp_sv(uint64_t h, std::string_view s) {
    h = fp_mix(h, s.size());
    for (char c : s) h = fp_mix(h, static_cast<uint8_t>(c));
    return h;
}
static uint64_t fp_d(uint64_t h, double d) {
    uint64_t b;
    memcpy(&b, &d, 8);
    return fp_mix(h, b);
}

// Covers every Beatmap field so reused-vs-fresh divergence anywhere shows.
static uint64_t fingerprint(const fosu::Beatmap& bm) {
    uint64_t h = 0;
    h = fp_mix(h, static_cast<uint64_t>(bm.format_version));
    h = fp_sv(h, bm.audio_filename);
    h = fp_mix(h, static_cast<uint32_t>(bm.audio_lead_in));
    h = fp_mix(h, static_cast<uint32_t>(bm.preview_time));
    h = fp_mix(h, static_cast<uint32_t>(bm.countdown));
    h = fp_sv(h, bm.sample_set);
    h = fp_d(h, bm.stack_leniency);
    h = fp_mix(h, static_cast<uint32_t>(bm.mode));
    h = fp_mix(h, bm.letterbox_in_breaks);
    h = fp_mix(h, bm.widescreen_storyboard);
    h = fp_mix(h, bm.epilepsy_warning);
    h = fp_mix(h, bm.special_style);
    h = fp_mix(h, bm.use_skin_sprites);
    h = fp_mix(h, bm.samples_match_playback_rate);
    h = fp_mix(h, static_cast<uint32_t>(bm.countdown_offset));
    h = fp_sv(h, bm.overlay_position);
    h = fp_sv(h, bm.skin_preference);
    h = fp_sv(h, bm.bookmarks);
    h = fp_d(h, bm.distance_spacing);
    h = fp_mix(h, static_cast<uint32_t>(bm.beat_divisor));
    h = fp_mix(h, static_cast<uint32_t>(bm.grid_size));
    h = fp_d(h, bm.timeline_zoom);
    h = fp_sv(h, bm.title);
    h = fp_sv(h, bm.title_unicode);
    h = fp_sv(h, bm.artist);
    h = fp_sv(h, bm.artist_unicode);
    h = fp_sv(h, bm.creator);
    h = fp_sv(h, bm.version);
    h = fp_sv(h, bm.source);
    h = fp_sv(h, bm.tags);
    h = fp_mix(h, static_cast<uint64_t>(bm.beatmap_id));
    h = fp_mix(h, static_cast<uint64_t>(bm.beatmap_set_id));
    h = fp_d(h, bm.hp);
    h = fp_d(h, bm.cs);
    h = fp_d(h, bm.od);
    h = fp_d(h, bm.ar);
    h = fp_d(h, bm.slider_multiplier);
    h = fp_d(h, bm.slider_tick_rate);
    h = fp_sv(h, bm.background);
    h = fp_sv(h, bm.video);
    h = fp_mix(h, bm.breaks.size());
    for (const auto& b : bm.breaks) {
        h = fp_mix(h, static_cast<uint32_t>(b.start));
        h = fp_mix(h, static_cast<uint32_t>(b.end));
    }
    h = fp_mix(h, bm.combo_colours.size());
    for (uint32_t c : bm.combo_colours) h = fp_mix(h, c);
    h = fp_mix(h, bm.timing_points.size());
    for (const auto& tp : bm.timing_points) {
        h = fp_d(h, tp.time);
        h = fp_d(h, tp.beat_length);
        h = fp_mix(h, static_cast<uint32_t>(tp.meter));
        h = fp_mix(h, static_cast<uint32_t>(tp.sample_set));
        h = fp_mix(h, static_cast<uint32_t>(tp.sample_index));
        h = fp_mix(h, static_cast<uint32_t>(tp.volume));
        h = fp_mix(h, tp.uninherited);
        h = fp_mix(h, tp.effects);
    }
    h = fp_mix(h, bm.hit_objects.size());
    for (const auto& o : bm.hit_objects) {
        h = fp_mix(h, static_cast<uint32_t>(o.x));
        h = fp_mix(h, static_cast<uint32_t>(o.y));
        h = fp_mix(h, o.type);
        h = fp_mix(h, o.hitsound);
        h = fp_mix(h, static_cast<uint32_t>(o.time));
        h = fp_mix(h, static_cast<uint32_t>(o.end_time));
        h = fp_mix(h, o.slider);
        h = fp_sv(h, o.hit_sample);
    }
    h = fp_mix(h, bm.sliders.size());
    for (const auto& s : bm.sliders) {
        h = fp_mix(h, s.point_begin);
        h = fp_mix(h, s.point_count);
        h = fp_mix(h, static_cast<uint32_t>(s.slides));
        h = fp_d(h, s.length);
        h = fp_mix(h, static_cast<uint8_t>(s.curve_type));
        h = fp_sv(h, s.edge_sounds);
        h = fp_sv(h, s.edge_sets);
    }
    h = fp_mix(h, bm.slider_points.size());
    for (const auto& p : bm.slider_points) {
        h = fp_mix(h, static_cast<uint32_t>(p.x));
        h = fp_mix(h, static_cast<uint32_t>(p.y));
    }
    h = fp_mix(h, bm.stats.fast_path_lines);
    h = fp_mix(h, bm.stats.slow_path_lines);
    h = fp_mix(h, bm.stats.malformed_lines);
    h = fp_mix(h, bm.stats.storyboard_lines);
    return h;
}

static const char* kSmallMap =
    "osu file format v11\r\n"
    "[General]\r\n"
    "AudioFilename: b.mp3\r\n"
    "Mode: 3\r\n"
    "[Metadata]\r\n"
    "Title:Second\r\n"
    "BeatmapID:42\r\n"
    "[Difficulty]\r\n"
    "HPDrainRate:3\r\n"
    "OverallDifficulty:7\r\n"
    "[TimingPoints]\r\n"
    "500,400,4,1,0,80,1,0\r\n"
    "[HitObjects]\r\n"
    "100,100,500,1,0\r\n"
    "256,192,1000,12,0,2000\r\n";

void test_parse_into_reuse() {
    printf("parse_into reuse\n");
    fosu::FileBuffer full = fosu::make_padded(kFullMap);
    fosu::FileBuffer small = fosu::make_padded(kSmallMap);
    fosu::FileBuffer empty = fosu::make_padded("osu file format v14\r\n");

    fosu::Beatmap bm;
    fosu::parse_into(full, bm);
    CHECK_EQ(fingerprint(bm), fingerprint(fosu::parse(full)));
    const size_t cap_objs = bm.hit_objects.capacity();

    // Shrinking reuse: stale fullmap state must not leak into the result.
    fosu::parse_into(small, bm);
    CHECK_EQ(fingerprint(bm), fingerprint(fosu::parse(small)));
    CHECK(bm.hit_objects.capacity() >= cap_objs);  // capacity kept
    CHECK(bm.title == "Second");
    CHECK(bm.background.empty());
    CHECK_EQ(bm.combo_colours.size(), 0u);

    // Growing reuse.
    fosu::parse_into(full, bm);
    CHECK_EQ(fingerprint(bm), fingerprint(fosu::parse(full)));

    // Near-empty file: everything back at defaults.
    fosu::parse_into(empty, bm);
    CHECK_EQ(fingerprint(bm), fingerprint(fosu::parse(empty)));
    CHECK_EQ(bm.hit_objects.size(), 0u);
    CHECK(std::abs(bm.stack_leniency - 0.7) < 1e-12);
    CHECK(bm.sample_set == "Normal");
}

void test_read_into_reuse() {
    printf("read_into reuse\n");
    char path[] = "/tmp/fosu_read_into_XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    const std::string big(10000, 'A');
    const std::string little(100, 'B');
    CHECK_EQ(write(fd, big.data(), big.size()),
             static_cast<ssize_t>(big.size()));
    close(fd);

    fosu::FileBuffer buf;
    CHECK(fosu::read_into(path, buf));
    CHECK_EQ(buf.size, big.size());
    CHECK(memcmp(buf.data.get(), big.data(), big.size()) == 0);
    const char* alloc0 = buf.data.get();
    const size_t cap0 = buf.capacity;

    FILE* f = fopen(path, "wb");
    fwrite(little.data(), 1, little.size(), f);
    fclose(f);

    CHECK(fosu::read_into(path, buf));
    CHECK_EQ(buf.size, little.size());
    CHECK(buf.data.get() == alloc0);  // allocation reused
    CHECK_EQ(buf.capacity, cap0);
    CHECK(memcmp(buf.data.get(), little.data(), little.size()) == 0);
    bool pad_zero = true;
    for (size_t i = 0; i < fosu::kBufferPadding; ++i)
        pad_zero &= buf.data[buf.size + i] == 0;
    CHECK(pad_zero);

    unlink(path);
    CHECK(!fosu::read_into("/nonexistent/fosu-no-such-file", buf));
}

#if FOSU_SIMD_X86
// Shape-cache equivalence: any line whose (comma, nondigit, len) key
// matches a cached shape must (a) be accepted by the reference parser and
// (b) convert bit-identically through tp_shape_convert.
void test_fuzz_tp_shape_cache() {
    printf("timing shape-cache fuzz\n");
    using namespace fosu::detail;
    const char* seeds[] = {
        "277,342.466666666667,4,2,1,60,1,0",
        "1234,-100,4,2,1,60,0,1",
        "56676,-83.3333333333333,4,2,1,45,0,1",
        "120,300,4,0,0,100,1,0",
        "1885,352.941176470588,4,1,0,70,1,0",
        "141476,-66.6666666666667,4,3,2,5,0,8",
    };
    uint64_t rng = 0x5EED5EEDULL;
    auto rnd = [&rng] {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return rng;
    };
    TpShapeCache cache{};
    char buf[64 + 80] = {};
    const char muts[] = "0123456789,.-x";
    int hits = 0, checked = 0;
    for (int it = 0; it < 300000; ++it) {
        const char* seed = seeds[rnd() % 6];
        size_t len = strlen(seed);
        memset(buf, 0, sizeof buf);
        memcpy(buf, seed, len);
        for (int m = int(rnd() % 4); m-- > 0;)
            buf[rnd() % len] = muts[rnd() % 14];
        if (rnd() % 8 == 0) cache = TpShapeCache{};  // section reset
        if (len > 64 || len < 15) continue;
        const __m256i a =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));
        const __m256i b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + 32));
        const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
        const uint64_t commas =
            (comma_mask32(a) | uint64_t(comma_mask32(b)) << 32) & line_mask;
        const uint64_t nondig =
            (nondigit_mask32(a) | uint64_t(nondigit_mask32(b)) << 32) &
            line_mask;
        fosu::TimingPoint ref{};
        TpGeom geom;
        const bool ref_ok = fast_parse_timing_point_masked(
            commas, nondig, buf, len, ref, &geom);
        const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
        if (tp_shape_match(row, commas, nondig, len, buf)) {
            ++hits;
            CHECK(ref_ok);  // cached shape implies structural validity
            fosu::TimingPoint got{};
            tp_shape_convert(row, buf, got);
            CHECK(memcmp(&got, &ref, sizeof(fosu::TimingPoint)) == 0);
            if (g_failures) {
                printf("  failing line: %s\n", buf);
                return;
            }
            ++checked;
        } else if (ref_ok) {
            tp_shape_insert(cache, commas, nondig, len, geom);
        }
    }
    printf("  shape-cache fuzz: %d hits verified\n", checked);
    CHECK(checked > 30000);
}
#endif

void test_selective_parsing() {
    printf("selective parsing\n");
    fosu::FileBuffer full = fosu::make_padded(kFullMap);
    const fosu::Beatmap ref = fosu::parse(full);

    for (int simd = 0; simd <= 1; ++simd) {
        // Difficulty only: correct values there, defaults elsewhere.
        fosu::Beatmap d = fosu::parse(
            full, {.use_simd = simd != 0,
                   .sections = fosu::kSectionDifficulty});
        CHECK(std::abs(d.od - ref.od) < 1e-12);
        CHECK(std::abs(d.ar - ref.ar) < 1e-12);
        CHECK(std::abs(d.hp - ref.hp) < 1e-12);
        CHECK(std::abs(d.cs - ref.cs) < 1e-12);
        CHECK(d.title.empty());
        CHECK_EQ(d.hit_objects.size(), 0u);
        CHECK_EQ(d.timing_points.size(), 0u);
        CHECK_EQ(d.combo_colours.size(), 0u);

        // Metadata + Difficulty.
        fosu::Beatmap md = fosu::parse(
            full, {.use_simd = simd != 0,
                   .sections =
                       fosu::kSectionMetadata | fosu::kSectionDifficulty});
        CHECK(md.title == ref.title);
        CHECK_EQ(md.beatmap_id, ref.beatmap_id);
        CHECK(std::abs(md.od - ref.od) < 1e-12);
        CHECK_EQ(md.hit_objects.size(), 0u);

        // HitObjects only: everything before it skipped, objects intact.
        fosu::Beatmap ho = fosu::parse(
            full, {.use_simd = simd != 0,
                   .sections = fosu::kSectionHitObjects});
        CHECK_EQ(ho.hit_objects.size(), ref.hit_objects.size());
        for (size_t i = 0; i < ho.hit_objects.size(); ++i) {
            CHECK_EQ(ho.hit_objects[i].x, ref.hit_objects[i].x);
            CHECK_EQ(ho.hit_objects[i].time, ref.hit_objects[i].time);
            CHECK_EQ(ho.hit_objects[i].type, ref.hit_objects[i].type);
        }
        CHECK(ho.title.empty());
        CHECK_EQ(ho.timing_points.size(), 0u);

        // Full mask == default behaviour.
        fosu::Beatmap all = fosu::parse(
            full, {.use_simd = simd != 0, .sections = fosu::kAllSections});
        CHECK_EQ(all.hit_objects.size(), ref.hit_objects.size());
        CHECK(all.title == ref.title);
        if (g_failures) {
            printf("  simd=%d\n", simd);
            return;
        }
    }
}

int main() {
    test_full_map();
    test_old_format();
    test_mania_hold();
    test_aspire_edge_cases();
    test_malformed();
    test_long_timing_offsets();
    test_parse_into_reuse();
    test_read_into_reuse();
    test_selective_parsing();
    test_fuzz_parse_double();
    test_fuzz_parse_coord();
#if FOSU_SIMD_X86
    test_fuzz_equivalence();
    test_fuzz_timing_point();
    test_fuzz_tp_shape_cache();
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
