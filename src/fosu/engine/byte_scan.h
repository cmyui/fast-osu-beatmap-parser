#pragma once
#include <cstring>
#include <fosu/engine/simd.h>

namespace fosu::internal {
// Input belongs to the parser's padded buffer. Matches are bounded by end,
// even when the vector load includes bytes from the following field or line.
// Returns end when the delimiter is absent.
template <char Delimiter, bool UseSimd = FOSU_SIMD>
inline const char* find_byte(const char* p, const char* end) {
#if FOSU_SIMD
  if constexpr (UseSimd) {
    const auto delimiter = broadcast_byte<static_cast<uint8_t>(Delimiter)>();
    while (p < end) {
      const auto mask = equal_mask32(load32(p), delimiter);
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
  } else
#endif
  {
    const auto* match = static_cast<const char*>(memchr(p, Delimiter, end - p));
    return match ? match : end;
  }
}
}  // namespace fosu::internal
