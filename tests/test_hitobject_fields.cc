#include "support/test.h"

static std::string slider_document(std::string_view slider) {
  return "osu file format v14\n[HitObjects]\n0,0,0,2,0," + std::string(slider) + '\n';
}

static void test_slider_points() {
  struct Case {
    const char* points;
    float x;
    float y;
  };
  const Case cases[] = {
      {"|172:44", 172, 44},
      {"|-1.9:2.9", -1, 2},
      {"|1:2e1", 1, 20},
      {"|131072:-131072", 131072, -131072},
  };
  for (bool simd : {false, true}) {
    for (const auto& test : cases) {
      const auto map =
          parse_str(slider_document("B" + std::string(test.points) + ",1,10"), simd);
      CHECK_EQ(map.hit_objects.size(), 1u);
      CHECK_EQ(map.sliders.size(), 1u);
      if (map.sliders.empty())
        continue;
      const auto& slider = map.sliders[0];
      CHECK_EQ(slider.point_count, 1u);
      CHECK_EQ(map.slider_points[slider.point_begin].x, test.x);
      CHECK_EQ(map.slider_points[slider.point_begin].y, test.y);
    }

    for (const auto points : {"|", "|1", "|1:", "|:2", "|bad:2", "|1:bad", "|131073:2"}) {
      const auto map =
          parse_str(slider_document("B" + std::string(points) + ",1,10"), simd);
      CHECK(map.hit_objects.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
}

static void test_slider_point_digit_widths() {
#if FOSU_SIMD
  // Exercise every SIMD shuffle-table entry through the resulting path.
  for (int first_x : {1, 12, 123, 1234})
    for (int first_y : {5, 56, 567, 5678})
      for (int second_x : {9, 98, 987, 9876})
        for (int second_y : {4, 43, 432, 4321}) {
          const std::string points =
              "B|" + std::to_string(first_x) + ":" + std::to_string(first_y) + "|" +
              std::to_string(second_x) + ":" + std::to_string(second_y) + ",1,10";
          const auto map = parse_str(slider_document(points));
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

static void test_slider_repeats_and_length() {
  struct Case {
    const char* tail;
    int32_t slides;
    double length;
  };
  const Case cases[] = {
      {",1", 1, 0},
      {",1,10", 1, 10},
      {", 12 , 1e2 ", 12, 100},
      {",9000,131072", 9000, 131072},
      {",-1,-5", 1, 0},
  };
  for (bool simd : {false, true}) {
    for (const auto& test : cases) {
      const auto map = parse_str(slider_document("B|1:2" + std::string(test.tail)), simd);
      CHECK_EQ(map.sliders.size(), 1u);
      if (map.sliders.empty())
        continue;
      CHECK_EQ(map.sliders[0].slides, test.slides);
      CHECK_EQ(map.sliders[0].length, test.length);
    }
    for (const auto tail :
         {"", ",", ",1,", ",9001,10", ",1,131073", ",1,10,,/:0", ",1,10,,,/:0"}) {
      const auto map = parse_str(slider_document("B|1:2" + std::string(tail)), simd);
      CHECK(map.hit_objects.empty());
      CHECK_EQ(map.stats.malformed_lines, 1u);
    }
  }
}

static void test_slider_sounds() {
  // Exercise both sides of the 32-byte field scan. Extra columns after the
  // hit sample do not belong to the Slider or HitObject models.
  for (bool simd : {false, true}) {
    for (size_t size : {size_t(0), size_t(21), size_t(22), size_t(23), size_t(80)}) {
      const std::string edge_sounds(size, '0');
      const auto map = parse_str(
          slider_document("B|1:2,2,10," + edge_sounds + ",0:0|0:0|0:0,1:2,ignored"),
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
    uint32_t type;
    const char* text;
    double end_time;
    const char* sample;
  };
  const Case cases[] = {
      {1, "", 0, ""},
      {3, ",0:0:0:0:", 0, "0:0:0:0:"},  // Circle wins over slider.
      {8, ",12.5,0:0:0:0:x,ignored", 12.5, "0:0:0:0:x"},
      {128, "", 10, ""},
      {128, ",", 10, ""},
      {128, ",12.5:0:0:0:0:", 12.5, "0:0:0:0:"},
      {128, ",12.5,ignored", 12.5, ""},
      {136, ",12.5,0:0", 12.5, "0:0"},  // Spinner wins over hold.
  };
  for (const auto& test : cases) {
    auto input = fosu::make_padded(test.text);
    const auto kind = fosu::internal::classify_hitobject_kind(test.type);
    if (kind == fosu::internal::HitObjectKind::Circle) {
      const auto details = fosu::internal::parse_circle_details(
          input.data.get(), input.data.get() + input.size);
      CHECK(details.has_value());
      if (details)
        CHECK_EQ(details->hit_sample, test.sample);
    } else if (kind == fosu::internal::HitObjectKind::Spinner) {
      const auto details = fosu::internal::parse_spinner_details(
          input.data.get(), input.data.get() + input.size);
      CHECK(details.has_value());
      if (details) {
        CHECK_EQ(details->end_time, test.end_time);
        CHECK_EQ(details->hit_sample, test.sample);
      }
    } else {
      const auto details = fosu::internal::parse_hold_details(
          10, input.data.get(), input.data.get() + input.size);
      CHECK(details.has_value());
      if (details) {
        CHECK_EQ(details->end_time, test.end_time);
        CHECK_EQ(details->hit_sample, test.sample);
      }
    }
  }
  for (const auto text : {"", ",", ",bad", ",12:0:0", ",12,/:0"}) {
    auto input = fosu::make_padded(text);
    CHECK(!fosu::internal::parse_spinner_details(input.data.get(),
                                                 input.data.get() + input.size));
  }
}

int main() {
  test_slider_points();
  test_slider_point_digit_widths();
  test_slider_repeats_and_length();
  test_slider_sounds();
  test_hitobject_details();
  return test_result();
}
