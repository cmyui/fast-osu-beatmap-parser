#pragma once
#include <fosu/mods.h>
#include <fosu/types.h>

namespace fosu {
enum : u32 {
  kSectionGeneral = 1u << 1,
  kSectionEditor = 1u << 2,
  kSectionMetadata = 1u << 3,
  kSectionDifficulty = 1u << 4,
  kSectionEvents = 1u << 5,
  kSectionTimingPoints = 1u << 6,
  kSectionColours = 1u << 7,
  kSectionHitObjects = 1u << 8,
  kAllSections = kSectionGeneral | kSectionEditor | kSectionMetadata |
      kSectionDifficulty | kSectionEvents | kSectionTimingPoints |
      kSectionColours | kSectionHitObjects,
};

// Whose behaviour to follow where osu!stable and osu!lazer parse a map
// differently. This is independent of the map's format version.
enum class Client : u8 { Stable, Lazer };

struct ParseOptions {
  u32    sections = kAllSections;
  bool   calculate_slider_end_times = false;
  bool   calculate_slider_paths = false;
  bool   calculate_slider_events = false;
  bool   apply_stacking = false;
  Mods   mods = Mods::None;
  Client client = Client::Stable;
};

// Why a parse returned no beatmap.
enum class ParseErrorCode : u8 {
  None,
  InvalidOptions,
  OutOfMemory,
  ReadFailed,  // parse_file could not read the file; errno has the cause.
  Unloadable,  // The target client would refuse to load this map.
};

struct ParseError {
  ParseErrorCode code = ParseErrorCode::None;
  u32            line = 0;  // 1-based line that makes the map unloadable.
};
}  // namespace fosu
