#include "support/test.hpp"
#include "support/equality.hpp"

static void test_reuse_shrinks_without_reallocating() {
    auto initial = fosu::make_padded(
        "[HitObjects]\n16,32,100,1,0\n32,64,200,1,0\n48,96,300,1,0\n");
    auto replacement = fosu::make_padded("[HitObjects]\n80,160,400,1,2\n");
    fosu::Beatmap bm;
    fosu::parse_into(initial, bm);
    const auto* allocation = bm.hit_objects.data();
    const size_t capacity = bm.hit_objects.capacity();
    fosu::parse_into(replacement, bm);
    CHECK_EQ(bm.hit_objects.size(), 1u);
    CHECK(bm.hit_objects.data() == allocation);
    CHECK_EQ(bm.hit_objects.capacity(), capacity);
    CHECK_EQ(canonical(bm), canonical(fosu::parse(replacement)));
}

static void test_reuse_grows_past_existing_capacity() {
    auto initial = fosu::make_padded("[HitObjects]\n24,48,500,1,0\n");
    fosu::Beatmap bm;
    fosu::parse_into(initial, bm);
    const size_t old_capacity = bm.hit_objects.capacity();
    std::string text = "[HitObjects]\n";
    for (size_t i = 0; i <= old_capacity; ++i)
        text += "72,144,600,1,4\n";
    auto replacement = fosu::make_padded(text);
    fosu::parse_into(replacement, bm);
    CHECK_EQ(bm.hit_objects.size(), old_capacity + 1);
    CHECK(bm.hit_objects.capacity() > old_capacity);
    CHECK_EQ(canonical(bm), canonical(fosu::parse(replacement)));
}

static void test_reuse_clears_omitted_sections() {
    auto initial = fosu::make_padded(
        "[General]\nAudioFilename:previous.mp3\nSampleSet:Soft\n"
        "[Editor]\nGridSize:16\n"
        "[Metadata]\nTitle:Previous title\nBeatmapID:123\n"
        "[Difficulty]\nOverallDifficulty:9\nApproachRate:10\n"
        "[Events]\n0,0,\"previous.jpg\",0,0\n2,50,100\n"
        "[TimingPoints]\n0,500\n"
        "[Colours]\nCombo1:0,128,255\n"
        "[HitObjects]\n64,96,700,2,0,B|128:192|192:96,1,200\n");
    auto replacement = fosu::make_padded("[Metadata]\nTitle:Replacement title\n");
    fosu::Beatmap bm;
    fosu::parse_into(initial, bm);
    CHECK(!bm.sliders.empty() && !bm.timing_points.empty());
    fosu::parse_into(replacement, bm);
    CHECK(bm.title == "Replacement title");
    CHECK_EQ(bm.beatmap_id, -1);
    CHECK(bm.audio_filename.empty() && bm.background.empty());
    CHECK(bm.sample_set == "Normal");
    CHECK_EQ(bm.grid_size, 4);
    CHECK_EQ(bm.od, 5);
    CHECK_EQ(bm.ar, 5);
    CHECK(bm.breaks.empty() && bm.combo_colours.empty());
    CHECK(bm.timing_points.empty() && bm.hit_objects.empty());
    CHECK(bm.sliders.empty() && bm.slider_points.empty());
    CHECK_EQ(canonical(bm), canonical(fosu::parse(replacement)));
}

static void test_reuse_resets_empty_document() {
    auto initial = fosu::make_padded(
        "[General]\nStackLeniency:0.2\nSampleSet:Drum\n"
        "[Metadata]\nTitle:Before empty input\n"
        "[HitObjects]\ninvalid\n96,192,800,1,0\n");
    auto empty = fosu::make_padded("");
    fosu::Beatmap bm;
    fosu::parse_into(initial, bm);
    CHECK_EQ(bm.stats.malformed_lines, 1u);
    fosu::parse_into(empty, bm);
    CHECK(bm.title.empty() && bm.hit_objects.empty());
    CHECK_EQ(bm.stats.malformed_lines, 0u);
    CHECK_EQ(bm.stats.fast_path_lines, 0u);
    CHECK_EQ(bm.stats.slow_path_lines, 0u);
    CHECK(std::abs(bm.stack_leniency - 0.7) < 1e-12);
    CHECK(bm.sample_set == "Normal");
    CHECK_EQ(canonical(bm), canonical(fosu::parse(empty)));
}

void test_read_into_reuse() {
    printf("read_into reuse\n");
    char path[] = "/tmp/fosu_read_into_XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    const std::string big(10000, 'A');
    const std::string little(100, 'B');
    CHECK_EQ(write(fd, big.data(), big.size()),
             static_cast<ssize_t>(big.size()));
    close(fd);

    fosu::FileBuffer buf;
    CHECK(fosu::read_into(path, buf));
    CHECK_EQ(buf.size, big.size());
    CHECK(memcmp(buf.data.get(), big.data(), big.size()) == 0);
    const char* alloc0 = buf.data.get();
    const size_t cap0 = buf.capacity;

    FILE* f = fopen(path, "wb");
    fwrite(little.data(), 1, little.size(), f);
    fclose(f);

    CHECK(fosu::read_into(path, buf));
    CHECK_EQ(buf.size, little.size());
    CHECK(buf.data.get() == alloc0);  // allocation reused
    CHECK_EQ(buf.capacity, cap0);
    CHECK(memcmp(buf.data.get(), little.data(), little.size()) == 0);
    bool pad_zero = true;
    for (size_t i = 0; i < fosu::kBufferPadding; ++i)
        pad_zero &= buf.data[buf.size + i] == 0;
    CHECK(pad_zero);

    unlink(path);
    CHECK(!fosu::read_into("/nonexistent/fosu-no-such-file", buf));
}

template <typename T>
inline bool vector_layout_ok(const std::vector<T>& v) {
    const T* raw[3];
    static_assert(sizeof(v) == sizeof(raw));
    const void* const object = &v;
    memcpy(raw, object, sizeof raw);
    return raw[0] == v.data() && raw[1] == v.data() + v.size() &&
           raw[2] == v.data() + v.capacity();
}
// Instantiate layout checks only when the implementation uses that layout;
// debug standard libraries may give vector a different representation.
template <typename T>
static void test_vector_layout() {
    if constexpr (fosu::internal::kDirectVectorWrites) {
        std::vector<T> records;
        records.reserve(9);
        records.resize(3);
        CHECK(vector_layout_ok(records));
        fosu::internal::set_vector_size(records, 2);
        CHECK_EQ(records.size(), (size_t)2);
        CHECK_EQ(records.capacity(), (size_t)9);
    }
}

template <typename Map>
static void test_vector_storage() {
    test_vector_layout<typename Map::HitObject>();
    test_vector_layout<typename Map::Slider>();
    test_vector_layout<typename Map::SliderPoint>();
    test_vector_layout<typename Map::TimingPoint>();

    // Short lines exceed the initial object/timing estimates. Slider-only
    // input separately grows both slider pools. Repeated headers append to
    // each pool, while a subsequent parse_into clears it for reuse.
    std::string circles = "[HitObjects]\n";
    std::string sliders = "[HitObjects]\n";
    std::string timing = "[TimingPoints]\n";
    for (int i = 0; i < 3000; ++i) {
        if (i == 1500) {
            circles += "[HitObjects]\n";
            sliders += "[HitObjects]\n";
            timing += "[TimingPoints]\n";
        }
        circles += "1,2,3,1,0\n";
        if (i == 0 || i == 1500) {
            // A broken point list drops its points; a later failure keeps
            // completed points as orphans but publishes no slider/object.
            sliders += "1,2,3,2,0,B|1:2|bad,1,10\n";
            sliders += "1,2,3,2,0,B|7:8|9:10,9001,10\n";
        }
        sliders += "1,2,3,2,0,B|1:2|3:4|5:6,1,10\n";
        timing += "0,500\n";
    }
    auto circle_input = fosu::make_padded(circles);
    auto slider_input = fosu::make_padded(sliders);
    auto timing_input = fosu::make_padded(timing);
    auto empty_section = fosu::make_padded("[HitObjects]\n[HitObjects]\n");
    for (bool simd : {false, true}) {
        Map bm;
        fosu::parse_into(empty_section, bm, {.use_simd = simd});
        CHECK(bm.hit_objects.empty() && bm.slider_points.empty());
        fosu::parse_into(circle_input, bm, {.use_simd = simd});
        CHECK_EQ(bm.hit_objects.size(), (size_t)3000);
        CHECK(bm.sliders.empty() && bm.slider_points.empty());
        CHECK_EQ(bm.hit_objects.back().slider, Map::HitObject::kNoSlider);
        CHECK(bm.resolve(bm.hit_objects.back().hit_sample).empty());

        fosu::parse_into(slider_input, bm, {.use_simd = simd});
        CHECK_EQ(bm.hit_objects.size(), (size_t)3000);
        CHECK_EQ(bm.sliders.size(), (size_t)3000);
        CHECK_EQ(bm.slider_points.size(), (size_t)9004);
        CHECK_EQ(bm.stats.malformed_lines, 4u);
        for (size_t i = 0; i < 3000; ++i) {
            const auto& s = bm.sliders[i];
            CHECK_EQ(bm.hit_objects[i].slider, i);
            CHECK_EQ(s.point_begin, 3 * i + (i < 1500 ? 2 : 4));
            CHECK_EQ(s.point_count, 3u);
            CHECK_EQ(bm.slider_points[s.point_begin].x, 1);
            CHECK_EQ(bm.slider_points[s.point_begin + 2].y, 6);
        }
        for (size_t index : {size_t(0), size_t(4502)}) {
            CHECK_EQ(bm.slider_points[index].x, 7);
            CHECK_EQ(bm.slider_points[index + 1].y, 10);
        }

        fosu::parse_into(timing_input, bm, {.use_simd = simd});
        CHECK_EQ(bm.timing_points.size(), (size_t)3000);
        CHECK_EQ(bm.timing_points.back().beat_length, 500);
        CHECK(bm.hit_objects.empty() && bm.sliders.empty() && bm.slider_points.empty());
        fosu::parse_into(circle_input, bm, {.use_simd = simd});
        CHECK_EQ(bm.hit_objects.size(), (size_t)3000);
        CHECK(bm.timing_points.empty() && bm.sliders.empty() && bm.slider_points.empty());
        CHECK_EQ(bm.stats.malformed_lines, 0u);
    }
}

static void test_fractional_reuse() {
    auto input = fosu::make_padded(
        "[Events]\n2,1.25,2.75\n[HitObjects]\n1,2,3.5,12,0,5.75\n");
    fosu::Beatmap reused;
    fosu::parse_into(input, reused);
    const auto expected = canonical(fosu::parse(input));
    CHECK_EQ(canonical(reused), expected);
    // The comparator must see changes within an integer millisecond.
    reused.breaks[0].start += 0.125;
    CHECK(canonical(reused) != expected);
    fosu::parse_into(input, reused);
    reused.hit_objects[0].time += 0.125;
    CHECK(canonical(reused) != expected);
    fosu::parse_into(input, reused);
    reused.hit_objects[0].end_time += 0.125;
    CHECK(canonical(reused) != expected);
}

int main() {
    test_vector_storage<fosu::Beatmap>();
    test_vector_storage<fosu::OffsetBeatmap>();
    test_reuse_shrinks_without_reallocating();
    test_reuse_grows_past_existing_capacity();
    test_reuse_clears_omitted_sections();
    test_reuse_resets_empty_document();
    test_read_into_reuse();
    test_fractional_reuse();
    return test_result();
}
