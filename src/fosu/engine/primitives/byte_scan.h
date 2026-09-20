#pragma once
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/types.h>

#include <cstring>

namespace fosu::internal {
#if FOSU_SIMD_NEON
inline u32 first_line_end16(uint8x16_t bytes) {
  constexpr u8 positions[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                8, 9, 10, 11, 12, 13, 14, 15};
  const auto   cr = vceqq_u8(bytes, broadcast_byte<'\r'>());
  const auto   lf = vceqq_u8(bytes, broadcast_byte<'\n'>());
  const auto   positions_or_end =
      vbslq_u8(vorrq_u8(cr, lf), vld1q_u8(positions), vdupq_n_u8(16));
  return vminvq_u8(positions_or_end);
}

// A line scan only needs the first ending, not the complete byte mask.
inline u32 first_line_end32(Bytes32 bytes) {
  const u32 first = first_line_end16(bytes.val[0]);
  if (first < 16)
    return first;
  return 16 + first_line_end16(bytes.val[1]);
}
#endif

// Input belongs to the parser's padded buffer. Matches are bounded by end,
// even when the vector load includes bytes from the following field or line.
// Returns end when the delimiter is absent.
template <char Delimiter>
inline const char* find_byte(const char* p, const char* end) {
#if FOSU_SIMD
  const auto delimiter = broadcast_byte<static_cast<u8>(Delimiter)>();
  while (p < end) {
    const auto   mask = equal_mask32(load32(p), delimiter);
    const size_t remaining = static_cast<size_t>(end - p);
    if (mask) {
      const auto offset = trailing_zeros(mask);
      return offset < remaining ? p + offset : end;
    }
    if (remaining <= 32)
      return end;
    p += 32;
  }
  return end;
#else
  const auto* match = static_cast<const char*>(memchr(p, Delimiter, end - p));
  return match ? match : end;
#endif
}

inline const char* find_line_end(const char* p, const char* end) {
  if (p >= end)
    return end;
#if FOSU_SIMD_NEON
  while (p < end) {
    const auto   offset = first_line_end32(load32(p));
    const size_t remaining = static_cast<size_t>(end - p);
    if (offset < 32)
      return offset < remaining ? p + offset : end;
    if (remaining <= 32)
      return end;
    p += 32;
  }
#elif FOSU_SIMD
  while (p < end) {
    const auto   chars = load32(p);
    const auto   mask = line_end_mask32(chars);
    const size_t remaining = static_cast<size_t>(end - p);
    if (mask) {
      const auto offset = trailing_zeros(mask);
      return offset < remaining ? p + offset : end;
    }
    if (remaining <= 32)
      return end;
    p += 32;
  }
#else
  // Bound each search so CR-only files do not rescan the whole suffix per line.
  while (p < end) {
    const size_t remaining = static_cast<size_t>(end - p);
    const size_t count = remaining < 128 ? remaining : 128;
    const auto*  lf = static_cast<const char*>(memchr(p, '\n', count));
    const auto*  cr = static_cast<const char*>(
        memchr(p, '\r', lf ? static_cast<size_t>(lf - p) : count));
    if (cr)
      return cr;
    if (lf)
      return lf;
    p += count;
  }
#endif
  return p;
}
}  // namespace fosu::internal
