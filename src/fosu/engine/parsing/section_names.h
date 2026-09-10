#pragma once
#include <fosu/engine/parsing/string_lookup.h>
#include <cstdint>
#include <string_view>
namespace fosu::internal {
enum class Section : uint8_t {
  None,
  General,
  Editor,
  Metadata,
  Difficulty,
  Events,
  TimingPoints,
  Colours,
  HitObjects,
  Variables,
  Unknown
};
inline constexpr auto kSectionNames = make_string_lookup<Section>({
    {"[General]", Section::General},
    {"[Editor]", Section::Editor},
    {"[Metadata]", Section::Metadata},
    {"[Difficulty]", Section::Difficulty},
    {"[Events]", Section::Events},
    {"[TimingPoints]", Section::TimingPoints},
    {"[Colours]", Section::Colours},
    {"[HitObjects]", Section::HitObjects},
    {"[Variables]", Section::Variables},
});

constexpr Section match_section(std::string_view line) {
  const auto* section = kSectionNames.find(line);
  return section ? *section : Section::Unknown;
}
}  // namespace fosu::internal
