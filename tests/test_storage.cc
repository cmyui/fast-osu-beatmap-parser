#include "support/equality.h"
#include "support/test.h"

static void test_reparse_reuses_arena_memory() {
  auto input = fosu::make_padded("[HitObjects]\n16,32,100,1,0\n32,64,200,1,0\n");
  fosu::Parser parser;
  const auto& first = require_parse(parser.parse(input));
  const auto* allocation = first.hit_objects.data();

  const auto& second = require_parse(parser.parse(input));
  CHECK_EQ(second.hit_objects.size(), 2u);
  CHECK(second.hit_objects.data() == allocation);
  fosu::Parser expected;
  CHECK_EQ(canonical(second), canonical(require_parse(expected.parse(input))));
}

static void test_reparse_accepts_larger_arrays() {
  auto initial = fosu::make_padded("[HitObjects]\n24,48,500,1,0\n");
  std::string text = "[HitObjects]\n";
  for (size_t i = 0; i < 5000; ++i)
    text += "72,144,600,1,4\n";
  auto replacement = fosu::make_padded(text);

  fosu::Parser parser;
  require_parse(parser.parse(initial));
  const auto& beatmap = require_parse(parser.parse(replacement));
  CHECK_EQ(beatmap.hit_objects.size(), 5000u);
  fosu::Parser expected;
  CHECK_EQ(canonical(beatmap), canonical(require_parse(expected.parse(replacement))));
}

static void test_reparse_clears_omitted_sections() {
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
  fosu::Parser parser;
  const auto& initial_map = require_parse(parser.parse(initial));
  CHECK(!initial_map.sliders.empty() && !initial_map.timing_points.empty());

  const auto& beatmap = require_parse(parser.parse(replacement));
  CHECK(beatmap.title == "Replacement title");
  CHECK_EQ(beatmap.beatmap_id, -1);
  CHECK(beatmap.audio_filename.empty() && beatmap.background.empty());
  CHECK(beatmap.sample_set == fosu::SampleSet::Normal);
  CHECK_EQ(beatmap.grid_size, 0);
  CHECK_EQ(beatmap.od, 5);
  CHECK_EQ(beatmap.ar, 5);
  CHECK(beatmap.breaks.empty() && beatmap.combo_colours.empty());
  CHECK(beatmap.timing_points.empty() && beatmap.hit_objects.empty());
  CHECK(beatmap.sliders.empty() && beatmap.slider_points.empty());
  fosu::Parser expected;
  CHECK_EQ(canonical(beatmap), canonical(require_parse(expected.parse(replacement))));
}

static void test_empty_reparse_resets_defaults() {
  auto initial = fosu::make_padded(
      "[General]\nStackLeniency:0.2\nSampleSet:Drum\n"
      "[Metadata]\nTitle:Before empty input\n"
      "[HitObjects]\ninvalid\n96,192,800,1,0\n");
  auto empty = fosu::make_padded("");
  fosu::Parser parser;
  const auto& initial_map = require_parse(parser.parse(initial));
  CHECK_EQ(initial_map.stats.malformed_lines, 1u);

  const auto& beatmap = require_parse(parser.parse(empty));
  CHECK(beatmap.title.empty() && beatmap.hit_objects.empty());
  CHECK_EQ(beatmap.stats.malformed_lines, 0u);
  CHECK_EQ(beatmap.stats.fast_path_lines, 0u);
  CHECK_EQ(beatmap.stats.slow_path_lines, 0u);
  CHECK_EQ(beatmap.stack_leniency, double(0.7f));
  CHECK(beatmap.sample_set == fosu::SampleSet::Normal);
  fosu::Parser expected;
  CHECK_EQ(canonical(beatmap), canonical(require_parse(expected.parse(empty))));
}

static void test_large_arena_arrays() {
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
      sliders += "1,2,3,2,0,B|1:2|bad,1,10\n";
      sliders += "1,2,3,2,0,B|7:8|9:10,9001,10\n";
    }
    sliders += "1,2,3,2,0,B|1:2|3:4|5:6,1,10\n";
    timing += "0,500\n";
  }
  auto circle_input = fosu::make_padded(circles);
  auto slider_input = fosu::make_padded(sliders);
  auto timing_input = fosu::make_padded(timing);

  for (bool simd : {false, true}) {
    fosu::Parser parser(simd ? fosu::internal::compiled_engine
                             : fosu_test::scalar_engine());
    const auto& circles_map = require_parse(parser.parse(circle_input));
    CHECK_EQ(circles_map.hit_objects.size(), 3000u);
    CHECK(circles_map.sliders.empty() && circles_map.slider_points.empty());
    CHECK_EQ(circles_map.hit_objects.back().slider, fosu::HitObject::kNoSlider);
    CHECK(circles_map.hit_objects.back().hit_sample.empty());

    const auto& sliders_map = require_parse(parser.parse(slider_input));
    CHECK_EQ(sliders_map.hit_objects.size(), 3000u);
    CHECK_EQ(sliders_map.sliders.size(), 3000u);
    CHECK_EQ(sliders_map.slider_points.size(), 9004u);
    CHECK_EQ(sliders_map.stats.malformed_lines, 4u);
    for (size_t i = 0; i < 3000; ++i) {
      const auto& slider = sliders_map.sliders[i];
      CHECK_EQ(sliders_map.hit_objects[i].slider, i);
      CHECK_EQ(slider.point_begin, 3 * i + (i < 1500 ? 2 : 4));
      CHECK_EQ(slider.point_count, 3u);
      CHECK_EQ(sliders_map.slider_points[slider.point_begin].x, 1);
      CHECK_EQ(sliders_map.slider_points[slider.point_begin + 2].y, 6);
    }

    const auto& timing_map = require_parse(parser.parse(timing_input));
    CHECK_EQ(timing_map.timing_points.size(), 3000u);
    CHECK_EQ(timing_map.timing_points.back().beat_length, 500);
    CHECK(timing_map.hit_objects.empty());
  }
}

static void test_rejected_slider_points() {
  struct Case {
    const char* tail;
    size_t retained_points;
  };
  const Case cases[] = {
      {"B|7:8|bad,1,10", 0},       {"B|7:8|9:10", 2},
      {"B|7:8|9:10,bad,10", 2},    {"B|7:8|9:10,1,131073", 2},
      {"B|7:8|9:10,1,10,,/:0", 2}, {"B|7:8|9:10,1,10,,,/:0", 2},
  };
  for (const auto& test : cases) {
    auto input = fosu::make_padded(std::string("[HitObjects]\n1,2,3,2,0,") + test.tail +
                                   "\n1,2,4,2,0,L|11:12,1,10\n");
    for (bool simd : {false, true}) {
      fosu::Parser parser(simd ? fosu::internal::compiled_engine
                               : fosu_test::scalar_engine());
      const auto& beatmap = require_parse(parser.parse(input));
      CHECK_EQ(beatmap.stats.malformed_lines, 1u);
      CHECK_EQ(beatmap.hit_objects.size(), 1u);
      CHECK_EQ(beatmap.sliders.size(), 1u);
      CHECK_EQ(beatmap.slider_points.size(), test.retained_points + 1);
      CHECK_EQ(beatmap.sliders[0].point_begin, test.retained_points);
      CHECK_EQ(beatmap.slider_points[test.retained_points].x, 11);
    }
  }
}

static void test_copy_owns_all_data() {
  fosu::Arena* program_arena = fosu::arena_alloc();
  CHECK(program_arena != nullptr);
  fosu::Beatmap owned{};
  std::string expected;
  {
    auto input = fosu::make_padded(
        "[General]\nAudioFilename:song.mp3\n"
        "[Metadata]\nTitle:Owned title\nArtist:Owned artist\n"
        "[Events]\n0,0,\"background.jpg\",0,0\n2,10,20\n"
        "[TimingPoints]\n0,500\n"
        "[Colours]\nCombo1:1,2,3\n"
        "[HitObjects]\n"
        "1,2,3,1,0,1:2:3:4:sample.wav\n"
        "1,2,4,2,0,B|7:8|9:10,1,20,2|0,1:2|3:4,"
        "1:2:3:4:slider.wav\n");
    fosu::Parser parser;
    const auto& parsed = require_parse(parser.parse(input));
    expected = canonical(parsed);
    auto copied = parsed.copy(*program_arena);
    CHECK(copied);
    if (copied)
      owned = copied.value();
    owned.hit_objects[0].x = 42;
    CHECK_EQ(parsed.hit_objects[0].x, 1);
    owned.hit_objects[0].x = 1;
    std::memset(input.data.get(), 'x', input.size);
    CHECK_EQ(canonical(parsed), expected);
    auto replacement =
        fosu::make_padded("[Metadata]\nTitle:Replacement\n[HitObjects]\n1,2,5,1,0\n");
    require_parse(parser.parse(replacement));
  }
  CHECK_EQ(canonical(owned), expected);
  fosu::arena_release(program_arena);
}

static void test_failed_parse_resets_and_parser_remains_reusable() {
  fosu::Parser parser;
  auto input = fosu::make_padded("[Metadata]\nTitle:Before failure\n");
  CHECK(parser.parse(input));

  auto failed = parser.parse(nullptr, 1);
  CHECK(!failed);
  CHECK_EQ(failed.error().code, fosu::ErrorCode::InvalidInput);
  const auto storage = fosu::internal::parser_storage(parser);
  CHECK(storage.input == nullptr);
  CHECK_EQ(fosu::arena_pos(storage.arena), fosu::kArenaHeaderSize);

  const auto& recovered = require_parse(parser.parse(input));
  CHECK(recovered.title == "Before failure");
}

static void test_failed_copy_rewinds_destination() {
  std::string text = "[HitObjects]\n";
  for (size_t i = 0; i < 2000; ++i)
    text += "1,2,3,1,0,1:2:3:4:sample.wav\n";
  auto input = fosu::make_padded(text);
  fosu::Parser parser;
  const auto& beatmap = require_parse(parser.parse(input));

  const size_t page_size = fosu::internal::os_page_size();
  fosu::Arena* destination = fosu::arena_alloc({
      .reserve_size = page_size,
      .commit_size = page_size,
      .flags = 0,
  });
  CHECK(destination != nullptr);
  auto* existing = fosu::arena_push_array<uint32_t>(destination, 1);
  CHECK(existing != nullptr);
  *existing = 0x12345678;
  const size_t checkpoint = fosu::arena_pos(destination);

  auto copied = beatmap.copy(*destination);
  CHECK(!copied);
  CHECK_EQ(copied.error().code, fosu::ErrorCode::AllocationFailure);
  CHECK_EQ(fosu::arena_pos(destination), checkpoint);
  CHECK_EQ(*existing, 0x12345678u);
  fosu::arena_release(destination);
}

static void test_arena_interface() {
  fosu::Arena* arena = fosu::arena_alloc({
      .reserve_size = 64u << 10,
      .commit_size = 4u << 10,
      .flags = fosu::ArenaFlagChain,
  });
  CHECK(arena != nullptr);
  CHECK(reinterpret_cast<uintptr_t>(arena) % fosu::kCacheLineSize == 0);
  const size_t initial = fosu::arena_pos(arena);
  auto* first = fosu::arena_push_array<uint32_t>(arena, 16);
  CHECK(first != nullptr);
  const fosu::TempArena temp = fosu::temp_begin(arena);
  CHECK(fosu::arena_push(arena, 96u << 10, alignof(uint64_t)) != nullptr);
  CHECK(arena->current != arena);
  fosu::temp_end(temp);
  CHECK(arena->current == arena);
  CHECK_EQ(fosu::arena_pos(arena), temp.pos);
  fosu::arena_clear(arena);
  CHECK_EQ(fosu::arena_pos(arena), initial);
  fosu::arena_release(arena);

  fosu::Arena* pooled = fosu::internal::acquire_parser_arena();
  CHECK(pooled != nullptr);
  CHECK(fosu::arena_push(pooled, 1024, alignof(uint64_t)) != nullptr);
  fosu::internal::recycle_parser_arena(pooled);
  fosu::Arena* reused = fosu::internal::acquire_parser_arena();
  CHECK(reused == pooled);
  CHECK_EQ(fosu::arena_pos(reused), fosu::kArenaHeaderSize);
  fosu::internal::recycle_parser_arena(reused);
}

void test_read_into_reuse() {
  char path[] = "/tmp/fosu_read_into_XXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  const std::string big(10000, 'A');
  const std::string little(100, 'B');
  CHECK_EQ(write(fd, big.data(), big.size()), static_cast<ssize_t>(big.size()));
  close(fd);

  fosu::FileBuffer buffer;
  CHECK(fosu::read_into(path, buffer));
  const char* allocation = buffer.data.get();
  const size_t capacity = buffer.capacity;

  FILE* file = fopen(path, "wb");
  fwrite(little.data(), 1, little.size(), file);
  fclose(file);
  CHECK(fosu::read_into(path, buffer));
  CHECK(buffer.data.get() == allocation);
  CHECK_EQ(buffer.capacity, capacity);
  CHECK(memcmp(buffer.data.get(), little.data(), little.size()) == 0);
  unlink(path);
}

static void test_parser_prepares_engine_input_and_output() {
  static int calls = 0;
  const fosu::ParsingEngine engine{
      fosu::EngineKind::Scalar,
      [](std::span<const char> input, fosu::Beatmap& beatmap,
         fosu::ParseOptions options) noexcept {
        ++calls;
        CHECK_EQ(options.sections, fosu::kSectionHitObjects);
        CHECK_EQ(std::string_view(input.data(), input.size()), "1,2,3,1,0");
        for (size_t i = 0; i < fosu::kBufferPadding; ++i)
          CHECK_EQ(input.data()[input.size() + i], '\0');
        CHECK(!beatmap.hit_objects.empty());
        CHECK(beatmap.timing_points.empty());
        CHECK_EQ(beatmap.sample_set, fosu::SampleSet::Normal);
        beatmap.hit_objects[0] = {.x = 42};
        beatmap.hit_objects = beatmap.hit_objects.first(1);
        beatmap.sliders = {};
        beatmap.slider_points = {};
      },
  };
  fosu::Parser parser(engine);
  std::string input = "1,2,3,1,0";
  const auto& beatmap = require_parse(
      parser.parse(std::span<const char>(input), {.sections = fosu::kSectionHitObjects}));
  CHECK_EQ(beatmap.hit_objects.size(), 1u);
  CHECK_EQ(beatmap.hit_objects[0].x, 42);
  CHECK(!parser.parse(nullptr, 1));
  CHECK(!parser.parse(input.data(), input.size(), {.sections = 1}));
  CHECK_EQ(calls, 1);
}

int main() {
  {
    fosu::Parser parser;
    const auto input = fosu::make_padded("[HitObjects]\n10,20,1000,2,0,L|110:20,1,150\n");
    const auto& map =
        require_parse(parser.parse(input, {.calculate_slider_paths = true}));
    CHECK_EQ(map.hit_objects[0].end_time, 0);
    CHECK_EQ(map.slider_paths.size(), 1u);
    CHECK_EQ(map.slider_paths[0].distance(), 150);
    CHECK_EQ(fosu::slider_position_at(map.slider_paths[0], 0.5).x, 75);
    auto* arena = fosu::arena_alloc();
    CHECK(arena);
    auto copy = map.copy(*arena);
    CHECK(copy);
    CHECK(copy.value().slider_paths[0].points.data() !=
          map.slider_paths[0].points.data());
    require_parse(parser.parse(fosu::make_padded("")));
    CHECK_EQ(fosu::slider_position_at(copy.value().slider_paths[0], 1).x, 150);
    fosu::arena_release(arena);
  }
  for (const auto* engine :
       {&fosu::internal::compiled_engine, &fosu_test::scalar_engine()}) {
    fosu::Parser parser(*engine);
    const std::string input = "[HitObjects]\n0,0,1000,2,0,L|100:0,1,140\n";
    const auto& skipped =
        require_parse(parser.parse(input, {.calculate_slider_end_times = false}));
    CHECK_EQ(skipped.hit_objects[0].end_time, 0);
    auto* destination = fosu::arena_alloc();
    CHECK(destination);
    auto copy = skipped.copy(*destination);
    CHECK(copy);
    CHECK_EQ(require_parse(parser.parse(input)).hit_objects[0].end_time, 0);
    const auto& calculated =
        require_parse(parser.parse(input, {.calculate_slider_end_times = true}));
    CHECK(calculated.hit_objects[0].end_time > calculated.hit_objects[0].time);
    CHECK_EQ(copy.value().hit_objects[0].end_time, 0);
    fosu::arena_release(destination);
  }
  test_parser_prepares_engine_input_and_output();
  test_reparse_reuses_arena_memory();
  test_reparse_accepts_larger_arrays();
  test_reparse_clears_omitted_sections();
  test_empty_reparse_resets_defaults();
  test_large_arena_arrays();
  test_rejected_slider_points();
  test_copy_owns_all_data();
  test_failed_parse_resets_and_parser_remains_reusable();
  test_failed_copy_rewinds_destination();
  test_arena_interface();
  test_read_into_reuse();
  return test_result();
}
