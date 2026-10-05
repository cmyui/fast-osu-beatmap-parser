#pragma once

#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <string_view>

namespace fosu::internal {
// Unknown also stands for sections a client knows but fosu does not read.
enum class Section : u8 {
  None,
  General,
  Editor,
  Metadata,
  Difficulty,
  Events,
  TimingPoints,
  Colours,
  HitObjects,
  Unknown
};
inline constexpr auto kLazerSectionNames = make_string_lookup<Section>({
    {"General", Section::General},
    {"Editor", Section::Editor},
    {"Metadata", Section::Metadata},
    {"Difficulty", Section::Difficulty},
    {"Events", Section::Events},
    {"TimingPoints", Section::TimingPoints},
    {"Colours", Section::Colours},
    {"HitObjects", Section::HitObjects},
    {"Variables", Section::Unknown},
    {"Fonts", Section::Unknown},
    {"CatchTheBeat", Section::Unknown},
    {"Mania", Section::Unknown},
});
// stable's FileSection names, including its Unknown and All values.
inline constexpr auto kStableSectionNames = make_string_lookup<Section>({
    {"General", Section::General},
    {"Editor", Section::Editor},
    {"Metadata", Section::Metadata},
    {"Difficulty", Section::Difficulty},
    {"Events", Section::Events},
    {"TimingPoints", Section::TimingPoints},
    {"Colours", Section::Colours},
    {"HitObjects", Section::HitObjects},
    {"Variables", Section::Unknown},
    {"Unknown", Section::Unknown},
    {"All", Section::Unknown},
});

// The section a header line starts; see section_header_line. Both clients
// match the name exactly; fosu does not follow the numbers, comma lists and
// padding their enum parsers also accept. A lazer header naming no section
// starts [General]; a stable one leaves `current` in effect.
template <Client C>
constexpr Section match_section(std::string_view line, Section current) {
  if constexpr (C == Client::Lazer) {
    while (line.back() == ' ' || line.back() == '\t')
      line.remove_suffix(1);
    const auto* section =
        kLazerSectionNames.find(line.substr(1, line.size() - 2));
    return section ? *section : Section::General;
  } else {
    // stable trims brackets from both ends: "[[Events]" names Events.
    const size_t first = line.find_first_not_of("[]");
    if (first == std::string_view::npos)
      return current;
    const size_t last = line.find_last_not_of("[]");
    const auto*  section =
        kStableSectionNames.find(line.substr(first, last + 1 - first));
    return section ? *section : current;
  }
}
}  // namespace fosu::internal
