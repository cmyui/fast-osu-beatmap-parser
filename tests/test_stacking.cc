// osu!standard stacking. Expected values match osu!'s OsuBeatmapProcessor,
// including the pre-v6 algorithm.
#include <fosu/beatmap.h>
#include <fosu/parse_options.h>
#include <fosu/slider_path.h>
#include <tests/support/test.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr fosu::ParseOptions kStacking{.apply_stacking = true};

std::vector<int> heights(const std::string& input, bool simd) {
  std::vector<int> heights;
  for (const auto& stacking : parse_str(input, simd, kStacking).stacking)
    heights.push_back(stacking.stack_height);
  return heights;
}

std::string with_version(int version, std::string_view body) {
  return "osu file format v" + std::to_string(version) + "\n" +
         std::string(body);
}

// Each level moves 6.4 px times the circle scale (0.5 at circle size 5) and
// stable's 1.00041 adjustment.
constexpr float kLevel = 3.2013118267059326f;

}  // namespace

static void test_circles_at_one_position_stack_up_and_left() {
  for (int version : {5, 6, 14}) {
    const auto input = with_version(
        version,
        "[HitObjects]\n100,100,1000,1,0\n100,100,1100,1,0\n100,100,1200,1,0\n");
    for (bool simd : {false, true}) {
      const auto map = parse_str(input, simd, kStacking);
      CHECK_EQ(map.stacking[0].stack_height, 2);
      CHECK_EQ(map.stacking[1].stack_height, 1);
      CHECK_EQ(map.stacking[2].stack_height, 0);
      CHECK_EQ(map.stacking[1].stack_offset.x, -kLevel);
      CHECK_EQ(map.stacking[1].stack_offset.y, -kLevel);
      CHECK_EQ(map.stacking[0].stack_offset.x, -2 * kLevel);
      CHECK_EQ(map.hit_objects[0].x, 100 - 2 * kLevel);
      const auto [x, y] =
          map.hit_objects[0].raw_position(map.stacking[0].stack_offset);
      CHECK_EQ(x, 100);
      CHECK_EQ(y, 100);
    }
  }
}

static void test_stack_offset_scales_with_circle_size() {
  const std::string input =
      "osu file format v14\n[Difficulty]\nCircleSize:4.2\n"
      "[HitObjects]\n100,100,1000,1,0\n100,100,1100,1,0\n";
  for (bool simd : {false, true})
    CHECK_EQ(parse_str(input, simd, kStacking).stacking[0].stack_offset.x,
             -3.5598587989807129f);
}

static void test_objects_at_a_slider_end_stack_down_and_right() {
  for (int version : {5, 6, 14}) {
    const auto input = with_version(
        version,
        "[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
        "[HitObjects]\n0,0,1000,2,0,L|100:0,1,100\n100,0,1550,1,0\n"
        "100,0,1600,1,0\n");
    for (bool simd : {false, true}) {
      CHECK((heights(input, simd) == std::vector<int>{0, -1, -2}));
      CHECK_EQ(parse_str(input, simd, kStacking).stacking[1].stack_offset.x,
               kLevel);
    }
  }
}

// After an even number of slides a slider ends at its head. Before v6, osu!
// still stacks objects at the far end of the path instead.
static void test_old_formats_stack_at_the_path_end_after_repeats() {
  const auto beatmap = [](int version, std::string_view position) {
    return with_version(
        version,
        "[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
        "[HitObjects]\n200,200,3000,2,0,L|300:200,2,100\n" +
            std::string(position) + ",4050,1,0\n" + std::string(position) +
            ",4100,1,0\n");
  };
  for (bool simd : {false, true}) {
    CHECK(
        (heights(beatmap(5, "300,200"), simd) == std::vector<int>{0, -1, -2}));
    CHECK(
        (heights(beatmap(14, "200,200"), simd) == std::vector<int>{0, -1, -2}));
    // Elsewhere the circles stack only on each other.
    CHECK((heights(beatmap(14, "300,200"), simd) == std::vector<int>{0, 1, 0}));
  }
}

static void test_stacking_needs_close_time_and_position() {
  // At AR 10 objects stack within 450 ms * 0.7 leniency = 315 ms, and within
  // 3 px. Spinners neither stack nor separate others.
  const std::string input =
      "osu file format v14\n[Difficulty]\nApproachRate:10\n[HitObjects]\n"
      "100,100,0,1,0\n100,100,316,1,0\n103,100,400,1,0\n0,0,410,8,0,420\n"
      "103,100,500,1,0\n";
  for (bool simd : {false, true})
    CHECK((heights(input, simd) == std::vector<int>{0, 0, 1, 0, 0}));
}

static void test_zero_leniency_still_stacks_at_a_slider_end() {
  const std::string input =
      "osu file format v14\n[General]\nStackLeniency:0\n"
      "[Difficulty]\nCircleSize:4\nApproachRate:9.5\nSliderMultiplier:2.1\n"
      "[TimingPoints]\n0,375\n0,-55.5555555555556,4,2,0,100,0,0\n"
      "[HitObjects]\n"
      "275,205,0,6,0,B|192:304|44:252|44:252|156:192|128:112,1,"
      "377.999988464356\n130,166,375,5,2\n";
  for (bool simd : {false, true})
    CHECK((heights(input, simd) == std::vector<int>{0, -1}));
}

// Stacking uses osu!standard's slider end times, which round the velocity
// multiplier through single precision.
static void test_stacking_uses_osu_standard_slider_end_times() {
  const std::string input =
      "osu file format v14\n[General]\nStackLeniency:0.5\n"
      "[Difficulty]\nCircleSize:3.8\nApproachRate:9\nSliderMultiplier:1.7\n"
      "[TimingPoints]\n390,300\n143790,-142.857142857143,4,2,1,50,0,0\n"
      "153990,-111.111111111111,4,2,1,65,0,0\n[HitObjects]\n"
      "406,228,146790,6,0,L|374:371,2,118.999996368408\n406,228,147690,1,2\n"
      "73,166,155190,6,0,L|104:6,1,152.999995330811\n102,15,155790,1,0\n"
      "102,15,155940,1,0\n";
  for (bool simd : {false, true})
    CHECK((heights(input, simd) == std::vector<int>{0, -1, 0, -1, -2}));
}

static void test_stacked_slider_moves_its_points_but_not_its_path() {
  const std::string input =
      "osu file format v14\n[Difficulty]\nSliderMultiplier:1\n"
      "[TimingPoints]\n0,500\n[HitObjects]\n"
      "100,100,1000,2,0,L|200:100,2,100\n100,100,1100,2,0,L|200:100,1,100\n";
  for (bool simd : {false, true}) {
    std::vector<fosu::PathPoint> unstacked;
    for (const auto point :
         parse_str(input, simd, {.calculate_slider_paths = true})
             .slider_paths[0]
             .points)
      unstacked.push_back(point);
    const auto map = parse_str(
        input, simd, {.calculate_slider_paths = true, .apply_stacking = true});
    CHECK_EQ(map.stacking[0].stack_height, 1);
    const auto points = map.slider_points.subspan(map.sliders[0].point_begin,
                                                  map.sliders[0].point_count);
    CHECK_EQ(points[0].x, 200 - kLevel);
    CHECK_EQ(points[0].y, 100 - kLevel);
    const auto path = map.slider_paths[0].points;
    CHECK(
        (std::vector<fosu::PathPoint>(path.begin(), path.end()) == unstacked));
  }
}

static void test_stacking_applies_only_to_osu_standard() {
  for (int mode : {1, 2, 3}) {
    const auto input = "[General]\nMode:" + std::to_string(mode) +
                       "\n[HitObjects]\n100,100,0,1,0\n100,100,1,1,0\n";
    for (bool simd : {false, true}) {
      const auto map = parse_str(input, simd, kStacking);
      CHECK(map.stacking.empty());
      CHECK_EQ(map.hit_objects[0].x, 100);
    }
  }
}

int main() {
  test_circles_at_one_position_stack_up_and_left();
  test_stack_offset_scales_with_circle_size();
  test_objects_at_a_slider_end_stack_down_and_right();
  test_old_formats_stack_at_the_path_end_after_repeats();
  test_stacking_needs_close_time_and_position();
  test_zero_leniency_still_stacks_at_a_slider_end();
  test_stacking_uses_osu_standard_slider_end_times();
  test_stacked_slider_moves_its_points_but_not_its_path();
  test_stacking_applies_only_to_osu_standard();
  return test_result();
}
