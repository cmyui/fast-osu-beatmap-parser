// Slider end times and events. Expected values match osu!'s legacy decoder
// (ConvertHitObjectParser, LegacyBeatmapDecoder) and SliderEventGenerator.
#include <fosu/beatmap.h>
#include <fosu/parse_options.h>
#include <fosu/slider_event.h>
#include <fosu/slider_path.h>
#include <tests/support/test.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using fosu::SliderEventType;

namespace {

// SliderMultiplier 1 at a 500 ms beat moves sliders at 0.2 px/ms, so each
// 100 px span takes 500 ms.
std::string beatmap(std::string_view timing_points,
                    std::string_view hit_objects) {
  return "osu file format v14\n[Difficulty]\nSliderMultiplier:1\n"
         "[TimingPoints]\n" +
         std::string(timing_points) + "[HitObjects]\n" +
         std::string(hit_objects);
}

double end_time(const std::string& input, bool simd, size_t object = 0) {
  return parse_str(input, simd, {.calculate_slider_end_times = true})
      .hit_objects[object]
      .end_time;
}

struct TimedEvent {
  SliderEventType type;
  double          time;
  bool operator==(const TimedEvent&) const = default;
};

using Events = std::vector<TimedEvent>;

Events timed_events(const std::string& input, bool simd) {
  const auto map = parse_str(input, simd, {.calculate_slider_events = true});
  Events     events;
  for (const auto& event : map.slider_events[map.hit_objects[0].slider])
    events.push_back({event.type, event.time});
  return events;
}

}  // namespace

static void test_end_time_covers_every_span() {
  const auto input = beatmap("0,500\n", "0,0,1000,2,0,L|100:0,2,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 2000);
}

static void test_omitted_or_nonpositive_length_uses_path_distance() {
  for (const char* tail : {"", ",0", ",-5"}) {
    const auto input =
        beatmap("0,500\n", "0,0,1000,2,0,L|200:0,1" + std::string(tail) + "\n");
    for (bool simd : {false, true})
      CHECK_EQ(end_time(input, simd), 2000);
  }
}

static void test_slide_count_below_one_is_one_slide() {
  const auto input = beatmap("0,500\n", "0,0,1000,2,0,L|100:0,0,100\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(end_time(input, simd), 1500);
    CHECK((timed_events(input, simd) ==
           Events{{SliderEventType::Head, 1000},
                  {SliderEventType::LegacyLastTick, 1464},
                  {SliderEventType::Tail, 1500}}));
  }
}

// The generic decoder's end time; rulesets convert sliders afterwards.
static void test_end_time_does_not_depend_on_game_mode() {
  for (const char* mode : {"0", "1", "2"}) {
    const auto input = std::string("osu file format v14\n[General]\nMode:") +
                       mode +
                       "\n[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n"
                       "0,500\n[HitObjects]\n0,0,1000,2,0,L|100:0,2,100\n";
    for (bool simd : {false, true})
      CHECK_EQ(end_time(input, simd), 2000);
  }
}

static void test_inherited_point_scales_velocity() {
  const auto input =
      beatmap("0,500\n0,-50,4,1,0,100,0,0\n", "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1250);
}

static void test_velocity_multiplier_is_clamped_from_tenth_to_tenfold() {
  // -1 asks for 100x and -10000 for 0.01x.
  const auto fast =
      beatmap("0,500\n0,-1,4,1,0,100,0,0\n", "0,0,1000,2,0,L|100:0,1,100\n");
  const auto slow = beatmap("0,500\n0,-10000,4,1,0,100,0,0\n",
                            "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(end_time(fast, simd), 1050);
    CHECK_EQ(end_time(slow, simd), 6000);
  }
}

static void test_nan_inherited_point_keeps_default_velocity() {
  const auto input =
      beatmap("0,500\n0,NaN,4,1,0,100,0,0\n", "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1500);
}

static void test_beat_length_is_clamped_from_6_to_60000_ms() {
  const auto short_beat = beatmap("0,1\n", "0,0,1000,2,0,L|100:0,1,100\n");
  const auto long_beat = beatmap("0,100000\n", "0,0,1000,2,0,L|100:0,1,1\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(end_time(short_beat, simd), 1006);
    CHECK_EQ(end_time(long_beat, simd), 1600);
  }
}

static void test_uninherited_point_resets_velocity() {
  const auto input = beatmap("0,500\n0,-50,4,1,0,100,0,0\n1000,500\n",
                             "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1500);
}

// Consecutive lines at one time form a group. Within it, the first
// uninherited line sets the beat length and the last inherited line sets the
// velocity, overriding the uninherited lines' 1x.
static void test_first_uninherited_point_in_a_group_wins() {
  const auto input = beatmap("0,500\n0,250\n", "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1500);
}

static void test_last_inherited_point_in_a_group_wins() {
  const auto input =
      beatmap("0,500\n0,-50,4,1,0,100,0,0\n0,-25,4,1,0,100,0,0\n",
              "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1125);
}

static void test_inherited_point_overrides_reset_in_its_group() {
  const auto inherited_first =
      beatmap("0,-50,4,1,0,100,0,0\n0,500\n", "0,0,1000,2,0,L|100:0,1,100\n");
  const auto inherited_last =
      beatmap("0,500\n1000,-50,4,1,0,100,0,0\n1000,500\n",
              "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(end_time(inherited_first, simd), 1250);
    CHECK_EQ(end_time(inherited_last, simd), 1250);
  }
}

static void test_points_apply_in_time_order_not_file_order() {
  const auto input =
      beatmap("0,500\n2000,-50,4,1,0,100,0,0\n1000,-25,4,1,0,100,0,0\n",
              "0,0,1500,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1625);
}

// osu! drops an inherited point that matches the velocity in effect when it
// is read. Here the 2x at 1000 matches the 2x at 0, so the 4x line read later
// for 500 still applies at 1000.
static void test_unchanged_inherited_point_is_dropped_when_read() {
  const auto input = beatmap(
      "0,500\n0,-50,4,1,0,100,0,0\n1000,-50,4,1,0,100,0,0\n"
      "500,-25,4,1,0,100,0,0\n",
      "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 1125);
}

// Unlike a repeat within one group, a separate later group replaces it.
static void test_later_group_at_the_same_time_replaces_earlier_one() {
  const auto input = beatmap("0,500\n1000,-50,4,1,0,100,0,0\n0,250\n",
                             "0,0,500,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true})
    CHECK_EQ(end_time(input, simd), 750);
}

// The first uninherited point's beat length applies before it; inherited
// points never apply before their time.
static void test_objects_before_the_first_point_use_its_beat_length() {
  const auto uninherited = beatmap("1000,250\n", "0,0,500,2,0,L|100:0,1,100\n");
  const auto inherited = beatmap("1000,250\n1000,-50,4,1,0,100,0,0\n",
                                 "0,0,500,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(end_time(uninherited, simd), 750);
    CHECK_EQ(end_time(inherited, simd), 750);
  }
}

static void test_without_timing_points_sliders_use_60_bpm() {
  // The default slider multiplier is 1.4 and the default beat 1000 ms.
  const std::string input =
      "osu file format v14\n[Difficulty]\nSliderMultiplier:1\n"
      "[TimingPoints]\n0,500\n[HitObjects]\n0,0,0,2,0,L|100:0,1,140\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd,
                               {.sections = fosu::kSectionHitObjects,
                                .calculate_slider_end_times = true});
    CHECK_EQ(map.hit_objects[0].end_time, 140 / (100 * 1.4 / 1000));
  }
}

// lazer removes the repeats of a path no longer than 1e-7 px. Stable creates
// them but never judges them.
static void test_zero_length_path_drops_repeats() {
  for (const char* slider : {"B|0:0,3,200", "L|0:0,2,100", "C|0:0,2,100",
                             "P|0:0|0:0,2,100", "B,3072,-1", "L,1,100"}) {
    const auto input =
        beatmap("0,500\n", "0,0,1000,2,0," + std::string(slider) + "\n");
    for (bool simd : {false, true}) {
      CHECK_EQ(end_time(input, simd), 1000);
      CHECK((timed_events(input, simd) ==
             Events{{SliderEventType::Head, 1000},
                    {SliderEventType::LegacyLastTick, 1000},
                    {SliderEventType::Tail, 1000}}));
    }
  }
  // The slider still reports the slide count as written.
  for (bool simd : {false, true}) {
    const auto map =
        parse_str(beatmap("0,500\n", "0,0,1000,2,0,B,3072,-1\n"), simd);
    CHECK_EQ(map.sliders[0].slides, 3072);
  }
}

static void test_repeats_need_a_path_longer_than_1e7_px() {
  const auto repeats = [](const char* length, bool simd) {
    const auto input = beatmap(
        "0,500\n", "0,0,1000,2,0,L|100:0,5," + std::string(length) + "\n");
    size_t count = 0;
    for (const auto& event : timed_events(input, simd))
      count += event.type == SliderEventType::Repeat;
    return count;
  };
  for (bool simd : {false, true}) {
    CHECK_EQ(repeats("0.00000001", simd), 0u);
    CHECK_EQ(repeats("0.000001", simd), 4u);
  }
}

// Stacking needs end times, so it calculates them; paths alone do not.
static void test_end_times_are_calculated_only_when_needed() {
  const auto input = beatmap("0,500\n", "0,0,1000,2,0,L|100:0,1,100\n");
  for (bool simd : {false, true}) {
    CHECK_EQ(parse_str(input, simd).hit_objects[0].end_time, 0);
    CHECK_EQ(parse_str(input, simd, {.calculate_slider_paths = true})
                 .hit_objects[0]
                 .end_time,
             0);
    const auto stacked = parse_str(input, simd, {.apply_stacking = true});
    CHECK_EQ(stacked.hit_objects[0].end_time, 1500);
    CHECK(stacked.slider_events.empty());
  }
}

static void test_end_time_is_independent_of_requested_outputs() {
  const std::string inputs[] = {
      beatmap("0,500\n0,-50,4,1,0,100,0,0\n", "0,0,1000,2,0,L|100:0,2,100\n"),
      beatmap("0,500\n", "0,0,1000,2,0,B|100:100|200:0|200:0,1,500\n"),
      beatmap("0,500\n", "0,0,1000,2,0,P|100:100|200:0,1,0\n"),
      beatmap("0,500\n", "0,0,1000,2,0,C|100:100|200:0,1,0\n"),
      beatmap("0,500\n", "0,0,1000,2,0,B,3072,-1\n"),
      "osu file format v128\n[Difficulty]\nSliderMultiplier:1.4\n"
      "[TimingPoints]\n0,500\n[HitObjects]\n"
      "0,0,1000,2,0,B2|100:0|100:100|100:100,1,300\n"
      "0,0,3000,2,0,B2|100:0|100:100|100:100,1,0\n"
      "0,0,5000,2,0,B2|100:0|100:100|100:100,1\n",
  };
  constexpr auto           lazer = fosu::Client::Lazer;
  const fosu::ParseOptions variants[] = {
      {.calculate_slider_end_times = true,
       .calculate_slider_paths = true,
       .client = lazer},
      {.calculate_slider_events = true, .client = lazer},
      {.calculate_slider_events = true,
       .apply_stacking = true,
       .client = lazer},
  };
  for (const auto& input : inputs) {
    for (bool simd : {false, true}) {
      std::vector<double> expected;
      for (const auto& object :
           parse_str(input, simd,
                     {.calculate_slider_end_times = true, .client = lazer})
               .hit_objects)
        expected.push_back(object.end_time);
      for (const auto& options : variants) {
        const auto map = parse_str(input, simd, options);
        for (size_t i = 0; i < expected.size(); ++i)
          CHECK_EQ(map.hit_objects[i].end_time, expected[i]);
      }
    }
  }
}

static void test_events_cover_head_ticks_repeats_and_tail() {
  const std::string input =
      "osu file format v8\n[Difficulty]\nSliderMultiplier:1\n"
      "[TimingPoints]\n0,500\n0,-50,4,0,0,100,0,0\n"
      "[HitObjects]\n0,0,1000,2,0,L|400:0,2,400\n";
  for (bool simd : {false, true}) {
    CHECK((timed_events(input, simd) ==
           Events{{SliderEventType::Head, 1000},
                  {SliderEventType::Tick, 1500},
                  {SliderEventType::Repeat, 2000},
                  {SliderEventType::Tick, 2500},
                  {SliderEventType::LegacyLastTick, 2964},
                  {SliderEventType::Tail, 3000}}));
    const auto map = parse_str(input, simd, {.calculate_slider_events = true});
    const auto events = map.slider_events[0];
    CHECK(events[2].position == (fosu::PathPoint{400, 0}));
    CHECK(events[5].position == (fosu::PathPoint{0, 0}));
    // 36 ms before the end, on the way back: 3.6% of the path from the head.
    CHECK_NEAR(events[4].path_progress, 0.036, 1e-9);
    CHECK_NEAR(events[4].position.x, 14.4f, 1e-4f);
    for (const auto& event : events) {
      const auto position =
          fosu::slider_position_at(map.slider_paths[0], event.path_progress);
      CHECK_NEAR(event.position.x, position.x, 1e-4f);
      CHECK_NEAR(event.position.y, position.y, 1e-4f);
    }
  }
}

// Before v8, tick spacing ignores the inherited velocity multiplier.
static void test_ticks_before_v8_ignore_velocity_multiplier() {
  const std::string input =
      "osu file format v7\n[Difficulty]\nSliderMultiplier:1\n"
      "[TimingPoints]\n0,500\n0,-50,4,0,0,100,0,0\n"
      "[HitObjects]\n0,0,1000,2,0,L|400:0,2,400\n";
  for (bool simd : {false, true}) {
    std::vector<double> ticks;
    for (const auto& event : timed_events(input, simd))
      if (event.type == SliderEventType::Tick)
        ticks.push_back(event.time);
    CHECK(ticks == (std::vector<double>{1250, 1500, 1750, 2250, 2500, 2750}));
  }
}

static void test_legacy_last_tick_is_at_least_halfway() {
  const auto input = beatmap("0,500\n", "0,0,1000,2,0,L|10:0,1,10\n");
  for (bool simd : {false, true}) {
    const auto  map = parse_str(input, simd, {.calculate_slider_events = true});
    const auto& legacy = map.slider_events[0][1];
    CHECK(legacy.type == SliderEventType::LegacyLastTick);
    CHECK_EQ(legacy.time, 1025);
    CHECK_EQ(legacy.path_progress, 0.5);
    CHECK_EQ(legacy.position.x, 5);
  }
}

static void test_nan_inherited_point_suppresses_ticks() {
  const auto input =
      beatmap("0,500\n0,NaN,4,0,0,100,0,0\n", "0,0,0,2,0,L|300:0,1,300\n");
  const auto zero_length =
      beatmap("0,500\n0,NaN,4,0,0,100,0,0\n", "0,0,0,2,0,B|0:0,1,0\n");
  for (bool simd : {false, true}) {
    CHECK((timed_events(input, simd) ==
           Events{{SliderEventType::Head, 0},
                  {SliderEventType::LegacyLastTick, 1464},
                  {SliderEventType::Tail, 1500}}));
    const auto map =
        parse_str(zero_length, simd, {.calculate_slider_events = true});
    CHECK_EQ(map.slider_events[0].size(), 3u);
    for (const auto& event : map.slider_events[0])
      CHECK(std::isfinite(event.time) && std::isfinite(event.path_progress));
  }
}

int main() {
  test_end_time_covers_every_span();
  test_omitted_or_nonpositive_length_uses_path_distance();
  test_slide_count_below_one_is_one_slide();
  test_end_time_does_not_depend_on_game_mode();
  test_inherited_point_scales_velocity();
  test_velocity_multiplier_is_clamped_from_tenth_to_tenfold();
  test_nan_inherited_point_keeps_default_velocity();
  test_beat_length_is_clamped_from_6_to_60000_ms();
  test_uninherited_point_resets_velocity();
  test_first_uninherited_point_in_a_group_wins();
  test_last_inherited_point_in_a_group_wins();
  test_inherited_point_overrides_reset_in_its_group();
  test_points_apply_in_time_order_not_file_order();
  test_unchanged_inherited_point_is_dropped_when_read();
  test_later_group_at_the_same_time_replaces_earlier_one();
  test_objects_before_the_first_point_use_its_beat_length();
  test_without_timing_points_sliders_use_60_bpm();
  test_zero_length_path_drops_repeats();
  test_repeats_need_a_path_longer_than_1e7_px();
  test_end_times_are_calculated_only_when_needed();
  test_end_time_is_independent_of_requested_outputs();
  test_events_cover_head_ticks_repeats_and_tail();
  test_ticks_before_v8_ignore_velocity_multiplier();
  test_legacy_last_tick_is_at_least_halfway();
  test_nan_inherited_point_suppresses_ticks();
  return test_result();
}
