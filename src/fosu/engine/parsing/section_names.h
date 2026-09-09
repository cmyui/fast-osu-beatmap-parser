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
  Unknown
};
constexpr Section match_section(std::string_view line) {
  switch (string_hash(line)) {
    case "[General]"_hash:
      return line == "[General]" ? Section::General : Section::Unknown;
    case "[Editor]"_hash:
      return line == "[Editor]" ? Section::Editor : Section::Unknown;
    case "[Metadata]"_hash:
      return line == "[Metadata]" ? Section::Metadata : Section::Unknown;
    case "[Difficulty]"_hash:
      return line == "[Difficulty]" ? Section::Difficulty : Section::Unknown;
    case "[Events]"_hash:
      return line == "[Events]" ? Section::Events : Section::Unknown;
    case "[TimingPoints]"_hash:
      return line == "[TimingPoints]" ? Section::TimingPoints : Section::Unknown;
    case "[Colours]"_hash:
      return line == "[Colours]" ? Section::Colours : Section::Unknown;
    case "[HitObjects]"_hash:
      return line == "[HitObjects]" ? Section::HitObjects : Section::Unknown;
  }
  return Section::Unknown;
}
}  // namespace fosu::internal
