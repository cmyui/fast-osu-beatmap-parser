#include <fosu/beatmap.h>
#include <fosu/engine/hit_objects/object_types.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/types.h>
#include <tests/support/test.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

static std::string slider_document(std::string_view slider) {
  return "osu file format v14\n[HitObjects]\n0,0,0,2,0," + std::string(slider) +
         '\n';
}

static void test_slider_points() {
  struct Case {
    const char* points;
    float       x;
    float       y;
  };
  const Case cases[] = {
      {"|172:44", 172, 44},
      {"|-1.9:2.9", -1, 2},
      {"|1:2e1", 1, 20},
      {"|131072:-131072", 131072, -131072},
      // osu! reads only the first two ':'-separated values of a point.
      {"|172:44:9", 172, 44},
      {"|172:44:junk", 172, 44},
  };
  for (bool simd : {false, true}) {
    for (const auto& test : cases) {
      const auto map = parse_str(
          slider_document("B" + std::string(test.points) + ",1,10"), simd);
      CHECK_EQ(map.hit_objects.size(), 1u);
      CHECK_EQ(map.sliders.size(), 1u);
      if (map.sliders.empty())
        continue;
      const auto& slider = map.sliders[0];
      CHECK_EQ(slider.point_count, 1u);
      CHECK_EQ(map.slider_points[slider.point_begin].x, test.x);
      CHECK_EQ(map.slider_points[slider.point_begin].y, test.y);
    }

    for (const auto points :
         {"|", "|1", "|1:", "|:2", "|bad:2", "|1:bad", "|131073:2"}) {
      const auto map = parse_str(
          slider_document("B" + std::string(points) + ",1,10"), simd, kLazer);
      CHECK(map.hit_objects.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
}

static void test_slider_pool_indices_after_rejected_record() {
  for (bool simd : {false, true}) {
    for (fosu::i32 version : {14, 128}) {
      const auto curve = version == 128 ? "B1" : "L";
      const auto map =
          parse_str("osu file format v" + std::to_string(version) +
                        "\n[HitObjects]\n0,0,100,1,0\n0,0,200,2,0," + curve +
                        "|10:20,1,30\n"
                        "0,0,300,2,0,L|30:40|B|50:60|bad:80,1,30\n"
                        "[Metadata]\nTitle:gap\n[HitObjects]\n"
                        "0.5,0,400,2,0," +
                        curve + "|70:80,1,40\n",
                    simd, kLazer);
      CHECK_EQ(map.stats.malformed_lines, 1u);
      CHECK_EQ(map.hit_objects.size(), 3u);
      CHECK_EQ(map.sliders.size(), 2u);
      CHECK_EQ(map.slider_points.size(), 2u);
      CHECK_EQ(map.slider_segments.size(), version == 128 ? 2u : 0u);
      if (map.hit_objects.size() != 3 || map.sliders.size() != 2 ||
          map.slider_points.size() != 2)
        continue;
      CHECK_EQ(map.hit_objects[0].slider, fosu::HitObject::kNoSlider);
      CHECK_EQ(map.hit_objects[1].slider, 0u);
      CHECK_EQ(map.hit_objects[2].slider, 1u);
      for (size_t i = 0; i < 2; ++i) {
        CHECK_EQ(map.sliders[i].point_begin, i);
        CHECK_EQ(map.sliders[i].point_count, 1u);
        CHECK_EQ(map.sliders[i].segment_begin, version == 128 ? i : 0u);
        CHECK_EQ(map.sliders[i].segment_count, version == 128 ? 1u : 0u);
      }
      CHECK_EQ(map.slider_points[0].x, 10);
      CHECK_EQ(map.slider_points[0].y, 20);
      CHECK_EQ(map.slider_points[1].x, 70);
      CHECK_EQ(map.slider_points[1].y, 80);
    }
  }
}

static void test_slider_point_digit_widths() {
#if FOSU_SIMD
  // Exercise every SIMD shuffle-table entry through the resulting path.
  for (fosu::i32 first_x : {1, 12, 123, 1234})
    for (fosu::i32 first_y : {5, 56, 567, 5678})
      for (fosu::i32 second_x : {9, 98, 987, 9876})
        for (fosu::i32 second_y : {4, 43, 432, 4321}) {
          const std::string points = "B|" + std::to_string(first_x) + ":" +
                                     std::to_string(first_y) + "|" +
                                     std::to_string(second_x) + ":" +
                                     std::to_string(second_y) + ",1,10";
          const auto        map = parse_str(slider_document(points));
          CHECK_EQ(map.sliders.size(), 1u);
          if (map.sliders.empty())
            continue;
          const auto& slider = map.sliders[0];
          CHECK_EQ(slider.point_count, 2u);
          CHECK_EQ(map.slider_points[slider.point_begin].x, first_x);
          CHECK_EQ(map.slider_points[slider.point_begin].y, first_y);
          CHECK_EQ(map.slider_points[slider.point_begin + 1].x, second_x);
          CHECK_EQ(map.slider_points[slider.point_begin + 1].y, second_y);
        }
#endif
}

static void test_slider_point_pairs_resume_after_fallback() {
  const auto input = slider_document(
      "B|123:456|789:123|12.5:7.5|123:456|789:123|123:45|678:90|1:2|3:4,1,10");
  const fosu::SliderPoint expected[] = {
      {123, 456}, {789, 123}, {12, 7}, {123, 456}, {789, 123},
      {123, 45},  {678, 90},  {1, 2},  {3, 4},
  };
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd);
    CHECK_EQ(map.sliders.size(), 1u);
    CHECK_EQ(map.slider_points.size(), 9u);
    if (map.slider_points.size() != 9)
      continue;
    for (size_t i = 0; i < 9; ++i) {
      CHECK_EQ(map.slider_points[i].x, expected[i].x);
      CHECK_EQ(map.slider_points[i].y, expected[i].y);
    }
  }
}

static void test_slider_repeats_and_length() {
  struct Case {
    const char* tail;
    int32_t     slides;
    double      length;
  };
  const Case cases[] = {
      {",1", 1, 0},
      {",1,10", 1, 10},
      {", 12 , 1e2 ", 12, 100},
      {",9000,131072", 9000, 131072},
      {",-1,-5", 1, 0},
      {",1,-131072", 1, 0},
  };
  for (bool simd : {false, true}) {
    for (const auto& test : cases) {
      const auto map =
          parse_str(slider_document("B|1:2" + std::string(test.tail)), simd);
      CHECK_EQ(map.sliders.size(), 1u);
      if (map.sliders.empty())
        continue;
      CHECK_EQ(map.sliders[0].slides, test.slides);
      CHECK_EQ(map.sliders[0].length, test.length);
    }
    for (const auto tail : {"", ",", ",1,", ",9001,10", ",1,131073",
                            ",1,-131073", ",1,10,,/:0", ",1,10,,,/:0"}) {
      const auto map =
          parse_str(slider_document("B|1:2" + std::string(tail)), simd, kLazer);
      CHECK(map.hit_objects.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
}

static void test_slider_sounds() {
  // Exercise both sides of the 32-byte field scan. Extra columns after the
  // hit sample do not belong to the Slider or HitObject models.
  for (bool simd : {false, true}) {
    for (size_t size :
         {size_t(0), size_t(21), size_t(22), size_t(23), size_t(80)}) {
      const std::string edge_sounds(size, '0');
      const auto map = parse_str(slider_document("B|1:2,2,10," + edge_sounds +
                                                 ",0:0|0:0|0:0,1:2,ignored"),
                                 simd);
      CHECK_EQ(map.sliders.size(), 1u);
      if (map.sliders.empty())
        continue;
      CHECK_EQ(map.sliders[0].edge_sounds, edge_sounds);
      CHECK_EQ(map.sliders[0].edge_sets, "0:0|0:0|0:0");
      CHECK_EQ(map.hit_objects[0].hit_sample, "1:2");
    }
  }
}

static void test_hitobject_details() {
  struct Case {
    uint32_t    type;
    const char* text;
    double      end_time;
    const char* sample;
  };
  const Case cases[] = {
      {1, "", 0, ""},
      {3, ",0:0:0:0:", 0, "0:0:0:0:"},  // Circle wins over slider.
      {8, ",12.5,0:0:0:0:x,ignored", 12.5, "0:0:0:0:x"},
      {128, "", 10, ""},
      {128, ",", 10, ""},
      {128, ",12.5:0:0:0:0:", 12.5, "0:0:0:0:"},
      {128, ",12:0:0:0:0:", 12, "0:0:0:0:"},
      {128, ",000012:0:0:0:0:", 12, "0:0:0:0:"},
      {128, ",2147483647:0:0:0:0:", 2147483647, "0:0:0:0:"},
      {128, ",12.5,ignored", 12.5, ""},
      {136, ",12.5,0:0", 12.5, "0:0"},  // Spinner wins over hold.
  };
  for (const auto& test : cases) {
    const auto input = fosu_test::padded(test.text);
    const auto size = std::string_view(test.text).size();
    const auto kind = fosu::internal::classify_hitobject_kind(test.type);
    if (kind == fosu::internal::HitObjectKind::Circle) {
      const auto details = fosu::internal::parse_circle_details(
          input.data(), input.data() + size);
      CHECK(details.has_value());
      if (details)
        CHECK_EQ(details->hit_sample, test.sample);
    } else if (kind == fosu::internal::HitObjectKind::Spinner) {
      const auto details = fosu::internal::parse_spinner_details(
          input.data(), input.data() + size);
      CHECK(details.has_value());
      if (details) {
        CHECK_EQ(details->end_time, test.end_time);
        CHECK_EQ(details->hit_sample, test.sample);
      }
    } else {
      const auto details = fosu::internal::parse_hold_details(
          10, input.data(), input.data() + size);
      CHECK(details.has_value());
      if (details) {
        CHECK_EQ(details->end_time, test.end_time);
        CHECK_EQ(details->hit_sample, test.sample);
      }
    }
  }
  for (const auto text : {"", ",", ",bad", ",12:0:0", ",12,/:0"}) {
    const auto input = fosu_test::padded(text);
    CHECK(!fosu::internal::parse_spinner_details(
        input.data(), input.data() + std::string_view(text).size()));
  }
  for (const auto text : {",2147483648:0:0:0:0:", ",12x:0:0:0:0:"}) {
    const auto input = fosu_test::padded(text);
    CHECK(!fosu::internal::parse_hold_details(
        10, input.data(), input.data() + std::string_view(text).size()));
  }
}

static void test_unknown_hitsound_bits_are_kept() {
  const uint32_t values[] = {0, 1,  2,  3,  4,  5,  6,  7,  8,
                             9, 10, 11, 12, 13, 14, 15, 32, 33};
  std::string    input = "osu file format v14\n[HitObjects]\n";
  for (size_t i = 0; i < std::size(values); ++i)
    input +=
        "1,2," + std::to_string(i) + ",1," + std::to_string(values[i]) + "\n";
  for (bool simd : {false, true}) {
    const auto map = parse_str(input, simd);
    CHECK_EQ(map.stats.malformed_lines, 0u);
    CHECK_EQ(map.hit_objects.size(), std::size(values));
    for (size_t i = 0; i < map.hit_objects.size(); ++i)
      CHECK_EQ(map.hit_objects[i].hitsound, values[i]);
  }
}

static void test_non_finite_object_times_are_rejected() {
  for (const char* time : {"NaN", "Infinity", "-Infinity", "1e309"}) {
    const auto input = "osu file format v14\n[HitObjects]\n1,2," +
                       std::string(time) + ",1,0\n1,2,3,1,0\n";
    for (bool simd : {false, true}) {
      const auto map = parse_str(input, simd, kLazer);
      CHECK_EQ(map.hit_objects.size(), 1u);
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
}

// osu! adds the format's time offset, so -0 becomes +0. Lazer-format
// coordinates keep fractions and the sign of zero; older formats truncate.
static void test_negative_zero_times_and_coordinates() {
  for (bool simd : {false, true}) {
    CHECK(!std::signbit(
        parse_str("[HitObjects]\n1,2,-0,1,0\n", simd).hit_objects[0].time));
    const auto v128 = parse_str(
        "osu file format v128\n[HitObjects]\n-0,-0,1,2,0,L|-0:-0,1,10\n", simd,
        kLazer);
    CHECK(std::signbit(v128.hit_objects[0].x));
    CHECK(std::signbit(v128.hit_objects[0].y));
    CHECK(std::signbit(v128.slider_points[0].x));
    CHECK(std::signbit(v128.slider_points[0].y));
    const auto v14 = parse_str(
        "osu file format v14\n[HitObjects]\n-0,-0,1,2,0,L|-0:-0,1,10\n", simd);
    CHECK(!std::signbit(v14.hit_objects[0].x));
    CHECK(!std::signbit(v14.slider_points[0].x));
  }
}

// Kind bits take precedence: circle, slider, spinner, then hold.
static void test_exactly_one_kind_helper_is_true() {
  for (uint32_t type = 1; type < 256; ++type) {
    fosu::HitObject object{};
    object.type = type;
    const int kinds = object.is_circle() + object.is_slider() +
                      object.is_spinner() + object.is_hold();
    CHECK_EQ(kinds, (type & 139) ? 1 : 0);
  }
  const auto kind_of = [](uint32_t type) {
    fosu::HitObject object{};
    object.type = type;
    return object.is_circle()    ? 'c'
           : object.is_slider()  ? 's'
           : object.is_spinner() ? 'p'
           : object.is_hold()    ? 'h'
                                 : '-';
  };
  CHECK_EQ(kind_of(3), 'c');
  CHECK_EQ(kind_of(9), 'c');
  CHECK_EQ(kind_of(10), 's');
  CHECK_EQ(kind_of(130), 's');
  CHECK_EQ(kind_of(136), 'p');
  CHECK_EQ(kind_of(4), '-');
}

// B-spline degrees belong to lazer's v128 format. Stable cannot load one in
// any version. Lazer before v128 rejects the line, where lazer itself (unlike
// us) reads a B-spline.
static void test_bspline_degree_needs_lazer_format() {
  for (const auto* engine :
       {&fosu::internal::compiled_engine, &fosu_test::scalar_engine()}) {
    for (const char* version : {"v14", "v127", "v128"}) {
      const auto   input = std::string("osu file format ") + version +
                           "\n[HitObjects]\n0,0,0,2,0,B2|100:0|100:100,1,10\n";
      fosu::Parser parser(*engine);
      CHECK(!parser.parse(input));
      CHECK(parser.error().code == fosu::ParseErrorCode::Unloadable);
      CHECK_EQ(parser.error().line, 3u);
      if (std::string_view(version) == "v128")
        continue;
      const auto& map = require_parse(parser.parse(input, kLazer));
      CHECK(map.hit_objects.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
  for (bool simd : {false, true}) {
    const auto v128 = parse_str(
        "osu file format v128\n[HitObjects]\n0,0,0,2,0,B2|100:0|100:100,1,10\n",
        simd, kLazer);
    CHECK_EQ(v128.hit_objects.size(), 1u);
    CHECK_EQ(v128.slider_segments.size(), 1u);
    if (!v128.slider_segments.empty())
      CHECK(v128.slider_segments[0].degree == 2u);
  }
}

int main() {
  test_exactly_one_kind_helper_is_true();
  test_bspline_degree_needs_lazer_format();
  test_unknown_hitsound_bits_are_kept();
  test_non_finite_object_times_are_rejected();
  test_negative_zero_times_and_coordinates();
  test_slider_points();
  test_slider_pool_indices_after_rejected_record();
  test_slider_point_digit_widths();
  test_slider_point_pairs_resume_after_fallback();
  test_slider_repeats_and_length();
  test_slider_sounds();
  test_hitobject_details();
  return test_result();
}
