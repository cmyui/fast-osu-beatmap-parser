// Mods applied while parsing: Easy and Hard Rock change difficulty settings
// and positions; Double Time, Nightcore and Half Time change the clock rate.
#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/mods.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <tests/support/scalar_engine.h>
#include <tests/support/test.h>

#include <string>
#include <utility>

using fosu::Mods;

static void test_hard_rock_raises_difficulty_and_flips_vertically() {
  const std::string input =
      "osu file format v14\n[General]\nMode:0\n"
      "[Difficulty]\nHPDrainRate:4\nCircleSize:4\nOverallDifficulty:4\n"
      "ApproachRate:4\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
      "[HitObjects]\n100,100,1000,2,0,L|200:150,1,100\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd, {.mods = Mods::HardRock});
    CHECK_EQ(map.hp, 4 * 1.4);
    CHECK_EQ(map.cs, 4 * 1.3);
    CHECK_EQ(map.od, 4 * 1.4);
    CHECK_EQ(map.ar, 4 * 1.4);
    CHECK_EQ(map.hit_objects[0].y, 384 - 100);
    CHECK_EQ(map.slider_points[0].y, 384 - 150);
  }
}

static void test_hard_rock_caps_difficulty_at_10() {
  const std::string input =
      "[General]\nMode:0\n[Difficulty]\nHPDrainRate:8\nCircleSize:8\n"
      "OverallDifficulty:8\nApproachRate:8\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd, {.mods = Mods::HardRock});
    CHECK_EQ(map.hp, 10);
    CHECK_EQ(map.cs, 10);
    CHECK_EQ(map.od, 10);
    CHECK_EQ(map.ar, 10);
  }
}

static void test_easy_halves_difficulty() {
  const std::string input =
      "[General]\nMode:0\n[Difficulty]\nHPDrainRate:4\nCircleSize:4\n"
      "OverallDifficulty:4\nApproachRate:4\nSliderMultiplier:1\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd, {.mods = Mods::Easy});
    CHECK_EQ(map.hp, 2);
    CHECK_EQ(map.cs, 2);
    CHECK_EQ(map.od, 2);
    CHECK_EQ(map.ar, 2);
    CHECK_EQ(map.slider_multiplier, 1);
  }
}

// osu!taiko has no circle size or approach rate to change, and scales
// slider velocity instead of flipping positions.
static void test_taiko_difficulty_mods_follow_taiko_rules() {
  const std::string input =
      "[General]\nMode:1\n[Difficulty]\nHPDrainRate:4\nCircleSize:4\n"
      "OverallDifficulty:4\nApproachRate:4\nSliderMultiplier:1\n"
      "[HitObjects]\n100,100,1000,1,0\n";
  for (bool simd : {false, true}) {
    const auto easy = parse_str(input, simd, {.mods = Mods::Easy});
    CHECK_EQ(easy.hp, 2);
    CHECK_EQ(easy.od, 2);
    CHECK_EQ(easy.slider_multiplier, 0.8);
    const auto hard_rock = parse_str(input, simd, {.mods = Mods::HardRock});
    CHECK_EQ(hard_rock.hp, 4 * 1.4);
    CHECK_EQ(hard_rock.cs, 4);
    CHECK_EQ(hard_rock.od, 4 * 1.4);
    CHECK_EQ(hard_rock.ar, 4);
    CHECK_EQ(hard_rock.slider_multiplier, 1.4 * 4 / 3);
    CHECK_EQ(hard_rock.hit_objects[0].y, 100);
  }
}

static void test_catch_and_mania_reject_difficulty_mods() {
  for (const char* mode : {"2", "3"}) {
    const auto input = std::string("[General]\nMode:") + mode +
                       "\n[Difficulty]\nCircleSize:4\n";
    for (const auto* engine :
         {&fosu::internal::compiled_engine, &fosu_test::scalar_engine()}) {
      fosu::Parser parser(*engine);
      CHECK(!parser.parse(input, {.mods = Mods::Easy}));
      CHECK(!parser.parse(input, {.mods = Mods::HardRock}));
    }
  }
}

// Times divide by the clock rate; beat lengths of uninherited points too,
// while inherited multipliers stay.
static void test_rate_mods_scale_every_time() {
  for (const auto [mods, rate] :
       {std::pair{Mods::DoubleTime, 1.5}, std::pair{Mods::Nightcore, 1.5},
        std::pair{Mods::HalfTime, 0.75}}) {
    for (int mode : {0, 1, 2, 3}) {
      const auto object = mode == 3 ? "0,0,900,128,0,1500:0:0:0:0:"
                                    : "0,0,900,8,0,1500,0:0:0:0:";
      const auto input = "[General]\nMode:" + std::to_string(mode) +
                         "\n[Events]\n2,1500,2000\n[TimingPoints]\n300,600\n"
                         "600,-50,4,0,0,100,0,0\n[HitObjects]\n" +
                         object + "\n";
      for (bool simd : {false, true}) {
        const auto map = parse_str(input, simd, {.mods = mods});
        CHECK_EQ(map.hit_objects[0].time, 900 / rate);
        CHECK_EQ(map.hit_objects[0].end_time, 1500 / rate);
        CHECK_EQ(map.timing_points[0].time, 300 / rate);
        CHECK_EQ(map.timing_points[1].time, 600 / rate);
        CHECK_EQ(map.timing_points[0].beat_length, 600 / rate);
        CHECK_EQ(map.timing_points[1].beat_length, -50);
        CHECK_EQ(map.breaks[0].start, 1500 / rate);
        CHECK_EQ(map.breaks[0].end, 2000 / rate);
      }
    }
  }
}

// Calculated values come from the unscaled map, then scale with it.
static void test_rate_mods_scale_calculated_slider_values() {
  const std::string input =
      "osu file format v14\n[Difficulty]\nSliderMultiplier:1\n"
      "[TimingPoints]\n0,500\n[HitObjects]\n100,100,1000,2,0,L|200:150,1,100\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd,
                               {.calculate_slider_events = true,
                                .mods = Mods::HardRock | Mods::DoubleTime});
    CHECK_EQ(map.hit_objects[0].end_time, 1500 / 1.5);
    const auto events = map.slider_events[0];
    CHECK_EQ(events.size(), 3u);
    CHECK_EQ(events[0].time, 1000 / 1.5);
    CHECK_EQ(events[1].time, 1464 / 1.5);
    CHECK_EQ(events[2].time, 1500 / 1.5);
    for (const auto& event : events)
      CHECK_EQ(event.span_start_time, 1000 / 1.5);
  }
}

static void test_invalid_mod_options_fail_the_parse() {
  const std::string input =
      "[General]\nMode:0\n[Difficulty]\nCircleSize:4\n[HitObjects]\n0,0,0,1,"
      "0\n";
  for (const auto* engine :
       {&fosu::internal::compiled_engine, &fosu_test::scalar_engine()}) {
    fosu::Parser parser(*engine);
    for (const auto mods :
         {Mods::Easy | Mods::HardRock, Mods::DoubleTime | Mods::HalfTime,
          Mods::Nightcore | Mods::HalfTime, static_cast<Mods>(1u << 30)})
      CHECK(!parser.parse(input, {.mods = mods}));
    // Difficulty mods need the General and Difficulty sections they adjust.
    CHECK(!parser.parse(
        input, {.sections = fosu::kSectionHitObjects, .mods = Mods::HardRock}));
    // A failed parse leaves the parser usable.
    CHECK(parser.parse(input, {.mods = Mods::HardRock}));
  }
}

int main() {
  test_hard_rock_raises_difficulty_and_flips_vertically();
  test_hard_rock_caps_difficulty_at_10();
  test_easy_halves_difficulty();
  test_taiko_difficulty_mods_follow_taiko_rules();
  test_catch_and_mania_reject_difficulty_mods();
  test_rate_mods_scale_every_time();
  test_rate_mods_scale_calculated_slider_values();
  test_invalid_mod_options_fail_the_parse();
  return test_result();
}
