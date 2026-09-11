#include <fosu/io.h>
#include <fosu/parser.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct Profile {
  const char* name;
  fosu::ParseOptions options;
};

constexpr Profile kProfiles[] = {
    {"decode", {}},
    {"hit-objects-only", {.sections = fosu::kSectionHitObjects}},
    {"end-times", {.calculate_slider_end_times = true}},
    {"paths", {.calculate_slider_paths = true}},
    {"geometry", {.calculate_slider_end_times = true, .calculate_slider_paths = true}},
    {"events", {.calculate_slider_events = true}},
    {"stacking", {.apply_stacking = true}},
    {"gameplay", {.calculate_slider_events = true, .apply_stacking = true}},
    {"double-time", {.mods = fosu::Mods::DoubleTime}},
};

constexpr Profile kStandardProfiles[] = {
    {"decode", {}},
    {"double-time", {.mods = fosu::Mods::DoubleTime}},
    {"hard-rock-double-time", {.mods = fosu::Mods::HardRock | fosu::Mods::DoubleTime}},
    {"full-hard-rock-double-time",
     {.calculate_slider_events = true,
      .apply_stacking = true,
      .mods = fosu::Mods::HardRock | fosu::Mods::DoubleTime}},
};

uint64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void parse(fosu::Parser& parser,
           const fosu::FileBuffer& input,
           fosu::ParseOptions options) {
  auto parsed = parser.parse(input, options);
  if (!parsed)
    std::abort();
  const auto& beatmap = *parsed.value();
  __asm__ volatile("" : : "g"(&beatmap), "g"(beatmap.hit_objects.size()) : "memory");
}

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr,
                 "usage: feature_matrix corpus reps variant all-modes|standard\n");
    return 2;
  }
  const int reps = std::atoi(argv[2]);
  if (reps < 1)
    return 2;

  std::vector<std::filesystem::path> files;
  for (const auto& file : std::filesystem::directory_iterator(argv[1]))
    if (file.path().extension() == ".osu")
      files.push_back(file.path());
  std::sort(files.begin(), files.end());
  if (files.empty())
    return 2;

  const Profile* profiles = kProfiles;
  size_t profile_count = std::size(kProfiles);
  if (std::string_view(argv[4]) == "standard") {
    profiles = kStandardProfiles;
    profile_count = std::size(kStandardProfiles);
  } else if (std::string_view(argv[4]) != "all-modes") {
    return 2;
  }

  std::puts("file,bytes,rep,variant,workload,wall_ns");
  fosu::FileBuffer input;
  fosu::Parser reused_parser;
  for (size_t file_index = 0; file_index < files.size(); ++file_index) {
    if (!fosu::read_into(files[file_index].c_str(), input))
      return 1;
    for (int rep = 0; rep < reps; ++rep) {
      for (size_t job_index = 0; job_index < profile_count * 2; ++job_index) {
        const size_t job =
            (job_index + file_index + static_cast<size_t>(rep)) % (profile_count * 2);
        const Profile& profile = profiles[job / 2];
        const bool reuse = job % 2;
        const uint64_t begin = now();
        if (reuse) {
          parse(reused_parser, input, profile.options);
        } else {
          fosu::Parser parser;
          parse(parser, input, profile.options);
        }
        const uint64_t elapsed = now() - begin;
        std::printf("%s,%zu,%d,%s,%s-%s,%llu\n", files[file_index].c_str(), input.size,
                    rep, argv[3], profile.name, reuse ? "reused" : "fresh",
                    static_cast<unsigned long long>(elapsed));
      }
    }
  }
}
