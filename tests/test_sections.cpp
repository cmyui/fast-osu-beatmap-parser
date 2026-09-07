#include "support/test.hpp"
#include "support/maps.hpp"

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
            "2123456789,-50,4,2,1,60,0,0\n",
            simd);
        CHECK_EQ(bm.timing_points.size(), 2u);
        CHECK(bm.timing_points[0].time == 123456789.0);
        CHECK(bm.timing_points[1].time == 2123456789.0);
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
    CHECK_EQ(bm.hit_objects.size(), 6u);
    CHECK_EQ(bm.hit_objects[0].x, -48);
    CHECK_EQ(bm.hit_objects[1].y, -24);
    CHECK_EQ(bm.hit_objects[2].time, -1000);
    CHECK_EQ(bm.hit_objects[3].x, 5120);
    CHECK_EQ(bm.hit_objects[4].x, 256);   // truncated
    CHECK_EQ(bm.hit_objects[4].y, 112);
    const auto& s = bm.sliders[bm.hit_objects[5].slider];
    CHECK_EQ(bm.slider_points[s.point_begin].x, -64);
    CHECK_EQ(bm.slider_points[s.point_begin].y, -32);
    CHECK_EQ(bm.stats.malformed_lines, 1u);
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
    test_selective_parsing();
    return test_result();
}
