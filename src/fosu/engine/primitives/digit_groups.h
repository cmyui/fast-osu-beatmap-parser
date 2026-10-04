#pragma once

#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/types.h>

namespace fosu::internal {

#if FOSU_SIMD_NEON
// Four right-aligned groups of four decimal digits, independently converted.
inline uint32x4_t decimal_groups(uint8x16_t digits) {
  constexpr u8  pairs[16] = {10, 1, 10, 1, 10, 1, 10, 1,
                             10, 1, 10, 1, 10, 1, 10, 1};
  constexpr u16 words[8] = {100, 1, 100, 1, 100, 1, 100, 1};
  return vpaddlq_u16(vmulq_u16(vpaddlq_u8(vmulq_u8(digits, vld1q_u8(pairs))),
                               vld1q_u16(words)));
}
#endif

}  // namespace fosu::internal
