#pragma once

// Parsers for the hitobject line prefix "x,y,time,type,hitSound".
//
// The AVX2 path is based on a prototype by Flamme (fla.me): classify
// delimiters with one vector compare, derive a (len_x, len_y, len_time,
// len_type) signature, and use it to index precomputed permute+shuffle
// masks that normalize every digit into a fixed position, so a single
// maddubs/madd chain converts the whole prefix at once. This version adds:
//   - parallel delimiter extraction (blsr chain + independent tzcnts)
//     instead of a serialized tzcnt/shift chain
//   - the table index computed directly from delimiter positions:
//     ((x0*3+y0)*10+t0)*3+ty0  ==  60*p0 + 27*p1 + 2*p2 + p3 - 158
//   - permute and shuffle masks fused into one 64-byte (one cache line)
//     table entry, generated at compile time
//   - structural validation with scalar fallback instead of assuming
//     well-formed input
//   - 1-2 digit hitSound support (osu! hitsound bitflags go up to 15)
//
// Callers must guarantee kBufferPadding readable bytes past the end of
// the buffer (see io.h); all paths rely on it to load past short lines
// and speculate past field boundaries safely.

#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace fosu::internal {

#if !FOSU_SIMD
struct HitObjectParseConstants {};  // the scalar path has no vector constants
#endif

inline constexpr uint32_t kNPrefixVariants = 3 * 3 * 10 * 3;

#if FOSU_SIMD_X86

// One entry per (len_x, len_y, len_time, len_type, len_hit_sound) combination. `perm`
// feeds vpermd to move each field's dwords into the lane that needs them;
// `shuf` then places digits at fixed offsets (0x80 lanes produce zero):
//   bytes  0-3   x   right-aligned  -> dword 0 after madd
//   bytes  4-7   y                  -> dword 1
//   bytes  8-11  type               -> dword 2
//   bytes 12-15  hitSound digits available in the low lane
//   bytes 16-19  remaining hitSound digits, summed with dword 3 after madd
//   bytes 20-31  time right-aligned -> dwords 5,6,7 = top2/mid4/low4 digits
struct alignas(64) LaneMasks {
  int32_t perm[8];
  int8_t shuf[32];
};
static_assert(sizeof(LaneMasks) == 64);

consteval std::array<LaneMasks, kNPrefixVariants * 2> make_lane_masks() {
  std::array<LaneMasks, kNPrefixVariants * 2> out{};
  for (int lx = 1; lx <= 3; ++lx)
    for (int ly = 1; ly <= 3; ++ly)
      for (int lt = 1; lt <= 10; ++lt)
        for (int lty = 1; lty <= 3; ++lty)
          for (int lhs = 1; lhs <= 2; ++lhs) {
            const int p0 = lx;
            const int p1 = p0 + 1 + ly;
            const int p2 = p1 + 1 + lt;
            const int p3 = p2 + 1 + lty;
            const int index =
                ((((lx - 1) * 3 + (ly - 1)) * 10 + (lt - 1)) * 3 + (lty - 1)) * 2 + lhs -
                1;

            int src[32];
            for (auto& s : src)
              s = -1;
            for (int i = 0; i < lx; ++i)
              src[4 - lx + i] = i;
            for (int i = 0; i < ly; ++i)
              src[8 - ly + i] = p0 + 1 + i;
            for (int i = 0; i < lty; ++i)
              src[12 - lty + i] = p2 + 1 + i;
            for (int i = 0; i < lt; ++i)
              src[32 - lt + i] = p1 + 1 + i;

            LaneMasks& lm = out[index];
            for (auto& word : lm.perm)
              word = -1;
            int used[2]{};
            // Each 128-bit lane can gather four source dwords before the byte shuffle.
            auto find_or_add_word = [&](int word, int lane) {
              for (int i = 0; i < used[lane]; ++i)
                if (lm.perm[lane * 4 + i] == word)
                  return i;
              if (used[lane] == 4)
                return -1;
              const int slot = used[lane]++;
              lm.perm[lane * 4 + slot] = word;
              return slot;
            };
            for (int b = 0; b < 32; ++b)
              if (src[b] >= 0 && find_or_add_word(src[b] / 4, b / 16) < 0)
                __builtin_abort();

            // Put hitSound in the low lane where possible; use spare high-lane
            // space otherwise. Preserve each digit's decimal weight in either lane.
            for (int i = 0; i < lhs; ++i) {
              const int source = p3 + 1 + i;
              if (find_or_add_word(source / 4, 0) >= 0) {
                src[16 - lhs + i] = source;
              } else {
                if (find_or_add_word(source / 4, 1) < 0)
                  __builtin_abort();
                src[20 - lhs + i] = source;
              }
            }

            for (int b = 0; b < 32; ++b) {
              if (src[b] < 0) {
                lm.shuf[b] = static_cast<int8_t>(0x80);
                continue;
              }
              const int dw = src[b] / 4;
              const int off = src[b] % 4;
              const int slot = find_or_add_word(dw, b / 16);
              lm.shuf[b] = static_cast<int8_t>(slot * 4 + off);
            }
            for (auto& word : lm.perm)
              if (word < 0)
                word = 0;
          }
  return out;
}

inline constexpr auto kLaneMasks = make_lane_masks();

// Vector constants for one [HitObjects] section, built once and passed by
// reference so callees use them as memory operands instead of rebuilding
// them per call.
struct HitObjectParseConstants {
  __m256i nl, comma, colon, pipe, zero;
  __m128i pair_weights, word_weights;  // slider point digit weights
  HitObjectParseConstants()
      : nl(broadcast_byte<'\n'>()),
        comma(broadcast_byte<','>()),
        colon(broadcast_byte<':'>()),
        pipe(broadcast_byte<'|'>()),
        zero(broadcast_byte<'0'>()),
        pair_weights(_mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0)),
        word_weights(_mm_setr_epi16(100, 1, 100, 1, 0, 0, 0, 0)) {}
};

#elif FOSU_SIMD_NEON
struct HitObjectParseConstants {
  ByteVector nl = broadcast_byte<'\n'>(), comma = broadcast_byte<','>(),
             colon = broadcast_byte<':'>(), pipe = broadcast_byte<'|'>(),
             zero = broadcast_byte<'0'>();
};

// TBL directly addresses both 16-byte input registers. No lane permutation
// is needed, so each prefix shape occupies 32 bytes instead of AVX2's 64.
// The first shuffle decodes x, y, type and hitSound; the second decodes time.
struct alignas(32) PrefixShuffle {
  uint8_t bytes[32];
};
consteval auto make_prefix_shuffles() {
  std::array<PrefixShuffle, kNPrefixVariants * 2> out{};
  for (int x = 1; x <= 3; ++x)
    for (int y = 1; y <= 3; ++y)
      for (int t = 1; t <= 10; ++t)
        for (int type = 1; type <= 3; ++type)
          for (int sound = 1; sound <= 2; ++sound) {
            auto& m = out[((((x - 1) * 3 + y - 1) * 10 + t - 1) * 3 + type - 1) * 2 +
                          sound - 1];
            for (auto& b : m.bytes)
              b = 255;
            for (int i = 0; i < x; ++i)
              m.bytes[4 - x + i] = i;
            for (int i = 0; i < y; ++i)
              m.bytes[8 - y + i] = x + 1 + i;
            for (int i = 0; i < type; ++i)
              m.bytes[12 - type + i] = x + y + t + 3 + i;
            for (int i = 0; i < t; ++i)
              m.bytes[32 - t + i] = x + y + 2 + i;
            for (int i = 0; i < sound; ++i)
              m.bytes[16 - sound + i] = x + y + t + type + 4 + i;
          }
  return out;
}
inline constexpr auto kPrefixShuffles = make_prefix_shuffles();
#endif

}  // namespace fosu::internal
