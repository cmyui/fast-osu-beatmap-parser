#pragma once
#include <fosu/engine/primitives/vector_ops.h>

#include <cstring>

namespace fosu::internal {
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
#if FOSU_SIMD
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
