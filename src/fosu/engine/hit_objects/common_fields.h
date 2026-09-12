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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/digit_groups.h>

#include <fosu/engine/primitives/vector_ops.h>

namespace fosu::internal {

#if !FOSU_SIMD
struct HitObjectParseConstants {};  // the scalar path has no vector constants
#endif

inline constexpr uint32_t kNPrefixVariants = 3 * 3 * 10 * 3;

struct HitObjectPrefix {
  int32_t x;
  int32_t y;
  uint32_t type;
  uint32_t hit_sound;
  double time;
};

struct ParsedHitObjectPrefix {
  HitObjectPrefix value;
  const char* next;
  float precise_x = 0;
  float precise_y = 0;
};

template <typename HitObject>
inline void initialize_hitobject(HitObject& object, const HitObjectPrefix& prefix) {
  object.x = prefix.x;
  object.y = prefix.y;
  object.time = prefix.time;
  object.type = prefix.type;
  object.hitsound = prefix.hit_sound;
  object.end_time = 0;
  object.slider = HitObject::kNoSlider;
}

// Lenient reference implementation: tolerates negative values, decimal
// coordinates (truncated), and values of any length.
inline std::optional<ParsedHitObjectPrefix> parse_hitobject_prefix_scalar(
    const char* line,
    size_t len) {
  const char* p = line;
  const char* end = line + len;
  float coord[2];
  for (int i = 0; i < 2; ++i) {
    const char* q = parse_osu_float(p, end, coord[i], 131072);
    if (q == p || q >= end || *q != ',')
      return std::nullopt;
    p = q + 1;
  }
  double time;
  const char* q = parse_osu_double(p, end, time);
  if (q == p || q >= end || *q != ',')
    return std::nullopt;
  p = q + 1;
  int64_t type, sound;
  q = parse_osu_int(p, end, type);
  if (q == p || q >= end || *q != ',')
    return std::nullopt;
  p = q + 1;
  q = parse_osu_int(p, end, sound);
  if (q == p || (q < end && *q != ','))
    return std::nullopt;
  return ParsedHitObjectPrefix{
      HitObjectPrefix{static_cast<int32_t>(coord[0]), static_cast<int32_t>(coord[1]),
                      static_cast<uint32_t>(type), static_cast<uint32_t>(sound), time},
      q, coord[0], coord[1]};
}

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

#if FOSU_SIMD
// Delimiter geometry of one hitobject prefix inside a 32-byte window, derived
// from the non-digit and comma masks alone. The four field lengths are packed
// into 16-bit lanes so one subtraction, one addition and one AND validate
// every length bound at once, and one multiply reduces the delimiter
// positions to the table index. `p4` is the first non-digit after hitSound
// (32 when the window holds none); the caller decides whether that byte ends
// the prefix. `index` is meaningful only when `ok`.
struct HitObjectPrefixShape {
  uint32_t index;
  uint32_t time_span;  // p2 - p1: time digits + 1
  uint32_t p4;
  bool ok;
};

inline HitObjectPrefixShape classify_hitobject_prefix(uint32_t nondig, uint32_t commas) {
  const uint32_t m1 = (nondig & (nondig - 1));
  const uint32_t m2 = (m1 & (m1 - 1));
  const uint32_t m3 = (m2 & (m2 - 1));
  const uint32_t m4 = (m3 & (m3 - 1));
  const uint64_t p0 = trailing_zeros(nondig);
  const uint64_t p1 = trailing_zeros(m1);
  const uint64_t p2 = trailing_zeros(m2);
  const uint64_t p3 = trailing_zeros(m3);
  const uint32_t p4 = trailing_zeros(m4);
  // Lanes (low to high): p0, p1, p2, p3 — each at most 32.
  const uint64_t pk = p0 | p1 << 16 | p2 << 32 | p3 << 48;
  // Lanes: len_x, len_y, len_time, len_type. A lane below zero borrows from
  // the next one, but such a lane fails the sign test itself, and a borrow
  // can only shrink a neighbour, never rescue an invalid line.
  const uint64_t lens = (pk - (pk << 16)) - 0x0002000200020001ull;
  // Bounds 2, 2, 9, 2: adding 0x7FFF - bound sets bit 15 exactly when a
  // non-negative lane exceeds its bound.
  const uint64_t over = lens + 0x7FFD7FF67FFD7FFDull;
  const bool lens_ok = ((lens | over) & 0x8000800080008000ull) == 0;
  // The first four non-digits must be literal commas.
  const uint32_t through_p3 = static_cast<uint32_t>((2ull << p3) - 1);
  const bool commas_ok = ((nondig ^ commas) & through_p3) == 0;
  // hitSound: one or two digits.
  const uint32_t hl = p4 - static_cast<uint32_t>(p3) - 1;
  const bool hs_ok = hl - 1 <= 1;
  HitObjectPrefixShape shape;
  // 60*p0 + 27*p1 + 2*p2 + p3 lands in the top lane; lower lanes cannot carry.
  shape.index = static_cast<uint32_t>((pk * 0x003C001B00020001ull) >> 48) - 158;
  shape.index = shape.index * 2 + hl - 1;
  shape.time_span = static_cast<uint32_t>(p2 - p1);
  shape.p4 = p4;
  shape.ok = lens_ok & commas_ok & hs_ok;
  return shape;
}

// Decodes a prefix whose delimiter geometry has already been classified.
// Returns nullopt only for 9-10 digit timestamps above INT32_MAX, which the
// scalar parser also rejects.
__attribute__((always_inline)) inline std::optional<HitObjectPrefix>
decode_hitobject_prefix(Bytes32 ascii,
                        ByteVector zero,
                        const HitObjectPrefixShape& shape) {
  HitObjectPrefix prefix;
  static_assert(offsetof(HitObjectPrefix, x) == 0 && offsetof(HitObjectPrefix, y) == 4 &&
                    offsetof(HitObjectPrefix, type) == 8 &&
                    offsetof(HitObjectPrefix, hit_sound) == 12 &&
                    offsetof(HitObjectPrefix, time) == 16,
                "the fast path decodes prefix fields as one record");
#if FOSU_SIMD_X86
  const __m256i digits = _mm256_sub_epi8(ascii, zero);
  const LaneMasks& lm = kLaneMasks[shape.index];
  const __m256i perm = _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.perm));
  const __m256i shuf = _mm256_load_si256(reinterpret_cast<const __m256i*>(lm.shuf));
  const __m256i placed =
      _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(digits, perm), shuf);
  const __m256i pair_weights =
      _mm256_setr_epi8(0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10, 1, 0, 0, 10, 1, 0, 0, 10,
                       1, 0, 0, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
  const __m256i word_weights =
      _mm256_setr_epi16(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1);
  const __m256i words = _mm256_maddubs_epi16(placed, pair_weights);
  const __m256i dwords = _mm256_madd_epi16(words, word_weights);
  const __m128i lo = _mm256_castsi256_si128(dwords);
  const __m128i thi = _mm256_extracti128_si256(dwords, 1);
  // Sum hitSound contributions from the two lanes into the fourth field.
  const __m128i fields = _mm_add_epi32(lo, _mm_slli_si128(thi, 12));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&prefix), fields);
  if (shape.time_span <= 9) [[likely]] {
    // Up to eight time digits fit in signed int32. Combine and widen in
    // SIMD registers.
    const __m128i packed = _mm_packus_epi32(thi, thi);
    const __m128i combined =
        _mm_madd_epi16(packed, _mm_setr_epi16(0, 0, 10000, 1, 0, 0, 0, 0));
    const __m128i pair = _mm_shuffle_epi32(combined, _MM_SHUFFLE(0, 0, 0, 1));
    _mm_store_sd(&prefix.time, _mm_cvtepi32_pd(pair));
  } else {
    const uint64_t t = static_cast<uint32_t>(_mm_extract_epi32(thi, 1)) * 100000000ull +
                       static_cast<uint32_t>(_mm_extract_epi32(thi, 2)) * 10000ull +
                       static_cast<uint32_t>(_mm_extract_epi32(thi, 3));
    if (t > INT32_MAX)
      return std::nullopt;
    prefix.time = static_cast<double>(t);
  }
#else
  const Bytes32 digits{{vsubq_u8(ascii.val[0], zero), vsubq_u8(ascii.val[1], zero)}};
  const auto& shuf = kPrefixShuffles[shape.index];
  const auto fields = decimal_groups(vqtbl2q_u8(digits, vld1q_u8(shuf.bytes)));
  const auto times = decimal_groups(vqtbl2q_u8(digits, vld1q_u8(shuf.bytes + 16)));
  vst1q_u32(reinterpret_cast<uint32_t*>(&prefix), fields);
  if (shape.time_span <= 9) [[likely]] {
    // Up to eight digits fit in uint32; combine the two four-digit groups
    // before converting to double.
    const uint32_t weights[2] = {10000, 1};
    const auto terms = vmul_u32(vget_high_u32(times), vld1_u32(weights));
    const auto sum = vpadd_u32(terms, terms);
    prefix.time = vgetq_lane_f64(vcvtq_f64_u64(vmovl_u32(sum)), 0);
  } else {
    const uint64_t t = uint64_t(vgetq_lane_u32(times, 1)) * 100000000 +
                       uint64_t(vgetq_lane_u32(times, 2)) * 10000 +
                       vgetq_lane_u32(times, 3);
    if (t > INT32_MAX)
      return std::nullopt;
    prefix.time = static_cast<double>(t);
  }
#endif
  return prefix;
}

// Returns nullopt to request the scalar parser for a structurally unusual
// prefix: signs, decimals, empty or over-long fields, or missing delimiters.
// The byte after hitSound must be a comma or line ending.
inline std::optional<ParsedHitObjectPrefix> try_parse_hitobject_prefix_fast(
    Bytes32 ascii,
    const char* line) {
  const auto shape =
      classify_hitobject_prefix(nondigit_mask32(ascii), comma_mask32(ascii));
  if (!shape.ok)
    return std::nullopt;
  const char after = line[shape.p4];
  if (!(after == ',' || after == '\n' || after == '\0' ||
        (after == '\r' && (line[shape.p4 + 1] == '\n' || line[shape.p4 + 1] == '\0'))))
    return std::nullopt;
  const auto prefix = decode_hitobject_prefix(ascii, broadcast_byte<'0'>(), shape);
  if (!prefix)
    return std::nullopt;
  return ParsedHitObjectPrefix{*prefix, line + shape.p4};
}

inline std::optional<ParsedHitObjectPrefix> try_parse_hitobject_prefix_fast(
    const char* line) {
  const Bytes32 ascii = load32(line);
  return try_parse_hitobject_prefix_fast(ascii, line);
}

#endif  // FOSU_SIMD

}  // namespace fosu::internal
