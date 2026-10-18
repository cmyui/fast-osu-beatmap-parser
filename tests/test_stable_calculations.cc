// osu!stable's own slider calculations and stacking, where they differ from
// lazer's. Expected values follow osu!stable's SliderOsu.UpdateCalculations
// and HitObjectManager stacking.
#include <fosu/beatmap.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/mods.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <fosu/slider_event.h>
#include <fosu/slider_path.h>
#include <tests/support/scalar_engine.h>
#include <tests/support/test.h>

#include <string>
#include <string_view>
#include <vector>

using fosu::Client;
using fosu::SliderEventType;

namespace {

// SliderMultiplier 1 at a 500 ms beat moves sliders at 200 px/s, with a tick
// every 100 px.
std::string beatmap(std::string_view hit_objects,
                    int              version = 14,
                    std::string_view general = "") {
  return "osu file format v" + std::to_string(version) + "\n[General]\n" +
         std::string(general) +
         "\n[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n"
         "0,-50,4,1,0,100,0,0\n1,500\n[HitObjects]\n" +
         std::string(hit_objects);
}

struct TimedEvent {
  SliderEventType type;
  double          time;
  bool operator==(const TimedEvent&) const = default;
};

using Events = std::vector<TimedEvent>;

Events timed_events(const std::string& input, bool simd, Client client) {
  const auto map = parse_str(
      input, simd, {.calculate_slider_events = true, .client = client});
  Events events;
  for (const auto& event : map.slider_events[0])
    events.push_back({event.type, event.time});
  return events;
}

fosu::SliderPath path(const std::string& input, bool simd) {
  return parse_str(input, simd, {.calculate_slider_paths = true})
      .slider_paths[0];
}

}  // namespace

static void test_end_times_are_whole_milliseconds() {
  const auto input = beatmap("0,0,1000,2,0,L|200:0,1,100.3\n");
  for (bool simd : {false, true}) {
    const auto stable =
        parse_str(input, simd, {.calculate_slider_end_times = true});
    CHECK_EQ(stable.hit_objects[0].end_time, 1501);
    const auto lazer = parse_str(
        input, simd,
        {.calculate_slider_end_times = true, .client = Client::Lazer});
    CHECK_EQ(lazer.hit_objects[0].end_time, 1501.5);
  }
}

// Stable's own calculations are its osu!standard ones.
static void test_other_modes_keep_lazer_calculations() {
  const auto input = beatmap("0,0,1000,2,0,L|200:0,1,100.3\n", 14, "Mode:1");
  for (bool simd : {false, true})
    CHECK_EQ(parse_str(input, simd, {.calculate_slider_end_times = true})
                 .hit_objects[0]
                 .end_time,
             1501.5);
}

// After a tick too near the end, stable restarts the spacing on the way back
// instead of mirroring the ticks: 100 px from the end, not from the head.
static void test_ticks_restart_after_one_too_near_the_end() {
  const auto input = beatmap("0,0,1000,2,0,L|201:0,2,201\n");
  for (bool simd : {false, true}) {
    CHECK((timed_events(input, simd, Client::Stable) ==
           Events{{SliderEventType::Head, 1000},
                  {SliderEventType::Tick, 1500},
                  {SliderEventType::Repeat, 2005},
                  {SliderEventType::Tick, 2505},
                  {SliderEventType::LegacyLastTick, 2974},
                  {SliderEventType::Tail, 3010}}));
    CHECK((timed_events(input, simd, Client::Lazer) ==
           Events{{SliderEventType::Head, 1000},
                  {SliderEventType::Tick, 1500},
                  {SliderEventType::Repeat, 2005},
                  {SliderEventType::Tick, 2510},
                  {SliderEventType::LegacyLastTick, 2974},
                  {SliderEventType::Tail, 3010}}));
    const auto map = parse_str(input, simd, {.calculate_slider_events = true});
    CHECK_NEAR(map.slider_events[0][3].position.x, 101, 1e-4);
  }
}

// Lazer drops the repeats of a path with no length; stable keeps them.
static void test_zero_length_paths_keep_repeats() {
  const auto input = beatmap("0,0,1000,2,0,B|0:0,3,0\n");
  for (bool simd : {false, true}) {
    CHECK((timed_events(input, simd, Client::Stable) ==
           Events{{SliderEventType::Head, 1000},
                  {SliderEventType::Repeat, 1000},
                  {SliderEventType::Repeat, 1000},
                  {SliderEventType::LegacyLastTick, 1000},
                  {SliderEventType::Tail, 1000}}));
    CHECK_EQ(timed_events(input, simd, Client::Lazer).size(), 3u);
  }
}

// v9 Béziers are sampled evenly and stop short of their last point.
static void test_v9_bezier_stops_short() {
  const auto slider = "0,0,1000,2,0,B|100:100|200:0,1,0\n";
  for (bool simd : {false, true}) {
    CHECK(!(path(beatmap(slider, 9), simd).points.back() ==
            fosu::PathPoint{200, 0}));
    CHECK(path(beatmap(slider, 10), simd).points.back() ==
          (fosu::PathPoint{200, 0}));
  }
}

// A perfect curve is chords of 8 to 16 px of arc: 39 for a semicircle of
// radius 100.
static void test_perfect_curve_chords() {
  for (bool simd : {false, true}) {
    const auto arc = path(beatmap("0,0,1000,2,0,P|100:100|200:0,1,0\n"), simd);
    CHECK_EQ(arc.points.size(), 40u);
    CHECK(arc.points.back() == (fosu::PathPoint{200, 0}));
  }
}

// Each pair of Catmull points is 50 lines.
static void test_catmull_lines() {
  for (bool simd : {false, true})
    CHECK_EQ(path(beatmap("0,0,1000,2,0,C|100:0,1,0\n"), simd).points.size(),
             51u);
}

// Stable plays a hold in an osu!standard map as a circle, which stacks.
static void test_holds_stack_as_circles() {
  const auto input =
      beatmap("100,100,1000,128,0,1500:0:0:0:0:\n100,100,1100,1,0,0:0:0:0:\n");
  for (bool simd : {false, true}) {
    const auto stable = parse_str(input, simd, {.apply_stacking = true});
    CHECK_EQ(stable.stacking[0].stack_height, 1);
    const auto lazer = parse_str(
        input, simd, {.apply_stacking = true, .client = Client::Lazer});
    CHECK_EQ(lazer.stacking[0].stack_height, 0);
  }
}

// The inherited point at 0 doubles the speed; the red line at 1 ms resets it.
static void test_slider_velocities() {
  const auto input =
      beatmap("0,0,0,2,0,L|100:0,1,100\n0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true}) {
    for (auto client : {Client::Stable, Client::Lazer}) {
      const auto map = parse_str(
          input, simd, {.calculate_slider_end_times = true, .client = client});
      CHECK_EQ(map.slider_velocities.size(), 2u);
      CHECK_EQ(map.slider_velocities[0], 0.4);
      CHECK_EQ(map.slider_velocities[1], 0.2);
      const auto fast = parse_str(input, simd,
                                  {.calculate_slider_end_times = true,
                                   .mods = fosu::Mods::DoubleTime,
                                   .client = client});
      CHECK_EQ(fast.slider_velocities[1], 0.2 * 1.5);
      CHECK(parse_str(input, simd,
                      {.calculate_slider_paths = true, .client = client})
                .slider_velocities.empty());
    }
  }
}

// Ticks along a path too long to walk exhaust the event budget, failing the
// parse rather than hanging: at this length, a tick no longer shortens the
// distance left.
static void test_huge_paths_fail_the_parse() {
  const auto input = beatmap("0,0,1000,2,0,L|100:0,1,1e30\n");
  for (bool simd : {false, true}) {
    for (const fosu::ParseOptions options :
         {fosu::ParseOptions{.calculate_slider_end_times = true},
          fosu::ParseOptions{.calculate_slider_events = true}}) {
      fosu::Parser parser(simd ? fosu::internal::compiled_engine
                               : fosu_test::scalar_engine());
      CHECK(!parser.parse(input, options));
      CHECK(parser.error().code == fosu::ParseErrorCode::OutOfMemory);
    }
  }
}

// A v9 Bézier's work grows with the cube of its points. A curve of 250
// parses, as real v9 maps' heavy curves must; one of 1000 is too costly to
// work out, failing the parse rather than hanging.
static void test_v9_bezier_budget() {
  const auto slider = [](int points) {
    std::string line = "0,0,1000,2,0,B";
    for (int i = 1; i < points; ++i)
      line += "|" + std::to_string(i) + ":" + std::to_string(i % 2 * 10);
    return beatmap(line + ",1,0\n", 9);
  };
  fosu::Parser parser;
  CHECK(parser.parse(slider(250), {.calculate_slider_end_times = true}));
  CHECK(!parser.parse(slider(1000), {.calculate_slider_end_times = true}));
  CHECK(parser.error().code == fosu::ParseErrorCode::OutOfMemory);
}

int main() {
  test_end_times_are_whole_milliseconds();
  test_other_modes_keep_lazer_calculations();
  test_ticks_restart_after_one_too_near_the_end();
  test_zero_length_paths_keep_repeats();
  test_v9_bezier_stops_short();
  test_perfect_curve_chords();
  test_catmull_lines();
  test_holds_stack_as_circles();
  test_slider_velocities();
  test_huge_paths_fail_the_parse();
  test_v9_bezier_budget();
  return test_result();
}
