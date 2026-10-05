// Calculated slider paths. Expected values match osu!'s SliderPath and its
// legacy decoder (ConvertHitObjectParser).
#include <fosu/beatmap.h>
#include <fosu/enums.h>
#include <fosu/parse_options.h>
#include <fosu/slider_path.h>
#include <tests/support/test.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string beatmap(std::string_view hit_objects, int version = 14) {
  return "osu file format v" + std::to_string(version) +
         "\n[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
         "[HitObjects]\n" +
         std::string(hit_objects);
}

// A copy that outlives the parser's next call.
struct Path {
  std::vector<fosu::PathPoint> points;
  double                       distance;
  bool operator==(const Path&) const = default;
};

Path path(const std::string& input,
          bool               simd,
          size_t             object = 0,
          fosu::Client       client = fosu::Client::Stable) {
  const auto map = parse_str(
      input, simd, {.calculate_slider_paths = true, .client = client});
  const auto& path = map.slider_paths[map.hit_objects[object].slider];
  return {{path.points.begin(), path.points.end()}, path.distance()};
}

// The same slider with another curve description. Only lazer has v128 rules.
Path path_as(std::string_view slider, bool simd, int version = 14) {
  return path(beatmap("0,0,1000,2,0," + std::string(slider) + "\n", version),
              simd, 0,
              version >= 128 ? fosu::Client::Lazer : fosu::Client::Stable);
}

}  // namespace

static void test_declared_length_trims_or_extends_the_path() {
  for (bool simd : {false, true}) {
    const auto trimmed = path(beatmap("10,20,1000,2,0,L|110:20,2,50\n"), simd);
    CHECK_EQ(trimmed.distance, 50);
    CHECK(trimmed.points.back() == (fosu::PathPoint{50, 0}));
    const auto extended =
        path(beatmap("10,20,1000,2,0,L|110:20,2,150\n"), simd);
    CHECK_EQ(extended.distance, 150);
    CHECK(extended.points.back() == (fosu::PathPoint{150, 0}));
  }
}

static void test_each_curve_type_honours_the_declared_length() {
  for (const char* curve :
       {"B|110:120|210:20", "P|110:120|210:20", "C|110:120|210:20"}) {
    const auto input =
        beatmap("10,20,1000,2,0," + std::string(curve) + ",2,200\n");
    for (bool simd : {false, true})
      CHECK_EQ(path(input, simd).distance, 200);
  }
}

// A path only extends along a final segment with a direction.
static void test_repeated_end_point_prevents_extension() {
  for (bool simd : {false, true}) {
    CHECK_EQ(
        path(beatmap("10,20,1000,2,0,L|110:20|110:20,2,150\n"), simd).distance,
        100);
    // lazer's duplicate-last-position-slider.osu.
    CHECK_EQ(
        path(beatmap("261,171,1000,2,0,B|262:171|262:171|262:171,1,2\n"), simd)
            .distance,
        1);
  }
}

// A repeated last point ends a Bezier segment instead, so the path still
// extends along the curve's final direction.
static void test_repeated_bezier_end_point_still_extends() {
  for (bool simd : {false, true})
    CHECK_EQ(path_as("B|100:100|200:0|200:0,1,500", simd).distance, 500);
}

static void test_point_at_the_head_makes_a_zero_length_path() {
  for (bool simd : {false, true})
    CHECK_EQ(path(beatmap("10,20,1000,2,0,B|10:20,2,100\n"), simd).distance, 0);
}

// A repeated point ends one Bezier segment and starts the next; repeating it
// again changes nothing.
static void test_repeated_point_splits_bezier_segments() {
  for (bool simd : {false, true}) {
    CHECK(path_as("B|100:0|100:0|100:100,1,0", simd) ==
          path_as("L|100:0|100:100,1,0", simd));
    CHECK(path_as("B|1:1|2:2|3:3|3:3|3:3|3:3|4:4,2,200", simd) ==
          path_as("B|1:1|2:2|3:3|3:3|4:4,2,200", simd));
  }
}

// A perfect curve needs exactly three points that are not collinear.
static void test_perfect_curve_falls_back_for_unusable_points() {
  for (bool simd : {false, true}) {
    CHECK(path_as("P|100:100,1,0", simd) == path_as("L|100:100,1,0", simd));
    CHECK(path_as("P|100:0|200:0,1,0", simd) ==
          path_as("L|100:0|200:0,1,0", simd));
    CHECK(path_as("P|100:100|200:0|300:100,1,0", simd) ==
          path_as("B|100:100|200:0|300:100,1,0", simd));
    // Lazer-format maps keep the perfect curve type, but a triangle with
    // |cross product| <= 1e-3 has no usable circle: osu! draws a Bezier.
    CHECK(path_as("P|100:0|200:0,1,0", simd, 128) ==
          path_as("B|100:0|200:0,1,0", simd, 128));
    CHECK(path_as("P|200:0|100:0,1,0", simd, 128) ==
          path_as("B|200:0|100:0,1,0", simd, 128));
    const auto nearly = path_as("P|100:0|200:0.00001,1,200", simd, 128);
    CHECK_EQ(nearly.points.size(), 3u);
    CHECK_EQ(nearly.distance, 200);
  }
}

static void test_perfect_curve_follows_a_circular_arc() {
  for (bool simd : {false, true}) {
    const auto arc = path_as("P|100:100|200:0,1,0", simd);
    // A semicircle of radius 100 approximated by chords.
    CHECK_NEAR(arc.distance, 314.05381774902344, 1e-3);
    CHECK_NEAR(arc.points.back().x, 200, 1e-3);
    CHECK_NEAR(arc.points.back().y, 0, 1e-3);
  }
}

// lazer's catmull-duplicate-initial-controlpoint.osu.
static void test_catmull_point_at_the_head_is_merged() {
  for (bool simd : {false, true})
    CHECK(path(beatmap("200,304,1000,2,0,C|200:304|288:304|288:208|352:208,1,"
                       "260\n"),
               simd) ==
          path(beatmap("200,304,1000,2,0,C|288:304|288:208|352:208,1,260\n"),
               simd));
}

// lazer's adjacent-catmull-segments.osu. Stable-format maps keep one Catmull
// curve through the repeated points. Lazer-format maps split there, and a
// two-point Catmull segment is straight.
static void test_repeated_catmull_points_split_only_in_lazer_format() {
  const std::string slider =
      "200,304,1000,2,0,C|288:304|288:304|288:208|288:208|352:208,1,0\n";
  for (bool simd : {false, true}) {
    const auto lazer = path(beatmap(slider, 128), simd, 0, fosu::Client::Lazer);
    CHECK_NEAR(lazer.distance, 88 + 96 + 64, 1e-3);
    CHECK_NEAR(lazer.points.back().x, 152, 1e-3);
    CHECK_NEAR(lazer.points.back().y, -96, 1e-3);
    CHECK(path(beatmap(slider), simd).distance > lazer.distance);
  }
}

// lazer's multi-segment-slider.osu: a letter starts a segment at the
// preceding point.
static void test_lazer_format_letters_start_new_segments() {
  const auto input = beatmap(
      "63,301,1000,2,0,P|224:57|B|439:298|131:316|322:169|155:194,1,1040\n",
      128);
  for (bool simd : {false, true}) {
    const auto map = parse_str(
        input, simd,
        {.calculate_slider_paths = true, .client = fosu::Client::Lazer});
    const auto segments = map.slider_segments;
    CHECK_EQ(segments.size(), 2u);
    CHECK(segments[0].type == fosu::CurveType::PerfectCurve);
    CHECK(segments[1].type == fosu::CurveType::Bezier);
    // The arc ends where the Bezier starts.
    bool joined = false;
    for (const auto point : map.slider_paths[0].points)
      joined |= point == fosu::PathPoint{376, -3};
    CHECK(joined);
    CHECK_EQ(map.slider_paths[0].distance(), 1040);
  }
}

Path lazer_path_as(std::string_view slider, bool simd) {
  return path(beatmap("0,0,1000,2,0," + std::string(slider) + "\n"), simd, 0,
              fosu::Client::Lazer);
}

// Before v128, lazer also starts a segment at each letter, then applies the
// legacy rules to each segment, ending at the next one's first point. stable
// gives the whole slider the last type instead; see test_clients.
static void test_lazer_letters_start_legacy_segments() {
  for (bool simd : {false, true}) {
    CHECK(
        lazer_path_as("L|100:0|B|100:100|0:100,1,0", simd).points ==
        (std::vector<fosu::PathPoint>{{0, 0}, {100, 0}, {100, 100}, {0, 100}}));
    // A perfect curve needs exactly three points, counting that end point,
    // and draws a line through collinear ones.
    const auto arc = lazer_path_as("P|100:100|L|200:0|300:0,1,0", simd);
    CHECK_NEAR(arc.distance, 314.05381774902344 + 100, 1e-3);
    CHECK(
        lazer_path_as("P|100:0|L|200:0|200:100,1,0", simd).points ==
        (std::vector<fosu::PathPoint>{{0, 0}, {100, 0}, {200, 0}, {200, 100}}));
    // A last segment holding only its first point adds nothing.
    CHECK(lazer_path_as("L|100:0|B|100:100,1,0", simd).points ==
          (std::vector<fosu::PathPoint>{{0, 0}, {100, 0}, {100, 100}}));
    // A repeated point splits a segment, unless it is the segment's last
    // before the next one's first.
    CHECK_NEAR(
        lazer_path_as("B|100:100|100:100|200:0|L|300:100,1,0", simd).distance,
        370.9743161201477, 1e-3);
    CHECK_NEAR(
        lazer_path_as("B|100:100|200:0|200:0|L|300:100,1,0", simd).distance,
        338.47502517700195, 1e-3);
  }
}

static void test_bspline_degree_shapes_the_curve() {
  for (bool simd : {false, true}) {
    // Degree 1 joins the control points with straight lines.
    const auto linear = path_as("B1|100:0|200:50|300:0,1,0", simd, 128);
    CHECK(linear.points == (std::vector<fosu::PathPoint>{
                               {0, 0}, {100, 0}, {200, 50}, {300, 0}}));
    // Higher degrees curve between them, even with distant points.
    const auto distant = path_as(
        "B8|10000:0|-10000:10000|10000:-10000|-10000:0|10000:10000|"
        "-10000:-10000|10000:0|0:10000,1,500",
        simd, 128);
    CHECK_EQ(distant.distance, 500);
    CHECK_NEAR(distant.points.back().x, 499.805969, 1e-3);
    CHECK_NEAR(distant.points.back().y, 11.9145603, 1e-3);
  }
}

static void test_segments_of_different_types_join() {
  for (bool simd : {false, true})
    CHECK(
        path_as("L|100:0|B3|100:100|0:100,1,300", simd, 128).points ==
        (std::vector<fosu::PathPoint>{{0, 0}, {100, 0}, {100, 100}, {0, 100}}));
}

static void test_position_query_clamps_and_interpolates() {
  for (bool simd : {false, true}) {
    const auto map = parse_str(beatmap("10,20,1000,2,0,L|110:20,1,100\n"), simd,
                               {.calculate_slider_paths = true});
    const auto& path = map.slider_paths[0];
    CHECK(fosu::slider_position_at(path, 0.5) == (fosu::PathPoint{50, 0}));
    CHECK(fosu::slider_position_at(path, -1) == (fosu::PathPoint{0, 0}));
    CHECK(fosu::slider_position_at(path, 2) == (fosu::PathPoint{100, 0}));
  }
}

int main() {
  test_declared_length_trims_or_extends_the_path();
  test_each_curve_type_honours_the_declared_length();
  test_repeated_end_point_prevents_extension();
  test_repeated_bezier_end_point_still_extends();
  test_point_at_the_head_makes_a_zero_length_path();
  test_repeated_point_splits_bezier_segments();
  test_perfect_curve_falls_back_for_unusable_points();
  test_perfect_curve_follows_a_circular_arc();
  test_catmull_point_at_the_head_is_merged();
  test_repeated_catmull_points_split_only_in_lazer_format();
  test_lazer_format_letters_start_new_segments();
  test_lazer_letters_start_legacy_segments();
  test_bspline_degree_shapes_the_curve();
  test_segments_of_different_types_join();
  test_position_query_clamps_and_interpolates();
  return test_result();
}
