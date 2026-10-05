// Where osu!stable and lazer read a map differently, each client's rule.
#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/enums.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <fosu/slider_path.h>
#include <fosu/types.h>
#include <tests/support/scalar_engine.h>
#include <tests/support/test.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using fosu::Client;

static constexpr fosu::ParseOptions     kStable{.client = Client::Stable};

static const fosu::ParsingEngine* const kEngines[] = {
    &fosu::internal::compiled_engine, &fosu_test::scalar_engine()};

// The line that makes `client` refuse the map, or 0 if it loads.
static fosu::u32 unloadable_line(const std::string& input, Client client) {
  fosu::Parser parser;
  if (parser.parse(input, {.client = client}))
    return 0;
  CHECK(parser.error().code == fosu::ParseErrorCode::Unloadable);
  return parser.error().line;
}

static const std::string kTimingAndCircle =
    "[TimingPoints]\n0,500\n[HitObjects]\n64,64,100,1,0\n";

// stable reads the version from the first line only; lazer from the first
// non-blank line, trimmed.
static void test_version_line_position() {
  for (const char* start :
       {" \r\nosu file format v9\r\n", "\nosu file format v9\n",
        " osu file format v9\n"}) {
    const auto input = start + kTimingAndCircle;
    CHECK_EQ(parse_str(input, true, kStable).format_version, 14);
    CHECK_EQ(parse_str(input, true, kLazer).format_version, 9);
  }
}

// An unreadable version number fails the map, wherever the client looks for
// it. stable reads Int32.Parse's full range; lazer its symmetric one.
static void test_unreadable_version_fails_the_map() {
  for (const auto client : {Client::Stable, Client::Lazer}) {
    CHECK_EQ(unloadable_line("osu file format vx\n" + kTimingAndCircle, client),
             1u);
    CHECK_EQ(
        unloadable_line("osu file format v5v\n" + kTimingAndCircle, client),
        1u);
  }
  const auto later = "\n\nosu file format v1x\n" + kTimingAndCircle;
  CHECK_EQ(unloadable_line(later, Client::Lazer), 3u);
  CHECK_EQ(unloadable_line(later, Client::Stable), 0u);

  const auto minimum = "osu file format v-2147483648\n" + kTimingAndCircle;
  CHECK_EQ(parse_str(minimum, true, kStable).format_version, INT32_MIN);
  CHECK_EQ(unloadable_line(minimum, Client::Lazer), 1u);
}

// stable consumes the first line as its version line even when it is a
// section header. Its header reader then never enters that section, though
// the object reader does.
static void test_first_line_header() {
  const auto general = "[General]\nMode:1\n" + kTimingAndCircle;
  CHECK_EQ(parse_str(general, true, kStable).mode, 0);
  CHECK_EQ(parse_str(general, true, kLazer).mode, 1);
  const auto objects = std::string("[HitObjects]\n64,64,100,1,0\n");
  CHECK_EQ(parse_str(objects, true, kStable).hit_objects.size(), 1u);
  CHECK_EQ(parse_str(objects, true, kLazer).hit_objects.size(), 1u);
}

// Lazer reads lines before the first header as [General]; stable ignores
// them.
static void test_lines_before_the_first_header() {
  const auto input =
      "osu file format v14\nStackLeniency:0.2\n" + kTimingAndCircle;
  CHECK_EQ(parse_str(input, true, kStable).stack_leniency, double(0.7f));
  CHECK_EQ(parse_str(input, true, kLazer).stack_leniency, double(0.2f));
}

// A header naming no section starts [General] in lazer and leaves the
// current section in effect in stable. Each also knows names fosu ignores:
// lazer Variables, Fonts, CatchTheBeat and Mania; stable Variables, Unknown
// and All.
static void test_unknown_section_names() {
  const auto with_header = [](const char* header) {
    return "osu file format v14\n[Difficulty]\nOverallDifficulty:3\n" +
           std::string(header) + "\nOverallDifficulty:4\nStackLeniency:0.2\n" +
           kTimingAndCircle;
  };
  struct Case {
    const char* header;
    double      stable_od;
    double      lazer_stack_leniency;
  };
  for (const auto& test : {
           Case{"[Future]", 4, 0.2f},
           Case{"[Fonts]", 4, 0.7f},
           Case{"[Variables]", 3, 0.7f},
           Case{"[Unknown]", 3, 0.2f},
           Case{"[All]", 3, 0.2f},
       }) {
    const auto stable = parse_str(with_header(test.header), true, kStable);
    CHECK_EQ(stable.od, test.stable_od);
    CHECK_EQ(stable.stack_leniency, double(0.7f));
    const auto lazer = parse_str(with_header(test.header), true, kLazer);
    CHECK_EQ(lazer.od, 3);
    CHECK_EQ(lazer.stack_leniency, test.lazer_stack_leniency);
  }
  for (bool simd : {false, true}) {
    const auto input = "osu file format v14\n" + kTimingAndCircle +
                       "[Future]\n64,64,200,1,0\n";
    CHECK_EQ(parse_str(input, simd, kStable).hit_objects.size(), 2u);
    CHECK_EQ(parse_str(input, simd, kLazer).hit_objects.size(), 1u);
  }
}

// stable reads any line starting with '[' as a header and trims every
// bracket from both ends. fosu follows its object reader in trimming no
// whitespace; its header reader also accepts "[Difficulty] ". Lazer needs
// "[name]" after trimming the line's end.
static void test_header_spelling() {
  struct Case {
    const char* header;
    double      stable_od;
    double      lazer_od;
  };
  for (const auto& test : {
           Case{"[Difficulty]", 4, 4},
           Case{"[[Difficulty]]", 4, 5},
           Case{"[Difficulty", 4, 5},
           Case{"[Difficulty] ", 5, 4},
       }) {
    const auto input = "osu file format v14\n" + std::string(test.header) +
                       "\nOverallDifficulty:4\n" + kTimingAndCircle;
    CHECK_EQ(parse_str(input, true, kStable).od, test.stable_od);
    CHECK_EQ(parse_str(input, true, kLazer).od, test.lazer_od);
  }
}

// stable's header reader never leaves [HitObjects], so later General,
// Metadata, Difficulty and TimingPoints sections are ignored. Its object
// reader still reads later Events, Colours and HitObjects.
static void test_header_sections_after_hit_objects() {
  const auto input = "osu file format v14\n" + kTimingAndCircle +
                     "[Difficulty]\nOverallDifficulty:4\n"
                     "[TimingPoints]\n50,250\n[Metadata]\nTitle:late\n"
                     "[Events]\n2,300,1000\n[Colours]\nCombo1:1,2,3\n"
                     "[HitObjects]\n64,64,2000,1,0\n";
  for (bool simd : {false, true}) {
    const auto stable = parse_str(input, simd, kStable);
    CHECK_EQ(stable.od, 5);
    CHECK_EQ(stable.timing_points.size(), 1u);
    CHECK(stable.title.empty());
    CHECK_EQ(stable.breaks.size(), 1u);
    CHECK_EQ(stable.combo_colours.size(), 1u);
    CHECK_EQ(stable.hit_objects.size(), 2u);
    const auto lazer = parse_str(input, simd, kLazer);
    CHECK_EQ(lazer.od, 4);
    CHECK_EQ(lazer.timing_points.size(), 2u);
    CHECK_EQ(lazer.title, "late");
    CHECK_EQ(lazer.hit_objects.size(), 2u);
  }
}

// stable cannot load a map with a hit object it fails to read; lazer skips
// the line. Both engines report the same line, whichever path read it.
static void test_unreadable_hit_object_fails_stable() {
  for (const char* line :
       {"64,64,x,1,0", "64,64,200,1,0,bad", "64,64,200,2,0,L|1:x,1,10",
        "64,64,200,8,0,x", "64,64"}) {
    for (const char* ending : {"\n", "\r\n", "\r"}) {
      std::string input;
      for (const std::string part :
           {"osu file format v14", "[TimingPoints]", "0,500", "[HitObjects]",
            "64,64,100,1,0", line, "64,64,300,1,0"})
        input += part + ending;
      for (const auto* engine : kEngines) {
        fosu::Parser parser(*engine);
        CHECK(!parser.parse(input));
        CHECK(parser.error().code == fosu::ParseErrorCode::Unloadable);
        CHECK_EQ(parser.error().line, 6u);
        const auto& lazer = require_parse(parser.parse(input, kLazer));
        CHECK_EQ(lazer.hit_objects.size(), 2u);
        CHECK_EQ(lazer.stats.malformed_lines, 1u);
      }
    }
  }
}

// stable ignores a hit object whose type names no kind, and skips lines
// indented with ' ' or '_' and lines starting with '['. fosu still counts
// the first as malformed. Lazer reads an indented line and rejects the
// others.
static void test_hit_object_lines_stable_skips() {
  struct Case {
    const char* line;
    size_t      stable_objects, stable_malformed;
    size_t      lazer_objects, lazer_malformed;
  };
  for (const auto& test : {
           Case{"64,64,200,4,0", 2, 1, 2, 1},
           Case{" 64,64,200,1,0", 2, 0, 3, 0},
           Case{"_64,64,200,1,0", 2, 0, 2, 1},
           Case{"[64,64,200,1,0", 2, 0, 2, 1},
       }) {
    const auto input =
        "osu file format v14\n[TimingPoints]\n0,500\n"
        "[HitObjects]\n64,64,100,1,0\n" +
        std::string(test.line) + "\n64,64,300,1,0\n";
    for (bool simd : {false, true}) {
      const auto stable = parse_str(input, simd, kStable);
      CHECK_EQ(stable.hit_objects.size(), test.stable_objects);
      CHECK_EQ(stable.stats.malformed_lines, test.stable_malformed);
      const auto lazer = parse_str(input, simd, kLazer);
      CHECK_EQ(lazer.hit_objects.size(), test.lazer_objects);
      CHECK_EQ(lazer.stats.malformed_lines, test.lazer_malformed);
    }
  }
}

// A line only fosu's own rules reject, such as an unknown curve type both
// clients read as Catmull, never makes a map unloadable. A slider that ends
// at that type is one stable cannot read either: its repeat count is missing.
static void test_policy_rejection_keeps_stable_map_loadable() {
  const auto slider = [](const char* curve) {
    return "osu file format v14\n[TimingPoints]\n0,500\n[HitObjects]\n"
           "64,64,100,2,0," +
           std::string(curve) + "\n64,64,300,1,0\n";
  };
  for (const char* curve : {"X|100:100,1,50", "X,1,50"}) {
    for (bool simd : {false, true}) {
      const auto map = parse_str(slider(curve), simd, kStable);
      CHECK_EQ(map.hit_objects.size(), 1u);
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
  CHECK_EQ(unloadable_line(slider("X"), Client::Stable), 5u);
}

// Like storyboard commands, stable skips colour lines indented with ' ' or
// '_'. Lazer trims them.
static void test_indented_colours() {
  const auto input =
      "osu file format v14\n[Colours]\n Combo1 : 1,2,3\n"
      "Combo2:4,5,6\n" +
      kTimingAndCircle;
  CHECK_EQ(parse_str(input, true, kStable).combo_colours.size(), 1u);
  CHECK_EQ(parse_str(input, true, kLazer).combo_colours.size(), 2u);
}

// Before v128, stable reads a one-character token as the curve type of the
// whole slider, the last one winning, and ignores one naming no type; a
// longer token must be a point. Lazer starts a segment at each type (see
// test_slider_paths) and rejects a token naming none.
static void test_curve_type_tokens() {
  const auto slider = [](const char* curve) {
    return "osu file format v14\n[TimingPoints]\n0,500\n[HitObjects]\n"
           "0,0,1000,2,0," +
           std::string(curve) + ",1,0\n";
  };
  for (bool simd : {false, true}) {
    const auto stable = parse_str(slider("B|100:0|L|100:100|0:100"), simd,
                                  {.calculate_slider_paths = true});
    CHECK(stable.sliders[0].curve_type == fosu::CurveType::Linear);
    CHECK_EQ(stable.slider_points.size(), 3u);
    CHECK(stable.slider_segments.empty());
    CHECK_EQ(stable.slider_paths[0].distance(), 300);
    const auto lazer =
        parse_str(slider("B|100:0|L|100:100|0:100"), simd, kLazer);
    CHECK(lazer.sliders[0].curve_type == fosu::CurveType::Bezier);
    CHECK_EQ(lazer.slider_segments.size(), 2u);

    for (const char* curve : {"B|100:0|X|100:100", "B|100:0|5|100:100",
                              "B|100:0|L", "B|L|100:100"}) {
      CHECK_EQ(parse_str(slider(curve), simd, kStable).sliders.size(), 1u);
    }
    for (const char* curve :
         {"B|100:0|X|100:100", "B|100:0|5|100:100", "B|100:0|L"}) {
      const auto map = parse_str(slider(curve), simd, kLazer);
      CHECK(map.sliders.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
  for (const char* curve : {"B|100:0|LL|100:100", "B|100:0|B2|100:100"}) {
    CHECK_EQ(unloadable_line(slider(curve), Client::Stable), 5u);
    CHECK_EQ(unloadable_line(slider(curve), Client::Lazer), 0u);
  }
}

// Lazer rejects coordinates and slider lengths beyond ±131072; stable reads
// them. Ranked maps have control points and lengths past that bound.
static void test_coordinate_and_length_bounds() {
  const std::string input =
      "osu file format v14\n[TimingPoints]\n0,500\n[HitObjects]\n"
      "200000,64,100,1,0\n"
      "64,64,200,2,0,L|262144:-460440,1,140000\n"
      "64,64,300,2,0,L|100:100,1,-1.79769313486231E+308\n";
  for (bool simd : {false, true}) {
    const auto stable = parse_str(input, simd, kStable);
    CHECK_EQ(stable.hit_objects.size(), 3u);
    CHECK_EQ(stable.hit_objects[0].x, 512);
    CHECK_EQ(stable.slider_points[0].x, 262144);
    CHECK_EQ(stable.slider_points[0].y, -460440);
    CHECK_EQ(stable.sliders[0].length, 140000);
    CHECK_EQ(stable.sliders[1].length, 0);
    const auto lazer = parse_str(input, simd, kLazer);
    CHECK(lazer.hit_objects.empty());
    CHECK_EQ(lazer.stats.malformed_lines, 3u);
  }
}

// stable has no rules past v14, its latest format, so it reads a v128 map
// like a v14 one: it truncates coordinates, reads curve types as markers and
// uses the legacy path rules. The map still reports v128.
static void test_v128_maps_in_stable() {
  const auto slider = [](int version, const char* curve) {
    return "osu file format v" + std::to_string(version) +
           "\n[TimingPoints]\n0,500\n[HitObjects]\n10.75,20.5,100,2,0," +
           std::string(curve) + ",1,0\n";
  };
  constexpr fosu::ParseOptions kPaths{.calculate_slider_paths = true};
  for (bool simd : {false, true}) {
    const auto stable =
        parse_str(slider(128, "B|100.5:0|L|100:100"), simd, kPaths);
    CHECK_EQ(stable.format_version, 128);
    CHECK_EQ(stable.hit_objects[0].x, 10);
    CHECK_EQ(stable.slider_points[0].x, 100);
    CHECK(stable.sliders[0].curve_type == fosu::CurveType::Linear);
    CHECK(stable.slider_segments.empty());
    const auto lazer =
        parse_str(slider(128, "B|100.5:0|L|100:100"), simd, kLazer);
    CHECK_EQ(lazer.hit_objects[0].x, 10.75f);
    CHECK_EQ(lazer.slider_segments.size(), 2u);

    // A collinear perfect curve is a line under the legacy rules.
    const auto v128 = parse_str(slider(128, "P|100:20|200:20"), simd, kPaths);
    const std::vector<fosu::PathPoint> v128_path(
        v128.slider_paths[0].points.begin(), v128.slider_paths[0].points.end());
    const auto v14 = parse_str(slider(14, "P|100:20|200:20"), simd, kPaths);
    CHECK(v128_path ==
          std::vector<fosu::PathPoint>(v14.slider_paths[0].points.begin(),
                                       v14.slider_paths[0].points.end()));
  }
  CHECK_EQ(unloadable_line(slider(128, "B2|100:0|100:100"), Client::Stable),
           5u);
}

static std::string difficulty_document(int version, const std::string& body) {
  return "osu file format v" + std::to_string(version) + "\n" + body +
         kTimingAndCircle;
}

// stable clamps stack leniency to [0, 1]; lazer keeps it.
static void test_stack_leniency_range() {
  const auto input = difficulty_document(14, "[General]\nStackLeniency:3\n");
  CHECK_EQ(parse_str(input, true, kStable).stack_leniency, 1);
  CHECK_EQ(parse_str(input, true, kLazer).stack_leniency, 3);
}

// Before v13, stable reads HP, CS, OD and AR as integer bytes; lazer reads
// floats in every version.
static void test_difficulty_bytes_before_v13() {
  struct Case {
    const char* line;
    double      stable, lazer;
  };
  for (const auto& test : {
           Case{"OverallDifficulty:+8", 8, 8},
           Case{"OverallDifficulty:7.5", 5, 7.5},
           Case{"OverallDifficulty:300", 5, 10},
       }) {
    for (int version : {12, 13}) {
      const auto input = difficulty_document(
          version, "[Difficulty]\n" + std::string(test.line) + "\n");
      const double stable = version == 13 ? test.lazer : test.stable;
      CHECK_EQ(parse_str(input, true, kStable).od, stable);
      CHECK_EQ(parse_str(input, true, kLazer).od, test.lazer);
    }
  }
}

// stable clamps circle size as it reads it, to mania's key range only when
// [General] already set mania and the map is v13+. Lazer clamps with the
// final mode.
static void test_circle_size_range_follows_mode_order() {
  const auto before_mode = difficulty_document(
      14, "[Difficulty]\nCircleSize:14\n[General]\nMode:3\n");
  CHECK_EQ(parse_str(before_mode, true, kStable).cs, 10);
  CHECK_EQ(parse_str(before_mode, true, kLazer).cs, 14);
  const auto mode_changes = difficulty_document(
      14,
      "[General]\nMode:3\n[Difficulty]\nCircleSize:14\n[General]\nMode:0\n");
  CHECK_EQ(parse_str(mode_changes, true, kStable).cs, 14);
  CHECK_EQ(parse_str(mode_changes, true, kLazer).cs, 10);
  for (int version : {12, 14}) {
    const auto after_mode = difficulty_document(
        version, "[General]\nMode:3\n[Difficulty]\nCircleSize:14\n");
    CHECK_EQ(parse_str(after_mode, true, kStable).cs, version == 14 ? 14 : 10);
    CHECK_EQ(parse_str(after_mode, true, kLazer).cs, 14);
  }
}

int main() {
  test_version_line_position();
  test_unreadable_version_fails_the_map();
  test_first_line_header();
  test_lines_before_the_first_header();
  test_unknown_section_names();
  test_header_spelling();
  test_header_sections_after_hit_objects();
  test_unreadable_hit_object_fails_stable();
  test_hit_object_lines_stable_skips();
  test_policy_rejection_keeps_stable_map_loadable();
  test_indented_colours();
  test_curve_type_tokens();
  test_coordinate_and_length_bounds();
  test_v128_maps_in_stable();
  test_stack_leniency_range();
  test_difficulty_bytes_before_v13();
  test_circle_size_range_follows_mode_order();
  return test_result();
}
