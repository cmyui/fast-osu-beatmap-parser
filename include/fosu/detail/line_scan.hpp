#pragma once
#include "prefix.hpp"

namespace fosu::detail {
#if FOSU_SIMD_X86
// Newline search past the first window, 32 bytes per step. Bytes beyond
// `end` are zero padding, so a hit is always inside the input. Returns `end`
// when the remaining bytes hold no newline.
inline const char* find_newline32(const char* p, const char* end, __m256i nl) {
    while (p < end) {
        const auto m = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), nl)));
        if (m) return p + _tzcnt_u32(m);
        p += 32;
    }
    return end;
}
#endif
}  // namespace fosu::detail
