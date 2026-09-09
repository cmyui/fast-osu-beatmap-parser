#pragma once
#include <cstdint>

namespace fosu {
enum : uint32_t {
  kSectionGeneral = 1u << 1,
  kSectionEditor = 1u << 2,
  kSectionMetadata = 1u << 3,
  kSectionDifficulty = 1u << 4,
  kSectionEvents = 1u << 5,
  kSectionTimingPoints = 1u << 6,
  kSectionColours = 1u << 7,
  kSectionHitObjects = 1u << 8,
  kAllSections = 0x1FEu,
};

struct ParseOptions {
  uint32_t sections = kAllSections;
  bool calculate_slider_end_times = false;
  bool calculate_slider_paths = false;
  bool calculate_slider_events = false;
  bool apply_stacking = false;
};
}  // namespace fosu
