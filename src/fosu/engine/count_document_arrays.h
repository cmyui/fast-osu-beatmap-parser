#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/parse_options.h>

namespace fosu::internal {

#if !FOSU_SIMD
// Return eight independent 0-or-1 byte lanes without reducing them to a scalar.
inline uint64_t matching_byte_lanes(uint64_t bytes, uint8_t needle) {
  constexpr uint64_t low_bits = 0x7f7f7f7f7f7f7f7f;
  constexpr uint64_t high_bits = 0x8080808080808080;
  const uint64_t repeated = uint64_t{needle} * 0x0101010101010101;
  const uint64_t differences = bytes ^ repeated;
  const uint64_t non_high_bits = differences & low_bits;
  const uint64_t original_high_bits = differences & high_bits;
  const uint64_t zero_high_bits =
      ~((non_high_bits + low_bits) | original_high_bits) & high_bits;
  return zero_high_bits >> 7;
}

inline uint32_t sum_byte_lanes(uint64_t lanes) {
  lanes = (lanes & 0x00ff00ff00ff00ff) + ((lanes >> 8) & 0x00ff00ff00ff00ff);
  lanes = (lanes & 0x0000ffff0000ffff) + ((lanes >> 16) & 0x0000ffff0000ffff);
  lanes = (lanes & 0x00000000ffffffff) + (lanes >> 32);
  return static_cast<uint32_t>(lanes);
}
#endif

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
#if FOSU_SIMD_X86
  // AVX2 comparisons produce 0xff byte lanes. Accumulate their byte sums in
  // four independent 64-bit lanes and reduce only after the scan.
  const ByteVector zero = _mm256_setzero_si256();
  ByteVector line_counts_a = zero;
  ByteVector line_counts_b = zero;
  ByteVector pipe_counts_a = zero;
  ByteVector pipe_counts_b = zero;
  while (end - cursor >= 64) {
    const Bytes32 first = load32(cursor);
    const Bytes32 second = load32(cursor + 32);
    line_counts_a = _mm256_add_epi64(
        line_counts_a, _mm256_sad_epu8(_mm256_cmpeq_epi8(first, newline), zero));
    line_counts_b = _mm256_add_epi64(
        line_counts_b, _mm256_sad_epu8(_mm256_cmpeq_epi8(second, newline), zero));
    pipe_counts_a = _mm256_add_epi64(
        pipe_counts_a, _mm256_sad_epu8(_mm256_cmpeq_epi8(first, pipe), zero));
    pipe_counts_b = _mm256_add_epi64(
        pipe_counts_b, _mm256_sad_epu8(_mm256_cmpeq_epi8(second, pipe), zero));
    cursor += 64;
  }
  while (end - cursor >= 32) {
    const Bytes32 bytes = load32(cursor);
    line_counts_a = _mm256_add_epi64(
        line_counts_a, _mm256_sad_epu8(_mm256_cmpeq_epi8(bytes, newline), zero));
    pipe_counts_a = _mm256_add_epi64(
        pipe_counts_a, _mm256_sad_epu8(_mm256_cmpeq_epi8(bytes, pipe), zero));
    cursor += 32;
  }
  const ByteVector line_counts = _mm256_add_epi64(line_counts_a, line_counts_b);
  const ByteVector pipe_counts = _mm256_add_epi64(pipe_counts_a, pipe_counts_b);
  alignas(32) uint64_t counts[4];
  _mm256_store_si256(reinterpret_cast<__m256i*>(counts), line_counts);
  lines = (counts[0] + counts[1] + counts[2] + counts[3]) / 255;
  _mm256_store_si256(reinterpret_cast<__m256i*>(counts), pipe_counts);
  pipes = (counts[0] + counts[1] + counts[2] + counts[3]) / 255;
#else
  while (end - cursor >= 32) {
    const Bytes32 bytes = load32(cursor);
    lines += count_equal_bytes32(bytes, newline);
    pipes += count_equal_bytes32(bytes, pipe);
    cursor += 32;
  }
#endif
#else
  size_t words = static_cast<size_t>(end - cursor) / sizeof(uint64_t);
  while (words) {
    // Each byte lane can accumulate 255 matches without carrying into its neighbour.
    const size_t batch = std::min<size_t>(words, 255);
    uint64_t line_lanes = 0;
    uint64_t pipe_lanes = 0;
    for (size_t i = 0; i < batch; ++i) {
      uint64_t bytes;
      std::memcpy(&bytes, cursor, sizeof(bytes));
      line_lanes += matching_byte_lanes(bytes, '\n');
      pipe_lanes += matching_byte_lanes(bytes, '|');
      cursor += sizeof(bytes);
    }
    lines += sum_byte_lanes(line_lanes);
    pipes += sum_byte_lanes(pipe_lanes);
    words -= batch;
  }
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
