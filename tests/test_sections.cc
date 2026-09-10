#include "support/equality.h"
#include "support/test.h"

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
      "SampleVolume: 73\r\n"
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
    CHECK(bm.sample_set == fosu::SampleSet::Soft);
    CHECK_EQ(bm.sample_volume, 73);
    CHECK(bm.widescreen_storyboard);
    CHECK(!bm.letterbox_in_breaks);
    CHECK(bm.bookmarks == "11240,22540");
    CHECK_EQ(bm.grid_size, 32);
    CHECK(bm.title == "Painters of the Tempest");
    CHECK(bm.creator == "cmyui");
    CHECK_EQ(bm.beatmap_id, 1193177);
    CHECK_EQ(bm.beatmap_set_id, 562454);
    CHECK(std::abs(bm.hp - 5.5) < 1e-9);
    CHECK_EQ(bm.ar, double(9.3f));
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
    CHECK_EQ(s.curve_type, fosu::CurveType::Bezier);
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
    if (simd)
      CHECK_EQ(bm.stats.fast_path_lines, 6u);
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
      "-48,192,1000,1,0\n"                            // negative x -> fallback
      "640,-24,2000,1,0\n"                            // negative y
      "256,192,-1000,1,0\n"                           // negative time
      "5120,192,3000,1,0\n"                           // 4-digit x
      "256.5,112.2,4000,1,0\n"                        // decimal coords
      "0,0,4294967290,1,0\n"                          // time > INT32_MAX
      "100,100,5000,2,0,B|-64:-32|700:512,1,600\n");  // negative ctrl points
  CHECK_EQ(bm.hit_objects.size(), 6u);
  CHECK_EQ(bm.hit_objects[0].time, -1000);
  CHECK_EQ(bm.hit_objects[1].x, -48);
  CHECK_EQ(bm.hit_objects[2].y, -24);
  CHECK_EQ(bm.hit_objects[3].x, 5120);
  CHECK_EQ(bm.hit_objects[4].x, 256);  // truncated
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
      "12,,123,4,0\n"          // empty field (index-alias trap)
      "256,192\n"              // truncated line
      "256,192,1000,2,0,B|\n"  // truncated slider
      "256,192,1000,1,0\n");   // one valid line
  CHECK_EQ(bm.hit_objects.size(), 1u);
  CHECK_EQ(bm.hit_objects[0].time, 1000);
  CHECK_EQ(bm.stats.malformed_lines, 5u);
}

static void test_omitted_sections_use_defaults() {
  for (bool simd : {false, true}) {
    auto bm = parse_str("[Metadata]\nTitle:Only metadata\n", simd);
    CHECK(bm.title == "Only metadata");
    CHECK(bm.audio_filename.empty());
    CHECK(bm.sample_set == fosu::SampleSet::Normal);
    CHECK_EQ(bm.preview_time, -1);
    CHECK_EQ(bm.grid_size, 0);
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
    fosu::Parser parser(simd ? fosu::internal::compiled_engine
                             : fosu_test::scalar_engine());
    const auto& bm =
        require_parse(parser.parse(input, {.sections = fosu::kSectionDifficulty}));
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
    fosu::Parser parser(simd ? fosu::internal::compiled_engine
                             : fosu_test::scalar_engine());
    const auto& bm = require_parse(parser.parse(
        input, {.sections = fosu::kSectionMetadata | fosu::kSectionDifficulty}));
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
    fosu::Parser parser(simd ? fosu::internal::compiled_engine
                             : fosu_test::scalar_engine());
    const auto& bm =
        require_parse(parser.parse(input, {.sections = fosu::kSectionHitObjects}));
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
    fosu::Parser parser(simd ? fosu::internal::compiled_engine
                             : fosu_test::scalar_engine());
    const auto& bm =
        require_parse(parser.parse(input, {.sections = fosu::kSectionDifficulty}));
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
    const auto& engine =
        simd ? fosu::internal::compiled_engine : fosu_test::scalar_engine();
    fosu::Parser explicit_parser(engine);
    fosu::Parser default_parser(engine);
    const auto& explicit_mask =
        require_parse(explicit_parser.parse(input, {.sections = fosu::kAllSections}));
    const auto& default_mask = require_parse(default_parser.parse(input));
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
  CHECK_EQ(map.sample_set, fosu::SampleSet::Soft);
  CHECK_EQ(map.title, "final: title");
  CHECK_EQ(map.title_unicode, "unicode");
  CHECK_EQ(map.background, "background.jpg");
  CHECK_EQ(map.video, "new.mp4");
  CHECK_EQ(map.breaks.size(), 2u);
  CHECK_EQ(map.breaks[0].start, 10);
  CHECK_EQ(map.breaks[1].end, 40);
  CHECK_EQ(map.stats.storyboard_lines, 1u);
}

static void test_long_event_lines() {
  for (size_t length : {63u, 64u, 65u, 95u, 96u, 97u, 200u}) {
    for (const auto ending : {"", "\n", "\r\n"}) {
      const std::string filename(length - 6, 'x');
      const auto map = parse_str("[Events]\n0,0,\"" + filename + "\"" + ending);
      CHECK_EQ(map.background, filename);
    }
    const auto map = parse_str("[Events]\n " + std::string(length, 'x') +
                               "\n[Metadata]\nTitle:after events\n");
    CHECK_EQ(map.stats.storyboard_lines, 1u);
    CHECK_EQ(map.title, "after events");
  }
}

static void test_timing_integer_widths() {
  for (int value : {9, 99, 999, 9999, 10000, 99999999, INT32_MAX}) {
    const auto field = std::to_string(value);
    const std::string input =
        "[TimingPoints]\n0,-100," + field + ",2," + field + "," + field + ",0," + field;
    for (bool simd : {false, true}) {
      fosu::Parser parser(simd ? fosu::internal::compiled_engine
                               : fosu_test::scalar_engine());
      const auto& map = require_parse(parser.parse(input.data(), input.size()));
      CHECK_EQ(map.timing_points.size(), 1u);
      CHECK_EQ(map.stats.malformed_lines, 0u);
      if (map.timing_points.size() != 1)
        continue;
      const auto& point = map.timing_points[0];
      CHECK_EQ(point.time, 0.0);
      CHECK_EQ(point.beat_length, -100.0);
      CHECK_EQ(point.meter, value);
      CHECK_EQ(point.sample_set, fosu::SampleSet::Soft);
      CHECK_EQ(point.sample_index, value);
      CHECK_EQ(point.volume, value);
      CHECK(!point.uninherited);
      CHECK_EQ(point.effects, static_cast<uint32_t>(value));
    }
  }
}

static void test_masked_timing_fallback() {
  // Exercise the general numeric rules, not just the fast editor shape.
  for (const std::string line :
       {"-1.5,500", "0,NaN,4,0,0,100,0,0", "0,500,0meter,0,0,100,1,0",
        "0,500,4,0,0,100,1anything,0,ignored", " 1 , 500 , 4 ,0,0,100,1,0",
        "0,500,4,0,0,100,1,", "0,500,,0,0,100,1,0", "0,500,4,0,0,100,1,bad",
        "0,NaN,4,0,0,100,1,0", "bad,500", "0,500,"}) {
    for (size_t length : {line.size(), size_t(63), size_t(64)}) {
      const std::string text = line + std::string(length - line.size(), ' ');
      const auto input = fosu::make_padded(text + ",outside\n");
      const char* p = input.data.get();
      uint64_t commas = 0;
      for (size_t i = 0; i < text.size(); ++i)
        if (p[i] == ',')
          commas |= 1ull << i;
      const auto expected = fosu::internal::parse_timing_point(p, p + text.size());
      const auto actual =
          fosu::internal::parse_timing_point<true>(p, p + text.size(), commas);
      CHECK_EQ(actual.has_value(), expected.has_value());
      if (actual && expected) {
        const auto& masked = *actual;
        const auto& scalar = *expected;
        CHECK_EQ(masked.time, scalar.time);
        CHECK(masked.beat_length == scalar.beat_length ||
              (std::isnan(masked.beat_length) && std::isnan(scalar.beat_length)));
        CHECK_EQ(masked.meter, scalar.meter);
        CHECK_EQ(masked.sample_set, scalar.sample_set);
        CHECK_EQ(masked.sample_index, scalar.sample_index);
        CHECK_EQ(masked.volume, scalar.volume);
        CHECK_EQ(masked.uninherited, scalar.uninherited);
        CHECK_EQ(masked.effects, scalar.effects);
      }
    }
  }
}

template <char Delimiter>
static void test_byte_scan_boundaries() {
  // Include non-vector-aligned inputs and a matching byte just outside end.
  for (size_t alignment = 0; alignment < 32; ++alignment) {
    for (size_t length = 0; length <= 97; ++length) {
      for (size_t position : {size_t(0), length / 2, length ? length - 1 : 0, length}) {
        std::string text(alignment + length + 1, 'x');
        text[alignment + length] = Delimiter;
        if (position < length)
          text[alignment + position] = Delimiter;
        const auto input = fosu::make_padded(text);
        const char* p = input.data.get() + alignment;
        CHECK_EQ(fosu::internal::find_byte<Delimiter>(p, p + length), p + position);
      }
    }
  }
}

static void test_event_filename_boundaries() {
  for (size_t timestamp_length : {0u, 30u, 31u, 32u, 64u}) {
    for (size_t filename_length : {0u, 1u, 30u, 31u, 32u, 63u, 64u, 96u}) {
      const std::string filename(filename_length, 'x');
      const std::string event =
          "Video," + std::string(timestamp_length, '0') + ",\"" + filename + "\"";
      for (const auto suffix : {"", ",0,0", "\nVideo,0"}) {
        const auto map = parse_str("[Events]\n" + event + suffix);
        CHECK_EQ(map.video, filename);
      }
    }
  }
}

static void test_section_skip_boundaries() {
  for (size_t padding : {0u, 30u, 31u, 32u, 63u, 64u, 95u}) {
    for (bool simd : {false, true}) {
      fosu::Parser parser(simd ? fosu::internal::compiled_engine
                               : fosu_test::scalar_engine());
      const std::string text = "[Unknown]\nvalue:" + std::string(padding, 'x') +
                               "[Metadata]\nTitle:ignored\n[Metadata]\nTitle:retained";
      auto& map = require_parse(
          parser.parse(text.data(), text.size(), {.sections = fosu::kSectionMetadata}));
      CHECK_EQ(map.title, "retained");
      const std::string missing = "[Unknown]\nvalue:[Metadata]";
      auto& empty = require_parse(parser.parse(missing.data(), missing.size(),
                                               {.sections = fosu::kSectionMetadata}));
      CHECK(empty.title.empty());
    }
  }
}

static void test_enum_contracts() {
  const std::string names[] = {"None", "Normal", "Soft", "Drum"};
  for (bool simd : {false, true}) {
    for (int value = 0; value < 4; ++value) {
      for (const auto& spelling :
           {names[value], std::to_string(value), " +" + std::to_string(value) + " ",
            "0" + std::to_string(value)}) {
        const auto map = parse_str("[General]\nSampleSet:" + spelling + "\n", simd);
        CHECK_EQ(map.stats.malformed_lines, 0u);
        CHECK_EQ(map.sample_set, static_cast<fosu::SampleSet>(value));
      }
    }
    for (const std::string value :
         {"-1", "4", "99", "2147483647", "Unknown", "Normal,Soft"}) {
      const auto map =
          parse_str("[General]\nSampleSet:Soft\nSampleSet:" + value + "\n", simd);
      CHECK_EQ(map.sample_set, fosu::SampleSet::Soft);
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
    for (const std::string value : {"-1", "4", "9", "99", "2147483647"}) {
      const auto map = parse_str(
          "[TimingPoints]\n0,500,4," + value + ",0,100,1,0\n1,500,4,2,0,100,1,0\n", simd);
      CHECK_EQ(map.stats.malformed_lines, 1u);
      CHECK_EQ(map.timing_points.size(), 1u);
      CHECK_EQ(map.timing_points[0].sample_set, fosu::SampleSet::Soft);
    }
    for (char value : {'B', 'C', 'L', 'P', 'X', 'b', '0'}) {
      const auto map = parse_str(
          std::string("[HitObjects]\n0,0,1,2,0,") + value + "|1:2,1,30\n0,0,2,1,0\n",
          simd);
      const bool valid = value == 'B' || value == 'C' || value == 'L' || value == 'P';
      CHECK_EQ(map.stats.malformed_lines, valid ? 0u : 1u);
      CHECK_EQ(map.hit_objects.size(), valid ? 2u : 1u);
      CHECK_EQ(map.sliders.size(), valid ? 1u : 0u);
      CHECK_EQ(map.slider_points.size(), valid ? 1u : 0u);
      if (valid)
        CHECK_EQ(static_cast<char>(map.sliders[0].curve_type), value);
    }
  }
}

static void test_header_field_failures_preserve_values() {
  for (bool simd : {false, true}) {
    const auto map = parse_str(
        "[General]\nPreviewTime:17\nPreviewTime:2147483648\n"
        "Mode:3\nMode:4\nSampleSet:Soft\nSampleSet:Normal,Soft\n"
        "Countdown:Normal,HalfSpeed\nUseSkinSprites:1suffix\n"
        "LetterboxInBreaks:1suffix\nApproachRate:9\n"
        "[Metadata]\nTitle:  text:with:colons \t\n"
        "BeatmapID:2147483647\nBeatmapID:2147483648\nUnknown:bad\n"
        "[Difficulty]\nOverallDifficulty:6\nApproachRate:8.5\n"
        "[Difficulty]\nApproachRate:bad\nOverallDifficulty:7\n",
        simd);
    CHECK_EQ(map.preview_time, 17);
    CHECK_EQ(map.mode, 3);
    CHECK_EQ(map.sample_set, fosu::SampleSet::Soft);
    CHECK_EQ(map.countdown, 3);
    CHECK(map.use_skin_sprites);
    CHECK(!map.letterbox_in_breaks);
    CHECK_EQ(map.title, "text:with:colons");
    CHECK_EQ(map.beatmap_id, INT32_MAX);
    CHECK_EQ(map.ar, 8.5);
    CHECK_EQ(map.od, 7);
    CHECK_EQ(map.stats.malformed_lines, 6u);

    const auto missing_ar = parse_str(
        "[Difficulty]\nApproachRate:bad\nOverallDifficulty:6\n"
        "[General]\nApproachRate:9\n"
        "[Difficulty]\nOverallDifficulty:7\n",
        simd);
    CHECK_EQ(missing_ar.ar, 7);
    CHECK_EQ(missing_ar.stats.malformed_lines, 1u);
  }
}

static void test_repeated_section_bodies() {
  const std::string text =
      "osu file format v14\n"
      "[General]\nAudioFilename:first.mp3\n[Editor]\nGridSize:8\n"
      "[Metadata]\nTitle:literal [Difficulty]\n[Difficulty]\nOverallDifficulty:7\n"
      "[Events]\n2,1,2\n[TimingPoints]\n0,500\n[Colours]\nCombo1:1,2,3\n"
      "[Future]\nTitle:ignored\n[HitObjects]\n1,2,3,1,0\n"
      "[General]\n[Editor]\n[Metadata]\nArtist:final\n[Difficulty]\n"
      "[Events]\n//comment\n2,3,4\n[TimingPoints]\n5,-100\n"
      "[Colours]\nCombo2:4,5,6\n[HitObjects]\n4,5,6,1,0";
  for (bool simd : {false, true}) {
    for (bool crlf : {false, true}) {
      std::string input;
      for (char c : text) {
        if (crlf && c == '\n')
          input += '\r';
        input += c;
      }
      const auto map = parse_str(input, simd);
      CHECK_EQ(map.audio_filename, "first.mp3");
      CHECK_EQ(map.grid_size, 8);
      CHECK_EQ(map.title, "literal [Difficulty]");
      CHECK_EQ(map.artist, "final");
      CHECK_EQ(map.ar, 7);
      CHECK_EQ(map.breaks.size(), 2u);
      CHECK_EQ(map.timing_points.size(), 2u);
      CHECK_EQ(map.combo_colours.size(), 2u);
      CHECK_EQ(map.hit_objects.size(), 2u);
      if (map.hit_objects.size() == 2)
        CHECK_EQ(map.hit_objects[1].time, 6);
      CHECK_EQ(map.stats.malformed_lines, 0u);
    }
  }
}

static void test_combo_colour_domain() {
  for (bool simd : {false, true}) {
    const auto map = parse_str(
        "[Colours]\nCombo1:1,2,3\nCombo0:4,5,6\nCombo9:7,8,9\n"
        "Combo:10,11,12\nCombo1suffix:13,14,15\nCombo-1:16,17,18\n"
        "Combo+8:19,20,21\nCombo01:22,23,24\nSliderBorder:25,26,27\n",
        simd);
    CHECK_EQ(map.combo_colours.size(), 3u);
    CHECK_EQ(map.combo_colours[0], 0x010203u);
    CHECK_EQ(map.combo_colours[1], 0x131415u);
    CHECK_EQ(map.combo_colours[2], 0x161718u);
    CHECK_EQ(map.stats.malformed_lines, 0u);

    const auto malformed = parse_str(
        "[Colours]\nCombo1:256,0,0\nCombo2:-1,0,0\nCombo3:1,2,3junk\n"
        "Combo4:1,2\nCombo5:1,2,3,4,5\nSliderBorder:no colour\n"
        "Combo6:1, 2 ,3,ignored alpha\nCombo7:4,5,6 // comment\n",
        simd);
    CHECK_EQ(malformed.combo_colours.size(), 2u);
    CHECK_EQ(malformed.combo_colours[0], 0x010203u);
    CHECK_EQ(malformed.combo_colours[1], 0x040506u);
    CHECK_EQ(malformed.stats.malformed_lines, 6u);
  }
}

static void test_legacy_rules() {
  static_assert(std::is_trivially_copyable_v<fosu::HitObject>);
  for (bool simd : {false, true}) {
    const auto late_mode = parse_str(
        "[Difficulty]\nCircleSize:18\nOverallDifficulty:20\nApproachRate:bad\n"
        "[General]\nMode:0\n[General]\nMode:3\n",
        simd);
    CHECK_EQ(late_mode.cs, 18);
    CHECK_EQ(late_mode.od, 10);
    CHECK_EQ(late_mode.ar, 10);
    const auto explicit_ar = parse_str(
        "[Difficulty]\nCircleSize:18\nApproachRate:20\n"
        "[Difficulty]\nApproachRate:bad\nOverallDifficulty:3\n"
        "[General]\nMode:3\n[General]\nMode:0\n",
        simd);
    CHECK_EQ(explicit_ar.cs, 10);
    CHECK_EQ(explicit_ar.ar, 10);
    CHECK_EQ(explicit_ar.od, 3);
    const auto map = parse_str(
        "osu file format v4\n[General]\nMode:3\nPreviewTime:100\n"
        "[Metadata]\n Title :\xE3\x80\x80trimmed\xC2\xA0\n"
        "[Difficulty]\nHPDrainRate:20\nCircleSize:32\nOverallDifficulty:9.3\n"
        "SliderMultiplier:8\nSliderTickRate:0.1\n"
        "[Editor]\nDistanceSpacing:-1\nTimelineZoom:-2\nBeatDivisor:100\n"
        "[TimingPoints]\n0,500\n[Events]\n2,150,100\n"
        "[HitObjects]\n10,20,200,1,0\n30,40,0,8,0,-10\n"
        "50,60,50,2,0,B|100:100,0,0\n70,80,100,53,0\n"
        "90,100,100,49,0\n110,120,20,128,0,10\n130,140,201,1,0\n",
        simd);
    CHECK_EQ(map.title, "trimmed");
    CHECK_EQ(map.hp, 10);
    CHECK_EQ(map.cs, 18);
    CHECK_EQ(map.ar, double(9.3f));
    CHECK_EQ(map.slider_multiplier, 3.6);
    CHECK_EQ(map.slider_tick_rate, 0.5);
    CHECK_EQ(map.distance_spacing, 0);
    CHECK_EQ(map.timeline_zoom, 0);
    CHECK_EQ(map.beat_divisor, 64);
    CHECK_EQ(map.preview_time, 124);
    CHECK_EQ(map.timing_points[0].time, 24);
    CHECK_EQ(map.breaks[0].start, 174);
    CHECK_EQ(map.breaks[0].end, 174);
    const auto objects = map.hit_objects;
    CHECK_EQ(objects[0].time, 24);
    CHECK_EQ(objects[0].end_time, 24);
    CHECK_EQ(objects[0].x, 256);
    CHECK_EQ(objects[0].y, 192);
    CHECK_EQ(objects[1].time, 44);
    CHECK_EQ(objects[1].end_time, 68);
    CHECK(objects[2].new_combo);  // Slider follows spinner in source order.
    CHECK_EQ(map.sliders[objects[2].slider].slides, 1);
    CHECK_EQ(objects[3].x, 70);  // Equal timestamps preserve input order.
    CHECK_EQ(objects[4].x, 90);
    CHECK_EQ(objects[3].combo_skip, 3);
    CHECK_EQ(objects[4].combo_skip, 0);
    CHECK(!objects[4].new_combo);
    CHECK(objects[5].new_combo);  // First source object and first after break.
    CHECK(!objects[6].new_combo);

    const auto breaks = parse_str(
        "osu file format v14\n[Events]\n2,0,200\n2,0,100\n2,0,350\n"
        "[HitObjects]\n0,0,100,1,0\n0,0,200,1,0\n0,0,300,1,0\n"
        "0,0,400,1,0\n",
        simd);
    CHECK(breaks.hit_objects[0].new_combo);
    CHECK(!breaks.hit_objects[1].new_combo);  // Break ends are exclusive.
    CHECK(breaks.hit_objects[2].new_combo);   // Earlier breaks cannot move backward.
    CHECK(breaks.hit_objects[3].new_combo);
  }
}

int main() {
  test_legacy_rules();
  test_combo_colour_domain();
  test_header_field_failures_preserve_values();
  test_repeated_section_bodies();
  test_enum_contracts();
  test_byte_scan_boundaries<','>();
  test_byte_scan_boundaries<':'>();
  test_byte_scan_boundaries<'\n'>();
  test_byte_scan_boundaries<'\0'>();
  test_event_filename_boundaries();
  test_section_skip_boundaries();
  test_long_event_lines();
  test_masked_timing_fallback();
  test_timing_integer_widths();
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
