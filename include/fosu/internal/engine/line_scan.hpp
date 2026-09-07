#pragma once
#include "prefix.hpp"

namespace fosu::internal {
#if FOSU_SIMD
// Newline search past the first window, 32 bytes per step. Bytes beyond
// `end` are zero padding, so a hit is always inside the input. Returns `end`
// when the remaining bytes hold no newline.
inline const char* find_newline32(const char* p, const char* end, ByteVector nl) {
    while (p < end) {
        const auto m = equal_mask32(load32(p), nl);
        if (m) return p + trailing_zeros(m);
        p += 32;
    }
    return end;
}
#endif
}  // namespace fosu::internal
