#pragma once

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/parsing/sample_sets.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/engine/primitives/packed_digits.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/engine/timing_points/beat_length.h>
#include <fosu/format.h>
#include <fosu/types.h>

#include <cmath>
#include <cstddef>
#include <optional>

namespace fosu::internal {

// Parse a timing point, including omitted legacy fields. A present field
// must parse completely; NaN is meaningful only when inherited. Invalid
// lines return nullopt.
template <Format F>
std::optional<TimingPoint> parse_timing_point(const char* p, const char* end) {
  f64         time, beat_length;
  const char* q = parse_osu_double(p, end, time);
  if (q == p || q >= end || *q != ',')
    return std::nullopt;
  p = q + 1;
  q = parse_beat_length(p, end, beat_length);
  if (q == p)
    return std::nullopt;
  p = q;
  i64 rest[6] = {4, 0, 0, 100, 1, 0};
  for (i32 i = 0; i < 6 && p < end; ++i) {
    if (*p++ != ',')
      return std::nullopt;
    const char* field_end = find_byte<','>(p, end);
    if (p == field_end)
      return std::nullopt;
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
        return std::nullopt;
    }
    p = field_end;
  }
  // Additional legacy columns are ignored by the official decoder.
  if ((p < end && *p != ',') || (rest[4] != 0 && std::isnan(beat_length)))
    return std::nullopt;
  const auto sample_set = parse_sample_set(rest[1]);
  if (!sample_set)
    return std::nullopt;
  return TimingPoint{
      .time = time + F.time_offset,
      .beat_length = beat_length,
      .meter = clamp_i32(rest[0]),
      .sample_set = *sample_set,
      .sample_index = clamp_i32(rest[2]),
      .volume = clamp_i32(rest[3]),
      .uninherited = rest[4] != 0,
      .effects = static_cast<u32>(rest[5]),
  };
}

// The editor's row: an unsigned integer time, a finite decimal beat length
// and six small unsigned integers. Returns nullopt for anything else, which
// the general parser then decides.
template <Format F>
FOSU_ALWAYS_INLINE std::optional<TimingPoint> parse_common_timing_point(
    const char* p,
    const char* end) {
  const u32 time_digits = digit_run8(p);
  if (time_digits - 1 > 7 || p[time_digits] != ',')
    return std::nullopt;
  const f64 time = static_cast<f64>(swar_parse_u64(p, time_digits));
  p += time_digits + 1;

  f64        beat_length;
  const auto parsed = fast_float::from_chars(p, end, beat_length);
  if (parsed.ec != std::errc() || *parsed.ptr != ',' ||
      !(std::abs(beat_length) <= INT32_MAX))
    return std::nullopt;
  p = parsed.ptr;

  // meter, sample set, sample index, volume, uninherited (one digit), effects
  u32 fields[6];
  for (u32 i = 0; i < 6; ++i) {
    if (*p != ',')
      return std::nullopt;
    const u32 digits = digit_run8(++p);
    if (digits - 1 > (i == 4 ? 0u : 2u))
      return std::nullopt;
    fields[i] = swar_parse_u32(p, digits);
    p += digits;
  }
  if (p != end)
    return std::nullopt;
  const auto sample_set = parse_sample_set(fields[1]);
  if (!sample_set)
    return std::nullopt;
  return TimingPoint{
      .time = time + F.time_offset,
      .beat_length = beat_length,
      .meter = static_cast<i32>(fields[0]),
      .sample_set = *sample_set,
      .sample_index = static_cast<i32>(fields[2]),
      .volume = static_cast<i32>(fields[3]),
      .uninherited = fields[4] == 1,
      .effects = fields[5],
  };
}

}  // namespace fosu::internal
