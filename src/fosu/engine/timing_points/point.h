#pragma once

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/parsing/lines.h>
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

// The editor's row, e.g. "1234,-66.6666666666667,4,2,1,60,0,0": an unsigned
// integer time, a short decimal beat length and six small unsigned integers,
// of which meter, sample set and uninherited have one digit. Returns the start
// of the next line, or nullptr for anything else, which the general parser
// then decides. Times are ordered, so `time_digits` carries the previous row's
// width and is only measured when it changes.
template <Format F>
FOSU_ALWAYS_INLINE const char* parse_common_timing_point(const char* p,
                                                         const char* file_end,
                                                         u32& time_digits,
                                                         TimingPoint& point) {
  u32 digits = time_digits;
  if (p[digits] != ',' || !leading_digits(p, digits)) [[unlikely]] {
    digits = digit_run8(p);
    if (digits - 1 > 7 || p[digits] != ',')
      return nullptr;
    time_digits = digits;
  }
  const f64 time = static_cast<f64>(swar_parse_u64(p, digits));
  p += digits + 1;

  // Inherited points most often scale by exactly -100.
  f64 beat_length = -100;
  if (load_u32_le(p) == load_u32_le("-100") && p[4] == ',')
    p += 4;
  else if (!(p = parse_short_decimal(p, beat_length)))
    return nullptr;

  // ,meter,set,index,volume,uninherited,effects
  if (p[0] != ',' || p[2] != ',' || p[4] != ',' || !is_digit(p[1]) ||
      !is_digit(p[3]))
    return nullptr;
  const u32   index_digits = digit_run8(p + 5);
  const char* volume = p + 6 + index_digits;
  const u32   volume_digits = digit_run8(volume);
  const char* uninherited = volume + volume_digits + 1;
  const char* line_end = uninherited + 3;
  if (index_digits - 1 > 1 || volume[-1] != ',' || volume_digits - 1 > 2 ||
      uninherited[-1] != ',' || !is_digit(uninherited[0]) ||
      uninherited[1] != ',' || !is_digit(uninherited[2]) ||
      (line_end != file_end && *line_end != '\r' && *line_end != '\n'))
    return nullptr;
  const auto sample_set = parse_sample_set(static_cast<u32>(p[3] - '0'));
  if (!sample_set)
    return nullptr;
  point = TimingPoint{
      .time = time + F.time_offset,
      .beat_length = beat_length,
      .meter = p[1] - '0',
      .sample_set = *sample_set,
      .sample_index = static_cast<i32>(swar_parse_u32(p + 5, index_digits)),
      .volume = static_cast<i32>(swar_parse_u32(volume, volume_digits)),
      .uninherited = uninherited[0] == '1',
      .effects = static_cast<u32>(uninherited[2] - '0'),
  };
  return after_line_ending(line_end, file_end);
}

}  // namespace fosu::internal
