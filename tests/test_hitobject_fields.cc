#include "support/test.h"

template <typename Point>
static void test_point_values() {
    const fosu::internal::HitObjectParseConstants constants;
    struct Case { const char* text; int32_t x, y; const char* remaining; };
    const Case cases[] = {
        {"|172:44,1", 172, 44, ",1"},
        {"|1:2|3:4", 1, 2, "|3:4"},
        {"|-1.9:2.9,", -1, 2, ","},
        {"|1:2e1,", 1, 20, ","},
        {"|131072:-131072,", 131072, -131072, ","},
        {"|1:2", 1, 2, ""},
    };
    for (const auto& test : cases) {
        const auto input = fosu::make_padded(test.text);
        const char* end = input.data.get() + input.size;
        const auto point = fosu::internal::parse_slider_point<Point>(
            input.data.get(), end, constants);
        CHECK(point.has_value());
        if (!point) continue;
        CHECK_EQ(point->value.x, test.x);
        CHECK_EQ(point->value.y, test.y);
        CHECK_EQ(std::string_view(point->next, end - point->next), test.remaining);
    }
    for (const auto text : {"", "1:2", "|", "|1", "|1:", "|:2", "|bad:2", "|1:bad", "|131073:2"}) {
        const auto input = fosu::make_padded(text);
        CHECK(!fosu::internal::parse_slider_point<Point>(
            input.data.get(), input.data.get() + input.size, constants));
    }
#if FOSU_SIMD
    // The pair shortcut returns actual coordinates; unsupported spellings
    // stay available to the general parser rather than being rejected.
    for (const auto suffix : {",1", "|3:4,1", "|3:4|5:6,1", "|bad,1"}) {
        const auto input = fosu::make_padded(std::string("|1:2") + suffix);
        const auto points = fosu::internal::try_parse_slider_point_prefix_fast<Point>(
            input.data.get(), constants);
        CHECK(points.has_value());
        if (!points) continue;
        CHECK_EQ(points->first.x, 1);
        CHECK_EQ(points->first.y, 2);
        const bool has_second = std::string_view(suffix).starts_with("|3:4");
        CHECK_EQ(points->has_second, has_second);
        CHECK_EQ(points->next - input.data.get(), has_second ? 8 : 4);
        if (points->has_second) {
            CHECK_EQ(points->second.x, 3);
            CHECK_EQ(points->second.y, 4);
        }
    }
    const auto input = fosu::make_padded("|-1:2,1");
    CHECK(!fosu::internal::try_parse_slider_point_prefix_fast<Point>(
        input.data.get(), constants));
#endif
}

static void test_point_prefix_boundaries() {
#if FOSU_SIMD
  const fosu::internal::HitObjectParseConstants constants;
  // Exhausted masks must reject or retain only the complete first point.
  for (const std::string prefix : {"|", "|1:", "|1:2|", "|1:2|3:"}) {
    const auto input = fosu::make_padded(prefix + std::string(64, '1'));
    const auto points =
        fosu::internal::try_parse_slider_point_prefix_fast<fosu::SliderPoint>(
            input.data.get(), constants);
    CHECK_EQ(points.has_value(), prefix.starts_with("|1:2|"));
    if (points) {
      CHECK(!points->has_second);
      CHECK_EQ(points->next - input.data.get(), 4);
    }
  }
  // Combining delimiter classifications must not admit other byte values.
  for (unsigned byte = 0; byte < 256; ++byte) {
    const auto input =
        fosu::make_padded(std::string("|1:2|3:4") + static_cast<char>(byte));
    const auto points =
        fosu::internal::try_parse_slider_point_prefix_fast<fosu::SliderPoint>(
            input.data.get(), constants);
    CHECK(points.has_value());
    if (!points)
      continue;
    const bool has_second = byte == '|' || byte == ',';
    CHECK_EQ(points->has_second, has_second);
    CHECK_EQ(points->next - input.data.get(), has_second ? 8 : 4);
    if (has_second) {
      CHECK_EQ(points->second.x, 3);
      CHECK_EQ(points->second.y, 4);
    }
  }
#endif
}

static void test_point_prefix_digit_widths() {
#if FOSU_SIMD
    const fosu::internal::HitObjectParseConstants constants;
    // Exercise every shuffle-table index for both points, independently.
    for (int first_x : {1, 12, 123, 1234})
    for (int first_y : {5, 56, 567, 5678})
    for (int second_x : {9, 98, 987, 9876})
    for (int second_y : {4, 43, 432, 4321}) {
        const std::string text =
            "|" + std::to_string(first_x) + ":" + std::to_string(first_y) +
            "|" + std::to_string(second_x) + ":" + std::to_string(second_y);
        const auto input = fosu::make_padded(text + ",1");
        const auto points =
            fosu::internal::try_parse_slider_point_prefix_fast<fosu::SliderPoint>(
                input.data.get(), constants);
        CHECK(points.has_value());
        if (!points) continue;
        CHECK(points->has_second);
        CHECK_EQ(points->first.x, first_x);
        CHECK_EQ(points->first.y, first_y);
        CHECK_EQ(points->second.x, second_x);
        CHECK_EQ(points->second.y, second_y);
        CHECK_EQ(points->next - input.data.get(), text.size());
    }
#endif
}

static void test_slider_fields() {
    const fosu::internal::HitObjectParseConstants constants;
    // Missing length differs from an explicitly empty field.
    struct Case { const char* text; int32_t slides; double length; };
    const Case cases[] = {
        {",1", 1, 0}, {",1,10", 1, 10}, {", 12 , 1e2 ", 12, 100},
        {",9000,131072", 9000, 131072}, {",-1,-5", -1, -5},
    };
    for (const auto& test : cases) {
        auto input = fosu::make_padded(test.text);
        const auto tail = fosu::internal::parse_slider_tail(
            input.data.get(), input.data.get() + input.size, constants);
        CHECK(tail.has_value());
        if (!tail) continue;
        CHECK_EQ(tail->slides, test.slides);
        CHECK_EQ(tail->length, test.length);
    }
    for (const auto text : {"", ",", ",1,", ",9001,10", ",1,131073",
                            ",1,10,,/:0", ",1,10,,,/:0"}) {
        auto input = fosu::make_padded(text);
        CHECK(!fosu::internal::parse_slider_tail(
            input.data.get(), input.data.get() + input.size, constants));
    }
    // Exercise either side of the 32-byte optional-field scan, including
    // truncation at the next comma and empty positional fields.
    for (size_t size : {size_t(0), size_t(21), size_t(22), size_t(23), size_t(80)}) {
        const std::string sounds(size, '0');
        auto input = fosu::make_padded(",2,10," + sounds + ",0:0,1:2,x");
        const auto tail = fosu::internal::parse_slider_tail(
            input.data.get(), input.data.get() + input.size, constants);
        CHECK(tail.has_value());
        if (!tail) continue;
        CHECK_EQ(tail->slides, 2);
        CHECK_EQ(tail->length, 10);
        CHECK_EQ(tail->sounds.edge_sounds, sounds);
        CHECK_EQ(tail->sounds.edge_sets, "0:0");
        CHECK_EQ(tail->sounds.hit_sample, "1:2");
    }
}

static void test_slider_sound_boundaries() {
  const fosu::internal::HitObjectParseConstants constants;
  for (size_t length : {0u, 1u, 30u, 31u, 32u, 33u, 63u, 64u, 65u, 95u}) {
    for (size_t long_field = 0; long_field < 3; ++long_field) {
      std::string fields[] = {"0", "0:0", "0:0:0:0:sample.wav"};
      fields[long_field] = std::string(length, 'x');
      for (size_t count = 1; count <= 4; ++count) {
        std::string text;
        for (size_t i = 0; i < count; ++i) {
          if (i)
            text += ',';
          text += i < 3 ? fields[i] : "ignored";
        }
        // The following comma is readable, but outside the field span.
        const auto input = fosu::make_padded(text + ",outside\n");
        const auto sounds = fosu::internal::parse_slider_sound_fields(
            input.data.get(), input.data.get() + text.size(), constants);
        CHECK_EQ(sounds.edge_sounds, fields[0]);
        CHECK_EQ(sounds.edge_sets, count >= 2 ? fields[1] : "");
        CHECK_EQ(sounds.hit_sample, count >= 3 ? fields[2] : "");
      }
    }
  }
}

static void test_hitobject_details() {
    struct Case { uint32_t type; const char* text; double end_time; const char* sample; };
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
            if (details) CHECK_EQ(details->hit_sample, test.sample);
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
        CHECK(!fosu::internal::parse_spinner_details(
            input.data.get(), input.data.get() + input.size));
    }
}

int main() {
    test_point_values<fosu::SliderPoint>();
    test_point_prefix_boundaries();
    test_point_prefix_digit_widths();
    test_slider_fields();
    test_slider_sound_boundaries();
    test_hitobject_details();
    return test_result();
}
