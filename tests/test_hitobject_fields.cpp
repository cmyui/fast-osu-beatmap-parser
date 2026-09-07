#include "support/test.hpp"

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
    test_slider_fields();
    test_object_tails();
    return test_result();
}
