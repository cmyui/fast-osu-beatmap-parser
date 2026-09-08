#pragma once

#include <bit>
#include "byte_scan.hpp"
#include "prefix.hpp"

namespace fosu::internal {

// Shared bounded fallback. Optional legacy fields may be absent, but a
// present field must parse completely; NaN is meaningful only when inherited.
// UseCommaMask is for lines of at most 64 bytes, with a mask relative to p.
template <bool UseCommaMask = false, typename T>
inline bool parse_timing_fields(const char* p,
                                const char* end,
                                T& tp,
                                uint64_t commas = 0) {
  [[maybe_unused]] const char* line = p;
  double time, beat_length;
  const char* q = parse_osu_double(p, end, time);
  if (q == p || q >= end || *q != ',')
    return false;
  p = q + 1;
  q = parse_beat_length(p, end, beat_length);
  if (q == p)
    return false;
  p = q;
  int64_t rest[6] = {4, 0, 0, 100, 1, 0};
  for (int i = 0; i < 6 && p < end; ++i) {
    if (*p++ != ',')
      return false;
    const char* field_end;
    if constexpr (UseCommaMask) {
      const size_t offset = static_cast<size_t>(p - line);
      commas = offset < 64 ? commas & (~0ull << offset) : 0;
      field_end = commas ? line + std::countr_zero(commas) : end;
    } else {
      field_end = find_byte<','>(p, end);
    }
    if (p == field_end)
      return false;
    if (i == 4)
      rest[i] = *p == '1';
    else if (i == 0 && *p == '0') {
      // The official decoder treats any meter field beginning in 0 as
      // 4/4. Preserve a plain raw zero; use 4 for nonnumeric spellings.
      q = parse_osu_int(p, field_end, rest[i]);
      if (q != field_end)
        rest[i] = 4;
    } else {
      q = parse_osu_int(p, field_end, rest[i]);
      if (q == p || q != field_end || (i == 0 && rest[i] <= 0))
        return false;
    }
    p = field_end;
  }
  // Additional legacy columns are ignored by the official decoder.
  if ((p < end && *p != ',') || (rest[4] != 0 && std::isnan(beat_length)))
    return false;
  tp.time = time;
  tp.beat_length = beat_length;
  tp.meter = clamp_i32(rest[0]);
  tp.sample_set = clamp_i32(rest[1]);
  tp.sample_index = clamp_i32(rest[2]);
  tp.volume = clamp_i32(rest[3]);
  tp.uninherited = rest[4] != 0;
  tp.effects = static_cast<uint32_t>(rest[5]);
  return true;
}

#if FOSU_SIMD
// One-pass timing point parse for the editor-emitted 8-field shape.
// Two preloaded 32-byte vectors cover the whole line (real max: 39
// bytes); one comma mask and one non-digit mask yield every field
// boundary; every value is computed speculatively and a single `valid`
// predicate — accumulated arithmetically, never branched on per field —
// decides. Structural surprises (old 2/7-field formats, decimal or >8
// digit offsets, junk bytes) return false to defer to the generic parser.
//
// All eight TimingPoint fields are written unconditionally; the caller
// discards the write by not advancing its cursor when this returns false.
// always_inline: gcc leaves this out of line otherwise — a call plus
// per-call constant rebuilds on every timing line (disassembly audit).
// Geometry derived by a successful parse, exported so the section loop's
// shape cache can replay identically-shaped lines without re-deriving it.
struct TpGeom {
    uint8_t c[7];                          // comma positions
    uint8_t bl_il, bl_fl1, bl_fl2, bl_frac;  // beatLength digit layout
    uint8_t bl_neg, bl_has_dot;
};

template <typename T>
__attribute__((always_inline))
inline bool fast_parse_timing_point_masked(uint64_t commas, uint64_t nondig,
                                           const char* p, size_t len,
                                           T& tp,
                                           TpGeom* geom = nullptr) {
    if (std::popcount(commas) != 7) return false;

    // Seven comma positions -> eight fields.
    const uint64_t m1 = (commas & (commas - 1));
    const uint64_t m2 = (m1 & (m1 - 1));
    const uint64_t m3 = (m2 & (m2 - 1));
    const uint64_t m4 = (m3 & (m3 - 1));
    const uint64_t m5 = (m4 & (m4 - 1));
    const uint64_t m6 = (m5 & (m5 - 1));
    const auto c0 = static_cast<uint32_t>(trailing_zeros(commas));
    const auto c1 = static_cast<uint32_t>(trailing_zeros(m1));
    const auto c2 = static_cast<uint32_t>(trailing_zeros(m2));
    const auto c3 = static_cast<uint32_t>(trailing_zeros(m3));
    const auto c4 = static_cast<uint32_t>(trailing_zeros(m4));
    const auto c5 = static_cast<uint32_t>(trailing_zeros(m5));
    const auto c6 = static_cast<uint32_t>(trailing_zeros(m6));

    // Offset: an integer with 1..8 digits — editor-emitted files never
    // produce negative or decimal offsets (those defer via the purity
    // check below). Speculative lengths are clamped into 1..8 so shifts
    // stay defined; `valid` already rules the clamped cases out.
    bool valid = (c0 - 1) <= 7;
    tp.time = static_cast<double>(swar_parse_u64_safe(p, ((c0 - 1) & 7) + 1));

    // beatLength: [c0+1, c1), optional leading '-', optional fraction.
    const char* f = p + c0 + 1;
    const bool neg = *f == '-';
    f += neg;
    const uint32_t flen = c1 - c0 - 1 - neg;
    // Distance from f to the first non-digit: the '.' if present, else
    // the comma at c1.
    const auto int_len = static_cast<uint32_t>(trailing_zeros(nondig >> (f - p)));
    const bool has_dot = int_len < flen;
    // The purity popcount below counts "one extra non-digit" for the dot;
    // verify that byte actually is '.' (fuzz-found: any junk byte in the
    // field would otherwise be accepted as the decimal point).
    valid &= !has_dot || f[int_len] == '.';
    const uint32_t frac_len = flen - int_len - has_dot;
    valid &= (int_len - 1) <= 7;
    valid &= frac_len <= 13;  // real files: 0 (67%) or 12-13
    valid &= int_len + frac_len <= 18;  // avoid uint64 mantissa wrap
    const uint32_t il = ((int_len - 1) & 7) + 1;
    const uint32_t fl1 = frac_len <= 8 ? frac_len : 8;
    const uint32_t fl2 = frac_len - fl1;
    const char* fp = f + il + 1;  // il, not int_len: bounds speculative
                                  // reads within kBufferPadding on garbage
    uint64_t mant = swar_parse_u64_safe(f, il);
    const uint64_t fm1 = fl1 ? swar_parse_u64_safe(fp, fl1) : 0;
    const uint64_t fm2 = fl2 ? swar_parse_u64_safe(fp + 8, fl2) : 0;
    mant = mant * kPow10u[fl1] + fm1;
    mant = mant * kPow10u[fl2 & 7] + fm2;  // fl2 <= 5 when valid
    double bl =
        static_cast<double>(mant) / kPow10[frac_len <= 13 ? frac_len : 0];
    // mant >= 0, so the sign bit can be OR'd in directly (no fp select).
    bl = std::bit_cast<double>(std::bit_cast<uint64_t>(bl) |
                               (static_cast<uint64_t>(neg) << 63));
    tp.beat_length = bl;

    // Whole-line digit purity in one predicate: the only non-digit bytes
    // allowed are the 7 commas, the optional dot, and the optional minus.
    valid &= std::popcount(nondig) ==
             7 + static_cast<int>(has_dot) + static_cast<int>(neg);

    // Six small-int tail fields, straight-line (no arrays, no loop — gcc
    // spills indexed locals to the stack).
    const uint32_t t0 = c2 - c1 - 1;
    const uint32_t t1 = c3 - c2 - 1;
    const uint32_t t2 = c4 - c3 - 1;
    const uint32_t t3 = c5 - c4 - 1;
    const uint32_t t4 = c6 - c5 - 1;
    const uint32_t t5 = static_cast<uint32_t>(len) - c6 - 1;
    valid &= ((t0 - 1) | (t1 - 1) | (t2 - 1) | (t3 - 1) | (t4 - 1) |
              (t5 - 1)) <= 7;
    tp.meter =
        static_cast<int32_t>(swar_parse_u64_safe(p + c1 + 1, ((t0 - 1) & 7) + 1));
    tp.sample_set =
        static_cast<int32_t>(swar_parse_u64_safe(p + c2 + 1, ((t1 - 1) & 7) + 1));
    tp.sample_index =
        static_cast<int32_t>(swar_parse_u64_safe(p + c3 + 1, ((t2 - 1) & 7) + 1));
    tp.volume =
        static_cast<int32_t>(swar_parse_u64_safe(p + c4 + 1, ((t3 - 1) & 7) + 1));
    tp.uninherited = p[c5 + 1] == '1';
    tp.effects =
        static_cast<uint32_t>(swar_parse_u64_safe(p + c6 + 1, ((t5 - 1) & 7) + 1));

    if (valid && mant > kMaxExactDoubleInteger) {
        double exact;
        if (bounded_double(p + c0 + 1, p + c1, exact) != p + c1) return false;
        tp.beat_length = exact;
    }
    if (geom && valid) {
        geom->c[0] = static_cast<uint8_t>(c0);
        geom->c[1] = static_cast<uint8_t>(c1);
        geom->c[2] = static_cast<uint8_t>(c2);
        geom->c[3] = static_cast<uint8_t>(c3);
        geom->c[4] = static_cast<uint8_t>(c4);
        geom->c[5] = static_cast<uint8_t>(c5);
        geom->c[6] = static_cast<uint8_t>(c6);
        geom->bl_il = static_cast<uint8_t>(il);
        geom->bl_fl1 = static_cast<uint8_t>(fl1);
        geom->bl_fl2 = static_cast<uint8_t>(fl2);
        geom->bl_frac = static_cast<uint8_t>(frac_len);
        geom->bl_neg = neg;
        geom->bl_has_dot = has_dot;
    }
    return valid;
}

// Compatibility entry (tests/fuzzers): computes the masks itself.
template <typename T>
__attribute__((always_inline))
inline bool fast_parse_timing_point(Bytes32 a, Bytes32 b, const char* p,
                                    size_t len, T& tp) {
    if (len > 64 || len < 15) return false;  // real lines: 20..39 bytes
    const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
    const uint64_t commas =
        (comma_mask32(a) | static_cast<uint64_t>(comma_mask32(b)) << 32) &
        line_mask;
    const uint64_t nondig =
        (nondigit_mask32(a) |
         static_cast<uint64_t>(nondigit_mask32(b)) << 32) &
        line_mask;
    return fast_parse_timing_point_masked(commas, nondig, p, len, tp);
}

// --- Timing-line shape cache -------------------------------------------
//
// A [TimingPoints] section reuses a handful of byte-level line layouts:
// on the 10k-map production census, the top 8 exact (comma mask, nondigit
// mask, length) shapes cover a median 98.1% of a file's timing lines. A
// line whose masks equal an already-accepted shape is structurally
// identical to it — same comma positions, same dot/minus placement, all
// other bytes digits — so validation collapses to the key comparison and
// every field converts at cached offsets. The six 1-2 digit tail fields
// convert together with one cached-shuffle maddubs when they fit a
// 16-byte window; wider shapes fall back to cached-offset SWAR.
struct TpShapeRow {
    uint64_t commas = 0, nondig = 0;
    uint32_t len = 0;  // 0 = empty slot (never matches: len >= 15)
    TpGeom g{};
    uint8_t simd_tails = 0;
    alignas(16) int8_t shuf[16];   // gathers tail digits, 2B lanes
    alignas(16) uint8_t subv[16];  // '0' on digit lanes, 0 on padding
};

struct TpShapeCache {
    TpShapeRow rows[16];
    static uint32_t slot(uint64_t commas) {
        return static_cast<uint32_t>((commas * 0x9E3779B97F4A7C15ull) >> 60);
    }
};

// The masks pin every byte's class (clear nondigit bit == digit, comma
// bit == literal comma), but not WHICH non-digit character occupies the
// beatLength's sign/dot slots — a fuzz-found hole. Two byte compares
// close it; everything else follows from mask equality.
inline bool tp_shape_match(const TpShapeRow& row, uint64_t commas,
                           uint64_t nondig, size_t len, const char* p) {
    if (row.commas != commas || row.nondig != nondig ||
        row.len != static_cast<uint32_t>(len))
        return false;
    const TpGeom& g = row.g;
    const bool neg_ok = !g.bl_neg || p[g.c[0] + 1] == '-';
    const bool dot_ok =
        !g.bl_has_dot || p[g.c[0] + 1 + g.bl_neg + g.bl_il] == '.';
    return neg_ok && dot_ok;
}

inline void tp_shape_insert(TpShapeCache& cache, uint64_t commas,
                            uint64_t nondig, size_t len, const TpGeom& g) {
    TpShapeRow& r = cache.rows[TpShapeCache::slot(commas)];
    r.commas = commas;
    r.nondig = nondig;
    r.len = static_cast<uint32_t>(len);
    r.g = g;
    // Tail SIMD layout: all six fields 1-2 digits and spanning <= 16
    // bytes from the first tail digit. Lane i bytes {2i, 2i+1} gather
    // {tens, ones} (0x80 = zero lane), built as two integers — inserts
    // are ~15% of a short section's timing cost, so no byte loops here.
    const uint32_t base = g.c[1] + 1;
    const uint32_t span = static_cast<uint32_t>(len) - base;
    uint64_t shuf_lo = 0, shuf_hi = 0, sub_lo = 0, sub_hi = 0;
    uint32_t maxlen = 0;
    for (int i = 0; i < 6; ++i) {
        const uint32_t hi = i < 5 ? g.c[i + 2] : static_cast<uint32_t>(len);
        const uint32_t off = g.c[i + 1] + 1 - base;
        const uint32_t flen = hi - g.c[i + 1] - 1;
        maxlen = flen > maxlen ? flen : maxlen;
        // flen 1: {0x80, off}; flen 2: {off, off+1}
        const uint64_t pair = flen == 2 ? (off | ((off + 1) << 8)) : (0x80u | (off << 8));
        const uint64_t sub = flen == 2 ? 0x3030u : 0x3000u;
        if (i < 4) {
            shuf_lo |= pair << (16 * i);
            sub_lo |= sub << (16 * i);
        } else {
            shuf_hi |= pair << (16 * (i - 4));
            sub_hi |= sub << (16 * (i - 4));
        }
    }
    r.simd_tails = span <= 16 && maxlen <= 2;
    shuf_hi |= 0x8080808000000000ull;  // lanes 6-7 (bytes 12-15) unused: zero
    memcpy(r.shuf, &shuf_lo, 8);
    memcpy(r.shuf + 8, &shuf_hi, 8);
    memcpy(r.subv, &sub_lo, 8);
    memcpy(r.subv + 8, &sub_hi, 8);
}

// Replays a cached shape. Arithmetic mirrors the one-pass parser exactly,
// so results are bit-identical (pinned by fuzz).
template <typename T>
inline void tp_shape_convert(const TpShapeRow& r, const char* p,
                             T& tp) {
    const TpGeom& g = r.g;
    tp.time = static_cast<double>(swar_parse_u64(p, g.c[0]));

    const char* f = p + g.c[0] + 1 + g.bl_neg;
    uint64_t mant = swar_parse_u64_safe(f, g.bl_il);
    const char* fp = f + g.bl_il + 1;
    const uint64_t fm1 = g.bl_fl1 ? swar_parse_u64_safe(fp, g.bl_fl1) : 0;
    const uint64_t fm2 =
        g.bl_fl2 ? swar_parse_u64_safe(fp + 8, g.bl_fl2) : 0;
    mant = mant * kPow10u[g.bl_fl1] + fm1;
    mant = mant * kPow10u[g.bl_fl2] + fm2;
    double bl;
    if (mant > kMaxExactDoubleInteger)
        bounded_double(f, p + g.c[1], bl);  // digit shape was validated; excludes sign
    else
        bl = static_cast<double>(mant) / kPow10[g.bl_frac];
    bl = std::bit_cast<double>(std::bit_cast<uint64_t>(bl) |
                               (static_cast<uint64_t>(g.bl_neg) << 63));
    tp.beat_length = bl;

    if (r.simd_tails) {
        alignas(16) uint16_t t[8];
#if FOSU_SIMD_X86
        const __m128i v = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p + g.c[1] + 1));
        const __m128i gathered = _mm_shuffle_epi8(
            v, _mm_load_si128(reinterpret_cast<const __m128i*>(r.shuf)));
        const __m128i digits = _mm_sub_epi8(
            gathered,
            _mm_load_si128(reinterpret_cast<const __m128i*>(r.subv)));
        const __m128i vals =
            _mm_maddubs_epi16(digits, _mm_set1_epi16(0x010A));
        _mm_store_si128(reinterpret_cast<__m128i*>(t), vals);
#else
        const auto v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + g.c[1] + 1));
        const auto gathered = vqtbl1q_u8(v, vld1q_u8(reinterpret_cast<const uint8_t*>(r.shuf)));
        const auto digits = vsubq_u8(gathered, vld1q_u8(reinterpret_cast<const uint8_t*>(r.subv)));
        constexpr uint8_t weights[16] = {10,1,10,1,10,1,10,1,10,1,10,1,10,1,10,1};
        vst1q_u16(t, vpaddlq_u8(vmulq_u8(digits, vld1q_u8(weights))));
#endif
        tp.meter = t[0];
        tp.sample_set = t[1];
        tp.sample_index = t[2];
        tp.volume = t[3];
        tp.uninherited = p[g.c[5] + 1] == '1';
        tp.effects = t[5];
    } else {
        const uint32_t len = r.len;
        const uint32_t t0 = g.c[2] - g.c[1] - 1;
        const uint32_t t1 = g.c[3] - g.c[2] - 1;
        const uint32_t t2 = g.c[4] - g.c[3] - 1;
        const uint32_t t3 = g.c[5] - g.c[4] - 1;
        const uint32_t t5 = len - g.c[6] - 1;
        tp.meter = static_cast<int32_t>(swar_parse_u64(p + g.c[1] + 1, t0));
        tp.sample_set =
            static_cast<int32_t>(swar_parse_u64(p + g.c[2] + 1, t1));
        tp.sample_index =
            static_cast<int32_t>(swar_parse_u64(p + g.c[3] + 1, t2));
        tp.volume = static_cast<int32_t>(swar_parse_u64(p + g.c[4] + 1, t3));
        tp.uninherited = p[g.c[5] + 1] == '1';
        tp.effects =
            static_cast<uint32_t>(swar_parse_u64(p + g.c[6] + 1, t5));
    }
}


#endif

}  // namespace fosu::internal
