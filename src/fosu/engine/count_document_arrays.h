#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/parse_options.h>

namespace fosu::internal {

struct BeatmapArrayCapacities {
  size_t breaks = 0;
  size_t colours = 0;
  size_t timing_points = 0;
  size_t hit_objects = 0;
  size_t sliders = 0;
  size_t slider_points = 0;
};

// Every accepted record consumes a source line, and every accepted slider
// point has a corresponding pipe. Count those two structural limits once;
// record validation remains the parsing engine's job.
inline BeatmapArrayCapacities count_document_arrays(std::span<const char> input,
                                                    uint32_t selected_sections) {
  constexpr uint32_t array_sections =
      kSectionEvents | kSectionTimingPoints | kSectionColours | kSectionHitObjects;
  if (!(selected_sections & array_sections))
    return {};

  const char* cursor = input.data();
  const char* end = cursor + input.size();
  size_t lines = 0;
  size_t pipes = 0;

#if FOSU_SIMD
  const ByteVector newline = broadcast_byte<'\n'>();
  const ByteVector pipe = broadcast_byte<'|'>();
  while (end - cursor >= 32) {
    const Bytes32 bytes = load32(cursor);
    lines += count_equal_bytes32(bytes, newline);
    pipes += count_equal_bytes32(bytes, pipe);
    cursor += 32;
  }
#else
  lines = static_cast<size_t>(std::count(cursor, end, '\n'));
  pipes = static_cast<size_t>(std::count(cursor, end, '|'));
  cursor = end;
#endif

  while (cursor < end) {
    lines += *cursor == '\n';
    pipes += *cursor == '|';
    ++cursor;
  }
  if (!input.empty() && input.back() != '\n')
    ++lines;

  BeatmapArrayCapacities capacities;
  if (selected_sections & kSectionEvents)
    capacities.breaks = lines;
  if (selected_sections & kSectionColours)
    capacities.colours = lines;
  if (selected_sections & kSectionTimingPoints)
    capacities.timing_points = lines;
  if (selected_sections & kSectionHitObjects) {
    capacities.hit_objects = lines;
    capacities.sliders = lines;
    capacities.slider_points = pipes;
  }
  return capacities;
}

}  // namespace fosu::internal
