#include <fosu/mods.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <fosu/types.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

struct Profile {
  const char*        name;
  fosu::ParseOptions options;
};

constexpr Profile kProfiles[] = {
    {"decode", {}},
    {"hit-objects-only", {.sections = fosu::kSectionHitObjects}},
    {"end-times", {.calculate_slider_end_times = true}},
    {"paths", {.calculate_slider_paths = true}},
    {"geometry",
     {.calculate_slider_end_times = true, .calculate_slider_paths = true}},
    {"events", {.calculate_slider_events = true}},
    {"stacking", {.apply_stacking = true}},
    {"gameplay", {.calculate_slider_events = true, .apply_stacking = true}},
    {"double-time", {.mods = fosu::Mods::DoubleTime}},
};

constexpr Profile kStandardProfiles[] = {
    {"decode", {}},
    {"double-time", {.mods = fosu::Mods::DoubleTime}},
    {"hard-rock-double-time",
     {.mods = fosu::Mods::HardRock | fosu::Mods::DoubleTime}},
    {"full-hard-rock-double-time",
     {.calculate_slider_events = true,
      .apply_stacking = true,
      .mods = fosu::Mods::HardRock | fosu::Mods::DoubleTime}},
};

#if defined(_MSC_VER)
static const void* volatile result_sink;
static volatile size_t object_count_sink;
#endif

uint64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// A pass's order of corpus entries, shuffled by Fisher-Yates over splitmix64
// so that the Python matrix (bench/common.py) visits entries identically.
std::vector<size_t> shuffled_order(size_t count, uint64_t seed) {
  std::vector<size_t> order(count);
  for (size_t i = 0; i < count; ++i)
    order[i] = i;
  for (size_t i = count; i > 1; --i) {
    uint64_t z = (seed += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    std::swap(order[i - 1], order[z % i]);
  }
  return order;
}

// Benchmarks parse resident bytes, so file contents are read before timing.
static bool read_file(const char* path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  out.assign(std::istreambuf_iterator<char>(file), {});
  return !file.bad() && file.is_open();
}

void parse(fosu::Parser&      parser,
           std::string_view   input,
           fosu::ParseOptions options) {
  auto parsed = parser.parse(input, options);
  if (!parsed)
    std::abort();
  const auto& beatmap = *parsed;
#if defined(_MSC_VER)
  result_sink = &beatmap;
  object_count_sink = beatmap.hit_objects.size();
#else
  __asm__ volatile(""
                   :
                   : "g"(&beatmap), "g"(beatmap.hit_objects.size())
                   : "memory");
#endif
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr,
                 "usage: feature_matrix corpus reps variant "
                 "all-modes|standard reused|fresh\n");
    return 2;
  }
  const fosu::i32 reps = std::atoi(argv[2]);
  if (reps < 1)
    return 2;
  // Time one parser lifetime per process. A fresh parser maps and unmaps its
  // memory every call, which would also slow interleaved reused-parser calls.
  const std::string_view lifetime = argv[5];
  if (lifetime != "reused" && lifetime != "fresh")
    return 2;
  const bool                         reuse = lifetime == "reused";

  std::vector<std::filesystem::path> files;
  for (const auto& file : std::filesystem::directory_iterator(argv[1]))
    if (file.path().extension() == ".osu")
      files.push_back(file.path());
  std::sort(files.begin(), files.end());
  if (files.empty())
    return 2;

  const Profile* profiles = kProfiles;
  size_t         profile_count = std::size(kProfiles);
  if (std::string_view(argv[4]) == "standard") {
    profiles = kStandardProfiles;
    profile_count = std::size(kStandardProfiles);
  } else if (std::string_view(argv[4]) != "all-modes") {
    return 2;
  }

  std::vector<std::string> inputs(files.size());
  for (size_t i = 0; i < files.size(); ++i)
    if (!read_file(files[i].string().c_str(), inputs[i]))
      return 1;

  // Each pass times one profile over every entry once, in its own shuffled
  // order, so no entry repeats until the next pass: branch predictors and
  // caches cannot learn one map's pattern from the previous call.
  std::puts("file,bytes,rep,variant,workload,wall_ns");
  fosu::Parser reused_parser;
  for (fosu::i32 rep = 0; rep < reps; ++rep) {
    for (size_t job_index = 0; job_index < profile_count; ++job_index) {
      const size_t   profile_index = (job_index + rep) % profile_count;
      const Profile& profile = profiles[profile_index];
      for (const size_t file_index :
           shuffled_order(inputs.size(),
                          static_cast<uint64_t>(rep) * 1000 + profile_index)) {
        const std::string& input = inputs[file_index];
        const uint64_t     begin = now();
        if (reuse) {
          parse(reused_parser, input, profile.options);
        } else {
          fosu::Parser parser;
          parse(parser, input, profile.options);
        }
        const uint64_t elapsed = now() - begin;
        std::printf("%s,%zu,%d,%s,%s-%s,%llu\n",
                    files[file_index].string().c_str(), input.size(), rep,
                    argv[3], profile.name, argv[5],
                    static_cast<unsigned long long>(elapsed));
      }
    }
  }
}
