#include "support/test.hpp"
#include "support/equality.hpp"

static void test_all_sections() {
    const char* input =
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
    for (bool simd : {true, false}) {
        auto bm = parse_str(input, simd);
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
#if FOSU_SIMD
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
#if FOSU_SIMD
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

static void test_omitted_sections_use_defaults() {
    for (bool simd : {false, true}) {
        auto bm = parse_str("[Metadata]\nTitle:Only metadata\n", simd);
        CHECK(bm.title == "Only metadata");
        CHECK(bm.audio_filename.empty());
        CHECK(bm.sample_set == "Normal");
        CHECK_EQ(bm.preview_time, -1);
        CHECK_EQ(bm.grid_size, 4);
        CHECK_EQ(bm.hp, 5);
        CHECK_EQ(bm.cs, 5);
        CHECK_EQ(bm.od, 5);
        CHECK_EQ(bm.ar, 5);
        CHECK(bm.background.empty() && bm.video.empty());
        CHECK(bm.timing_points.empty() && bm.breaks.empty());
        CHECK(bm.combo_colours.empty() && bm.hit_objects.empty());
        CHECK(bm.sliders.empty() && bm.slider_points.empty());
        CHECK_EQ(bm.stats.malformed_lines, 0u);
    }
}

static void test_difficulty_selection_skips_other_sections() {
    auto input = fosu::make_padded(
        "[Metadata]\nTitle:Unrequested\n"
        "[Difficulty]\nHPDrainRate:3\nCircleSize:4\nOverallDifficulty:7\nApproachRate:8\n"
        "[TimingPoints]\n0,500\n"
        "[Colours]\nCombo1:255,0,0\n"
        "[HitObjects]\n64,96,1000,1,0\n");
    for (bool simd : {false, true}) {
        fosu::Parser parser(simd ? fosu::internal::native_engine : fosu::internal::scalar_engine);
        const auto& bm = require_parse(parser.parse(
            input,
            {.sections = fosu::kSectionDifficulty}));
        CHECK_EQ(bm.hp, 3);
        CHECK_EQ(bm.cs, 4);
        CHECK_EQ(bm.od, 7);
        CHECK_EQ(bm.ar, 8);
        CHECK(bm.title.empty());
        CHECK(bm.timing_points.empty() && bm.combo_colours.empty());
        CHECK(bm.hit_objects.empty());
    }
}

static void test_metadata_and_difficulty_selection() {
    auto input = fosu::make_padded(
        "[General]\nAudioFilename:unrequested.mp3\n"
        "[Metadata]\nTitle:Selected metadata\nBeatmapID:42\n"
        "[Difficulty]\nOverallDifficulty:6\n"
        "[HitObjects]\n128,192,2000,1,0\n");
    for (bool simd : {false, true}) {
        fosu::Parser parser(simd ? fosu::internal::native_engine : fosu::internal::scalar_engine);
        const auto& bm = require_parse(parser.parse(
            input, {                .sections = fosu::kSectionMetadata |
                            fosu::kSectionDifficulty}));
        CHECK(bm.title == "Selected metadata");
        CHECK_EQ(bm.beatmap_id, 42);
        CHECK_EQ(bm.od, 6);
        CHECK_EQ(bm.ar, 6);
        CHECK(bm.audio_filename.empty());
        CHECK(bm.hit_objects.empty());
    }
}

static void test_hitobject_selection_skips_preceding_sections() {
    auto input = fosu::make_padded(
        "[Metadata]\nTitle:Skipped metadata\n"
        "[TimingPoints]\n100,400\n"
        "[HitObjects]\n32,48,3000,1,2\n256,192,4000,8,0,5000\n");
    for (bool simd : {false, true}) {
        fosu::Parser parser(simd ? fosu::internal::native_engine : fosu::internal::scalar_engine);
        const auto& bm = require_parse(parser.parse(
            input,
            {.sections = fosu::kSectionHitObjects}));
        CHECK(bm.title.empty() && bm.timing_points.empty());
        CHECK_EQ(bm.hit_objects.size(), 2u);
        CHECK_EQ(bm.hit_objects[0].x, 32);
        CHECK_EQ(bm.hit_objects[0].time, 3000);
        CHECK_EQ(bm.hit_objects[0].hitsound, 2u);
        CHECK(bm.hit_objects[1].is_spinner());
        CHECK_EQ(bm.hit_objects[1].end_time, 5000);
    }
}

static void test_selected_missing_section_uses_defaults() {
    auto input = fosu::make_padded(
        "[Metadata]\nTitle:No difficulty section\n"
        "[HitObjects]\n96,64,6000,1,0\n");
    for (bool simd : {false, true}) {
        fosu::Parser parser(simd ? fosu::internal::native_engine : fosu::internal::scalar_engine);
        const auto& bm = require_parse(parser.parse(
            input,
            {.sections = fosu::kSectionDifficulty}));
        CHECK_EQ(bm.hp, 5);
        CHECK_EQ(bm.cs, 5);
        CHECK_EQ(bm.od, 5);
        CHECK_EQ(bm.ar, 5);
        CHECK(bm.title.empty() && bm.hit_objects.empty());
        CHECK_EQ(bm.stats.malformed_lines, 0u);
    }
}

static void test_all_section_mask_matches_default() {
    auto input = fosu::make_padded(
        "[General]\nMode:3\n"
        "[Metadata]\nTitle:Explicit all sections\n"
        "[Events]\n2,100.25,200.75\n"
        "[TimingPoints]\n300,250\n"
        "[HitObjects]\n320,192,7000,128,0,7500:0:0:0:0:\n");
    for (bool simd : {false, true}) {
        const auto& engine = simd ? fosu::internal::native_engine : fosu::internal::scalar_engine;
        fosu::Parser explicit_parser(engine);
        fosu::Parser default_parser(engine);
        const auto& explicit_mask = require_parse(explicit_parser.parse(
            input, {.sections = fosu::kAllSections}));
        const auto& default_mask = require_parse(default_parser.parse(
            input));
        CHECK_EQ(canonical(explicit_mask), canonical(default_mask));
    }
}

static void test_exact_keys_and_event_aliases() {
  const auto map = parse_str(
      "[General]\nCountdown:Normal,HalfSpeed\nSampleSet:Soft\n"
      "[Metadata]\nTitle:\tkept \nTitleUnicode : unicode\n"
      "TitleExtra:ignored\n Title:ignored\nTitle\t: final: title \n"
      "[MetadataExtra]\nTitle:ignored section\n"
      "[Events]\n0,0,\"background.jpg\"\n"
      "1,0,\"old.mp4\"\nVideo,0,\"new.mp4\"\n"
      "2,10,20\nBreak,30,40\nVideoExtra,0,\"ignored.mp4\"\n");
  CHECK_EQ(map.countdown, 3);
  CHECK_EQ(map.sample_set, "Soft");
  CHECK_EQ(map.title, "final: title ");
  CHECK_EQ(map.title_unicode, "unicode");
  CHECK_EQ(map.background, "background.jpg");
  CHECK_EQ(map.video, "new.mp4");
  CHECK_EQ(map.breaks.size(), 2u);
  CHECK_EQ(map.breaks[0].start, 10);
  CHECK_EQ(map.breaks[1].end, 40);
  CHECK_EQ(map.stats.storyboard_lines, 1u);
}

int main() {
  test_exact_keys_and_event_aliases();
  test_all_sections();
  test_old_format();
  test_mania_hold();
  test_aspire_edge_cases();
  test_malformed();
  test_long_timing_offsets();
  test_omitted_sections_use_defaults();
  test_difficulty_selection_skips_other_sections();
  test_metadata_and_difficulty_selection();
  test_hitobject_selection_skips_preceding_sections();
  test_selected_missing_section_uses_defaults();
  test_all_section_mask_matches_default();
  return test_result();
}
