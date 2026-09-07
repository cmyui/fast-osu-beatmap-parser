#include "support/test.hpp"

template <typename Point>
static void test_point_values() {
    const fosu::internal::HitConsts constants;
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
        const auto point = fosu::internal::parse_point<Point>(input.data.get(), end, constants);
        CHECK(point.has_value());
        if (!point) continue;
        CHECK_EQ(point->value.x, test.x);
        CHECK_EQ(point->value.y, test.y);
        CHECK_EQ(std::string_view(point->next, end - point->next), test.remaining);
    }
    for (const auto text : {"", "1:2", "|", "|1", "|1:", "|:2", "|bad:2", "|1:bad", "|131073:2"}) {
        const auto input = fosu::make_padded(text);
        CHECK(!fosu::internal::parse_point<Point>(
            input.data.get(), input.data.get() + input.size, constants));
    }
#if FOSU_SIMD
    // The pair shortcut returns actual coordinates; unsupported spellings
    // stay available to the general parser rather than being rejected.
    for (const auto suffix : {",1", "|3:4,1", "|3:4|5:6,1", "|bad,1"}) {
        const auto input = fosu::make_padded(std::string("|1:2") + suffix);
        const auto pair = fosu::internal::fast_parse_point_pair<Point>(input.data.get(), constants);
        CHECK(pair.has_value());
        if (!pair) continue;
        CHECK_EQ(pair->first.x, 1);
        CHECK_EQ(pair->first.y, 2);
        const bool has_second = std::string_view(suffix).starts_with("|3:4");
        CHECK_EQ(pair->has_second, has_second);
        CHECK_EQ(pair->next - input.data.get(), has_second ? 8 : 4);
        if (pair->has_second) {
            CHECK_EQ(pair->second.x, 3);
            CHECK_EQ(pair->second.y, 4);
        }
    }
    const auto input = fosu::make_padded("|-1:2,1");
    CHECK(!fosu::internal::fast_parse_point_pair<Point>(input.data.get(), constants));
#endif
}

static void test_slider_fields() {
    const fosu::internal::HitConsts constants;
    // Missing length differs from an explicitly empty field.
    struct Case { const char* text; int32_t slides; double length; };
    const Case cases[] = {
        {",1", 1, 0}, {",1,10", 1, 10}, {", 12 , 1e2 ", 12, 100},
        {",9000,131072", 9000, 131072}, {",-1,-5", -1, -5},
    };
    for (const auto& test : cases) {
        auto input = fosu::make_padded(test.text);
        const auto fields = fosu::internal::parse_slider_fields(
            input.data.get(), input.data.get() + input.size, constants);
        CHECK(fields.has_value());
        if (!fields) continue;
        CHECK_EQ(fields->slides, test.slides);
        CHECK_EQ(fields->length, test.length);
    }
    for (const auto text : {"", ",", ",1,", ",9001,10", ",1,131073",
                            ",1,10,,/:0", ",1,10,,,/:0"}) {
        auto input = fosu::make_padded(text);
        CHECK(!fosu::internal::parse_slider_fields(
            input.data.get(), input.data.get() + input.size, constants));
    }
    // Exercise either side of the 32-byte optional-field scan, including
    // truncation at the next comma and empty positional fields.
    for (size_t size : {size_t(0), size_t(21), size_t(22), size_t(23), size_t(80)}) {
        const std::string sounds(size, '0');
        auto input = fosu::make_padded(",2,10," + sounds + ",0:0,1:2,x");
        const auto fields = fosu::internal::parse_slider_fields(
            input.data.get(), input.data.get() + input.size, constants);
        CHECK(fields.has_value());
        if (!fields) continue;
        CHECK_EQ(fields->slides, 2);
        CHECK_EQ(fields->length, 10);
        CHECK_EQ(fields->extras.edge_sounds, sounds);
        CHECK_EQ(fields->extras.edge_sets, "0:0");
        CHECK_EQ(fields->extras.hit_sample, "1:2");
    }
}

static void test_object_tails() {
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
        const auto tail = fosu::internal::parse_object_tail(
            test.type, 10, input.data.get(), input.data.get() + input.size);
        CHECK(tail.has_value());
        if (!tail) continue;
        CHECK_EQ(tail->end_time, test.end_time);
        CHECK_EQ(tail->sample, test.sample);
    }
    for (const auto text : {"", ",", ",bad", ",12:0:0", ",12,/:0"}) {
        auto input = fosu::make_padded(text);
        CHECK(!fosu::internal::parse_object_tail(
            8, 10, input.data.get(), input.data.get() + input.size));
    }
}

int main() {
    test_point_values<fosu::SliderPoint>();
    test_point_values<fosu_point>();
    test_slider_fields();
    test_object_tails();
    return test_result();
}
