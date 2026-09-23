#pragma once

#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/engine/hit_objects/common_fields.h>
#include <fosu/engine/hit_objects/object_types.h>
#include <fosu/engine/hit_objects/samples.h>
#include <fosu/engine/hit_objects/slider.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/digit_groups.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

namespace fosu::internal {

// Parses one complete [HitObjects] section and publishes accepted objects.
// Object-specific syntax is delegated after the common fields are decoded.

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, or a trailing hit sample.
FOSU_NOINLINE inline bool parse_hitobject_details(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    HitObject&                     object,
    const char*                    p,
    const char*                    end,
    const HitObjectParseConstants& constants) {
  switch (classify_hitobject_kind(object.type)) {
    case HitObjectKind::Circle: {
      const auto details = parse_circle_details(p, end);
      if (!details)
        return false;
      object.end_time = 0;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Slider:
      return p < end && *p == ',' &&
             parse_slider(beatmap, counts, object, p + 1, end, constants);
    case HitObjectKind::Spinner: {
      const auto details = parse_spinner_details(p, end);
      if (!details)
        return false;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Hold: {
      const auto details = parse_hold_details(object.time, p, end);
      if (!details)
        return false;
      object.end_time = details->end_time;
      object.hit_sample = details->hit_sample;
      return true;
    }
    case HitObjectKind::Invalid:
      return false;
  }
  return false;
}

// Lines the fast prefix does not accept: the scalar prefix parser handles
// signed, decimal, spaced or wide fields; anything else is malformed.
inline std::optional<HitObject> parse_hitobject_line_scalar(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const char*                    p,
    const char*                    line_end,
    const HitObjectParseConstants& constants) {
  f32         x;
  const char* next = parse_osu_float(p, line_end, x, 131072);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  f32 y;
  next = parse_osu_float(p, line_end, y, 131072);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  f64 time;
  next = parse_osu_double(p, line_end, time);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  i64 type;
  next = parse_osu_int(p, line_end, type);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  i64 hitsound;
  next = parse_osu_int(p, line_end, hitsound);
  if (next == p || (next < line_end && *next != ','))
    return std::nullopt;

  if (beatmap.format_version < 128) {
    x = static_cast<f32>(static_cast<i32>(x));
    y = static_cast<f32>(static_cast<i32>(y));
  }

  ++beatmap.stats.slow_path_lines;
  HitObject object{
      .x = x,
      .y = y,
      .type = static_cast<u32>(type),
      .hitsound = static_cast<u32>(hitsound),
      .time = time,
      .end_time = 0,
      .slider = HitObject::kNoSlider,
      .new_combo = false,
      .combo_skip = 0,
      .hit_sample = {},
  };
  if (!parse_hitobject_details(beatmap, counts, object, next, line_end,
                               constants)) {
    return std::nullopt;
  }
  return object;
}

// Interpret a successfully decoded record before publishing it to the arena.
// The preceding accepted object is still in source order, including across
// repeated HitObjects sections. No separate state crosses the engine boundary.
inline HitObject normalize_hitobject(HitObject object,
                                     size_t    preceding_count,
                                     bool      preceding_was_spinner,
                                     i32       offset) {
  const bool explicit_combo = object.type & 4;
  object.time += offset;
  object.new_combo = false;
  object.combo_skip = 0;
  if (object.is_circle() || object.is_slider()) {
    object.new_combo =
        !preceding_count || explicit_combo || preceding_was_spinner;
    object.combo_skip = explicit_combo ? (object.type >> 4) & 7 : 0;
    object.end_time = object.is_circle() ? object.time : 0;
  } else if (object.is_spinner()) {
    object.new_combo = explicit_combo;
    object.x = 256;
    object.y = 192;
    object.end_time = std::max(object.time, object.end_time + offset);
  } else {
    // Legacy holds clamp against the offset start before offsetting the end.
    object.end_time = std::max(object.time, object.end_time) + offset;
  }
  return object;
}

inline const char* parse_hitobjects_section_scalar(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const char*                    p,
    const char*                    file_end,
    const HitObjectParseConstants& constants,
    i32                            time_offset) {
  while (p < file_end) {
    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    const char* line_end = find_line_end(p, file_end);
    if (c == '[' && section_header_line(p, line_end))
      break;
    const char* following_line = after_line_ending(line_end, file_end);
    if (!ignored_line(p, line_end)) {
      if (const auto object = parse_hitobject_line_scalar(
              beatmap, counts, p, line_end, constants)) {
        const bool preceding_was_spinner =
            counts.objects && (object->is_circle() || object->is_slider()) &&
            !(object->type & 4) &&
            classify_hitobject_kind(
                beatmap.hit_objects[counts.objects - 1].type) ==
                HitObjectKind::Spinner;
        beatmap.hit_objects[counts.objects] = normalize_hitobject(
            *object, counts.objects, preceding_was_spinner, time_offset);
        ++counts.objects;
      } else [[unlikely]] {
        ++beatmap.stats.malformed_lines;
      }
    }
    p = following_line;
  }
  return p;
}

#if FOSU_SIMD
// SIMD section loop. One 32-byte load per line yields the newline, comma and
// non-digit masks. Lines outside the common editor shape take the scalar path.
inline const char* parse_hitobjects_section_simd(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const char*                    p,
    const char*                    file_end,
    const HitObjectParseConstants& constants,
    i32                            time_offset) {
  const ByteVector comma_value = constants.comma;
  const ByteVector zero = constants.zero;
  u32              fast_lines = 0;
  u32              malformed = 0;
  bool             preceding_was_spinner =
      counts.objects &&
      classify_hitobject_kind(beatmap.hit_objects[counts.objects - 1].type) ==
          HitObjectKind::Spinner;

  while (p < file_end) {
    const Bytes32 ascii = load32(p);
#if FOSU_SIMD_NEON
    const u32 first_ending = first_line_end32(ascii);
#else
    const auto endings = line_end_mask32(ascii);
#endif
    const auto commas = equal_mask32(ascii, comma_value);
    const u32  nondigits = nondigit_mask32(ascii);
#if FOSU_SIMD_NEON
    const char* line_end =
        first_ending < 32 ? p + first_ending : find_line_end(p + 32, file_end);
#else
    const char* line_end =
        endings ? p + trailing_zeros(endings) : find_line_end(p + 32, file_end);
#endif
    const char* next_line = after_line_ending(line_end, file_end);
    const auto  length = static_cast<size_t>(line_end - p);
    u32         p1, p2, prefix_end, hitsound_length, mask_index;
    bool        common_layout;
    // Common editor prefixes: ddd,ddd,ddddd,d,d and ddd,ddd,dddddd,d,d.
    // Match every digit boundary and comma before using fixed shuffle masks.
    if ((nondigits & 0x3ffffu) == 0x2a088u && (commas & 0xffffu) == 0xa088u) {
      p1 = 7;
      p2 = 13;
      prefix_end = 17;
      hitsound_length = 1;
      mask_index = kFiveDigitTimeMaskIndex;
      common_layout = true;
    } else if ((nondigits & 0x7ffffu) == 0x54088u &&
               (commas & 0x1ffffu) == 0x14088u) {
      p1 = 7;
      p2 = 14;
      prefix_end = 18;
      hitsound_length = 1;
      mask_index = kSixDigitTimeMaskIndex;
      common_layout = true;
    } else {
      const u32 m1 = nondigits & (nondigits - 1);
      const u32 m2 = m1 & (m1 - 1);
      const u32 m3 = m2 & (m2 - 1);
      const u32 m4 = m3 & (m3 - 1);
      const u64 p0 = trailing_zeros(nondigits);
      p1 = trailing_zeros(m1);
      p2 = trailing_zeros(m2);
      const u64 p3 = trailing_zeros(m3);
      prefix_end = trailing_zeros(m4);

      // Lanes (low to high): the first four delimiter positions. Their
      // differences are the lengths of x, y, time and type.
      const u64 delimiter_positions =
          p0 | u64(p1) << 16 | u64(p2) << 32 | p3 << 48;
      const u64 field_lengths =
          (delimiter_positions - (delimiter_positions << 16)) -
          0x0002000200020001ull;
      const u64  overlong_fields = field_lengths + 0x7FFD7FF67FFD7FFDull;
      const bool field_lengths_ok =
          ((field_lengths | overlong_fields) & 0x8000800080008000ull) == 0;
      const u32  through_type = static_cast<u32>((2ull << p3) - 1);
      const bool delimiters_are_commas =
          ((nondigits ^ commas) & through_type) == 0;
      hitsound_length = prefix_end - static_cast<u32>(p3) - 1;
      const bool hitsound_length_ok = hitsound_length - 1 <= 1;
      common_layout =
          field_lengths_ok & delimiters_are_commas & hitsound_length_ok;
      mask_index = static_cast<u32>(
                       (delimiter_positions * 0x003C001B00020001ull) >> 48) -
                   158;
      mask_index = mask_index * 2 + hitsound_length - 1;
    }
    const char after_prefix = p[prefix_end];

    if (common_layout && (prefix_end == length || after_prefix == ',' ||
                          after_prefix == '\0')) [[likely]] {
      const u32 time_span = static_cast<u32>(p2 - p1);

      u32       fields[4];
      f64       time;
      bool      time_ok = true;
#if FOSU_SIMD_X86
      const auto digits = _mm256_sub_epi8(ascii, zero);
      const auto place_digits = [&](const LaneMasks& masks) {
        const __m256i perm =
            _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.perm));
        const __m256i shuf =
            _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.shuf));
        return _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(digits, perm),
                                   shuf);
      };
      __m256i placed;
      if (mask_index == kFiveDigitTimeMaskIndex)
        placed = place_digits(kLaneMasks[kFiveDigitTimeMaskIndex]);
      else if (mask_index == kSixDigitTimeMaskIndex)
        placed = place_digits(kLaneMasks[kSixDigitTimeMaskIndex]);
      else
        placed = place_digits(kLaneMasks[mask_index]);
      const __m256i pair_weights = _mm256_setr_epi8(
          0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10, 1, 0, 0, 10, 1, 0, 0, 10, 1,
          0, 0, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
      const __m256i word_weights =
          _mm256_setr_epi16(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1);
      const __m256i words = _mm256_maddubs_epi16(placed, pair_weights);
      const __m256i values = _mm256_madd_epi16(words, word_weights);
      const __m128i low = _mm256_castsi256_si128(values);
      const __m128i time_groups = _mm256_extracti128_si256(values, 1);
      const __m128i field_values =
          _mm_add_epi32(low, _mm_slli_si128(time_groups, 12));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(fields), field_values);
      if (time_span <= 9) [[likely]] {
        const __m128i packed = _mm_packus_epi32(time_groups, time_groups);
        const __m128i combined =
            _mm_madd_epi16(packed, _mm_setr_epi16(0, 0, 10000, 1, 0, 0, 0, 0));
        const __m128i pair =
            _mm_shuffle_epi32(combined, _MM_SHUFFLE(0, 0, 0, 1));
        _mm_store_sd(&time, _mm_cvtepi32_pd(pair));
      } else {
        const u64 parsed_time =
            static_cast<u32>(_mm_extract_epi32(time_groups, 1)) * 100000000ull +
            static_cast<u32>(_mm_extract_epi32(time_groups, 2)) * 10000ull +
            static_cast<u32>(_mm_extract_epi32(time_groups, 3));
        time_ok = parsed_time <= INT32_MAX;
        time = static_cast<f64>(parsed_time);
      }
#else
      const Bytes32 digits{
          {vsubq_u8(ascii.val[0], zero), vsubq_u8(ascii.val[1], zero)}};
      const PrefixShuffle& masks = kPrefixShuffles[mask_index];
      const auto           field_values =
          decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes)));
      const auto time_groups =
          decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes + 16)));
      vst1q_u32(fields, field_values);
      if (time_span <= 9) [[likely]] {
        const u32  weights[2] = {10000, 1};
        const auto terms =
            vmul_u32(vget_high_u32(time_groups), vld1_u32(weights));
        const auto sum = vpadd_u32(terms, terms);
        time = vgetq_lane_f64(vcvtq_f64_u64(vmovl_u32(sum)), 0);
      } else {
        const u64 parsed_time =
            u64(vgetq_lane_u32(time_groups, 1)) * 100000000 +
            u64(vgetq_lane_u32(time_groups, 2)) * 10000 +
            vgetq_lane_u32(time_groups, 3);
        time_ok = parsed_time <= INT32_MAX;
        time = static_cast<f64>(parsed_time);
      }
#endif

      if (time_ok) [[likely]] {
        ++fast_lines;
        const HitObjectKind kind = classify_hitobject_kind(fields[2]);
        const auto          make_object = [&] {
          return HitObject{
              .x = static_cast<f32>(fields[0]),
              .y = static_cast<f32>(fields[1]),
              .type = fields[2],
              .hitsound = fields[3],
              .time = time,
              .end_time = 0,
              .slider = HitObject::kNoSlider,
              .new_combo = false,
              .combo_skip = 0,
              .hit_sample = {},
          };
        };
        const bool simple_circle =
            kind == HitObjectKind::Circle &&
            (prefix_end == length ||
             (length - prefix_end == 9 && after_prefix == ',' &&
              short_sample(p + prefix_end + 1)));
        // Publish common circles before the address-taken object used by
        // slider and detail handlers is materialized on the stack.
        if (simple_circle) [[likely]] {
          HitObject circle = make_object();
          if (prefix_end != length)
            circle.hit_sample = {p + prefix_end + 1, 8};
          beatmap.hit_objects[counts.objects] = normalize_hitobject(
              circle, counts.objects, preceding_was_spinner, time_offset);
          preceding_was_spinner = false;
          ++counts.objects;
          p = next_line;
          continue;
        }
        HitObject object = make_object();
        if (kind == HitObjectKind::Circle) {
          if (!parse_hitobject_details(beatmap, counts, object, p + prefix_end,
                                       line_end, constants)) [[unlikely]] {
            ++malformed;
            p = next_line;
            continue;
          }
        } else if (kind == HitObjectKind::Slider) {
          if (prefix_end >= length || after_prefix != ',' ||
              !parse_slider(beatmap, counts, object, p + prefix_end + 1,
                            line_end, constants)) [[unlikely]] {
            ++malformed;
            p = next_line;
            continue;
          }
        } else if (!parse_hitobject_details(beatmap, counts, object,
                                            p + prefix_end, line_end,
                                            constants)) [[unlikely]] {
          ++malformed;
          p = next_line;
          continue;
        }

        beatmap.hit_objects[counts.objects] = normalize_hitobject(
            object, counts.objects, preceding_was_spinner, time_offset);
        preceding_was_spinner = kind == HitObjectKind::Spinner;
        ++counts.objects;
      } else {
        const auto object = parse_hitobject_line_scalar(beatmap, counts, p,
                                                        line_end, constants);
        if (object) {
          beatmap.hit_objects[counts.objects] = normalize_hitobject(
              *object, counts.objects, preceding_was_spinner, time_offset);
          preceding_was_spinner =
              classify_hitobject_kind(object->type) == HitObjectKind::Spinner;
          ++counts.objects;
        } else [[unlikely]] {
          ++malformed;
        }
      }
    } else {
      const char c = *p;
      if (c == '\r' || c == '\n') {
        ++p;
        continue;
      }
      if (c == '[' && section_header_line(p, line_end))
        break;
      if (!ignored_line(p, line_end)) {
        if (const auto object = parse_hitobject_line_scalar(
                beatmap, counts, p, line_end, constants)) {
          beatmap.hit_objects[counts.objects] = normalize_hitobject(
              *object, counts.objects, preceding_was_spinner, time_offset);
          preceding_was_spinner =
              classify_hitobject_kind(object->type) == HitObjectKind::Spinner;
          ++counts.objects;
        } else [[unlikely]] {
          ++malformed;
        }
      }
    }
    p = next_line;
  }

  beatmap.stats.fast_path_lines += fast_lines;
  beatmap.stats.malformed_lines += malformed;
  return p;
}
#endif

inline const char* parse_hitobjects_section(Beatmap&         beatmap,
                                            HitObjectCounts& counts,
                                            const char*      p,
                                            const char*      file_end,
                                            i32              time_offset = 0) {
  const HitObjectParseConstants constants;
#if FOSU_SIMD
  return parse_hitobjects_section_simd(beatmap, counts, p, file_end, constants,
                                       time_offset);
#else
  return parse_hitobjects_section_scalar(beatmap, counts, p, file_end,
                                         constants, time_offset);
#endif
}

}  // namespace fosu::internal
