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
#include <fosu/format.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace fosu::internal {

// Parses one complete [HitObjects] section and publishes accepted objects.
// Object-specific syntax is delegated after the common fields are decoded.

// Everything after the "x,y,time,type,hitSound" prefix: slider params,
// spinner/hold end times, or a trailing hit sample.
template <Format F, Client C>
FOSU_NOINLINE bool parse_hitobject_details(
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
             parse_slider_as<F.lazer_format, C>(beatmap, counts, object, p + 1,
                                                end, constants);
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

struct HitObjectPrefix {
  HitObject   object;
  const char* rest;  // After hitSound: empty, or starting with ','.
};

// The x,y,time,type,hitSound prefix of a line the fast prefix does not
// accept: signed, decimal, spaced or wide fields.
template <Format F, Client C>
std::optional<HitObjectPrefix> parse_hitobject_prefix(const char* p,
                                                      const char* line_end) {
  f32         x;
  const char* next = parse_osu_float(p, line_end, x, kCoordinateLimit<C>);
  if (next == p || next >= line_end || *next != ',')
    return std::nullopt;
  p = next + 1;

  f32 y;
  next = parse_osu_float(p, line_end, y, kCoordinateLimit<C>);
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

  x = std::clamp(x, 0.0f, 512.0f);
  y = std::clamp(y, 0.0f, 512.0f);
  if constexpr (!F.lazer_format) {
    x = static_cast<f32>(static_cast<i32>(x));
    y = static_cast<f32>(static_cast<i32>(y));
  }

  return HitObjectPrefix{
      .object =
          {
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
          },
      .rest = next,
  };
}

// Anything else the scalar prefix parser rejects is malformed.
template <Format F, Client C>
std::optional<HitObject> parse_hitobject_line_scalar(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const char*                    p,
    const char*                    line_end,
    const HitObjectParseConstants& constants) {
  auto prefix = parse_hitobject_prefix<F, C>(p, line_end);
  if (!prefix)
    return std::nullopt;
  ++beatmap.stats.slow_path_lines;
  if (!parse_hitobject_details<F, C>(beatmap, counts, prefix->object,
                                     prefix->rest, line_end, constants)) {
    return std::nullopt;
  }
  return prefix->object;
}

// osu! (lazer) creates a circle once its prefix parses, and a spinner once
// its end time also parses, before reading the hit sample. A line rejected
// only for its sample therefore still counts as the previous object when the
// next one decides whether it starts a new combo. `rest` follows hitSound.
inline bool preceding_spinner_after_rejection(u32         type,
                                              bool        preceding_was_spinner,
                                              const char* rest,
                                              const char* line_end) {
  switch (classify_hitobject_kind(type)) {
    case HitObjectKind::Circle:
      return false;
    case HitObjectKind::Spinner: {
      if (rest == line_end || *rest != ',')
        return preceding_was_spinner;
      f64         end_time;
      const char* next = parse_osu_double(rest + 1, line_end, end_time);
      return (next != rest + 1 && (next == line_end || *next == ',')) ||
             preceding_was_spinner;
    }
    default:
      return preceding_was_spinner;
  }
}

// osu!stable cannot load a map with a hit object it fails to read, but
// ignores one whose type names no kind. Lazer skips both.
template <Client C>
void reject_hitobject(HitObjectCounts& counts,
                      bool&            preceding_was_spinner,
                      u32              type,
                      const char*      line,
                      const char*      rest,
                      const char*      line_end) {
  if constexpr (C == Client::Stable) {
    if (!counts.unloadable_line &&
        classify_hitobject_kind(type) != HitObjectKind::Invalid)
      counts.unloadable_line = line;
  } else {
    preceding_was_spinner = preceding_spinner_after_rejection(
        type, preceding_was_spinner, rest, line_end);
  }
}

template <Format F, Client C>
FOSU_NOINLINE void reject_hitobject_line(Beatmap&         beatmap,
                                         HitObjectCounts& counts,
                                         bool&            preceding_was_spinner,
                                         const char*      p,
                                         const char*      line_end) {
  ++beatmap.stats.malformed_lines;
  if (const auto prefix = parse_hitobject_prefix<F, C>(p, line_end))
    reject_hitobject<C>(counts, preceding_was_spinner, prefix->object.type, p,
                        prefix->rest, line_end);
  else if (C == Client::Stable && !counts.unloadable_line)
    counts.unloadable_line = p;
}

// Lines osu!stable skips before reading a hit object: blank, comments, and
// those indented with ' ' or '_'.
template <Client C>
bool ignored_hitobject_line(const char* p, const char* line_end) {
  if (C == Client::Stable && (*p == ' ' || *p == '_'))
    return true;
  return ignored_line(p, line_end);
}

// Interpret a successfully decoded record of `kind` before publishing it to
// the arena. The preceding object is in source order, including across
// repeated HitObjects sections.
inline HitObject normalize_hitobject(HitObject     object,
                                     HitObjectKind kind,
                                     size_t        preceding_count,
                                     bool          preceding_was_spinner,
                                     i32           offset) {
  const bool explicit_combo = object.type & 4;
  object.time += offset;
  object.new_combo = false;
  object.combo_skip = 0;
  if (kind == HitObjectKind::Circle || kind == HitObjectKind::Slider) {
    object.new_combo =
        !preceding_count || explicit_combo || preceding_was_spinner;
    object.combo_skip = explicit_combo ? (object.type >> 4) & 7 : 0;
    object.end_time = kind == HitObjectKind::Circle ? object.time : 0;
  } else if (kind == HitObjectKind::Spinner) {
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

#if FOSU_SIMD
// Publishes an object whose x,y,time,type,hitSound prefix is decoded. `rest`
// is everything after the prefix: empty, or starting with ','.
template <Format F, Client C>
FOSU_ALWAYS_INLINE void accept_hitobject(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const HitObjectParseConstants& constants,
    bool&                          preceding_was_spinner,
    HitObject                      object,
    const char*                    line,
    const char*                    rest,
    const char*                    line_end) {
  const HitObjectKind kind = classify_hitobject_kind(object.type);
  const auto          rest_length = static_cast<size_t>(line_end - rest);
  // Most objects are circles with no sample or the editor's "0:0:0:0:".
  if (kind == HitObjectKind::Circle &&
      (rest_length == 0 || (rest_length == 9 && short_sample(rest + 1)))) {
    if (rest_length)
      object.hit_sample = {rest + 1, 8};
  } else if (!(kind == HitObjectKind::Slider
                   ? rest_length && parse_slider_as<F.lazer_format, C>(
                                        beatmap, counts, object, rest + 1,
                                        line_end, constants)
                   : parse_hitobject_details<F, C>(beatmap, counts, object,
                                                   rest, line_end, constants)))
      [[unlikely]] {
    ++beatmap.stats.malformed_lines;
    reject_hitobject<C>(counts, preceding_was_spinner, object.type, line, rest,
                        line_end);
    return;
  }
  const size_t count = counts.objects++;
  beatmap.hit_objects[count] = normalize_hitobject(
      object, kind, count, preceding_was_spinner, F.time_offset);
  preceding_was_spinner = kind == HitObjectKind::Spinner;
}

// Prefix shapes for one time width, keyed by the comma bits of a line's
// first eight bytes, which always include the commas after 1-3 digit x and y.
// Each entry also precomputes the common case of one-digit type and
// hitSound: its delimiter mask (including the byte after hitSound), prefix
// end and shuffle index. x == 0 marks any other key.
struct PrefixLayout {
  u8  x, y, common_end;
  u16 common_index;
  u32 common_delimiters;
};
template <u32 TimeDigits>
consteval std::array<PrefixLayout, 256> make_prefix_layouts() {
  std::array<PrefixLayout, 256> layouts{};
  for (u32 x = 1; x <= 3; ++x)
    for (u32 y = 1; y <= 3; ++y) {
      const u32 type_at = x + y + TimeDigits + 3;
      const u32 delimiters = (1u << x) | (1u << (x + y + 1)) |
                             (1u << (type_at - 1)) | (1u << (type_at + 1)) |
                             (1u << (type_at + 3));
      // The byte after hitSound may be a line ending rather than a comma, so
      // it must lie outside the key.
      if (type_at + 3 < 8)
        continue;
      layouts[delimiters & 0xff] = {
          .x = static_cast<u8>(x),
          .y = static_cast<u8>(y),
          .common_end = static_cast<u8>(type_at + 3),
          .common_index = static_cast<u16>(
              (((x - 1) * 3 + y - 1) * 10 + TimeDigits - 1) * 3 * 2),
          .common_delimiters = delimiters,
      };
    }
  return layouts;
}
template <u32 TimeDigits>
inline constexpr auto kPrefixLayouts = make_prefix_layouts<TimeDigits>();

// Parses lines whose time has exactly TimeDigits digits, with 1-3 digit x and
// y, 1-3 digit type and 1-2 digit hitSound. Times only grow through a map, so
// one width usually covers a long run of lines. Returns at the first other
// line.
template <u32 TimeDigits, Format F, Client C>
FOSU_NOINLINE const char* parse_hitobjects_fixed_time(
    Beatmap&                       beatmap,
    HitObjectCounts&               counts,
    const HitObjectParseConstants& constants,
    bool&                          preceding_was_spinner_ref,
    const char*                    p,
    const char*                    file_end) {
  bool preceding_was_spinner = preceding_was_spinner_ref;
  u32  fast_lines = 0;
  while (p < file_end) {
    const Bytes32       ascii = load32(p);
    const u32           commas = equal_mask32(ascii, constants.comma);
    const u32           nondigits = nondigit_mask32(ascii);

    const PrefixLayout& layout = kPrefixLayouts<TimeDigits>[commas & 0xff];
    u32                 prefix_end = layout.common_end;
    u32                 mask_index = layout.common_index;
    const u32 common = (2u << prefix_end) - 1;  // through the byte after
    if ((nondigits & common) != layout.common_delimiters ||
        (commas & layout.common_delimiters & (common >> 1)) !=
            (layout.common_delimiters & (common >> 1))) [[unlikely]] {
      // Longer type or hitSound: measure both digit runs.
      const u32 type_at = layout.x + layout.y + TimeDigits + 3;
      const u32 type_digits = trailing_zeros(nondigits >> type_at);
      if (!layout.x || type_digits - 1 > 2)
        break;
      const u32 sound_at = type_at + type_digits + 1;
      const u32 sound_digits = trailing_zeros(nondigits >> sound_at);
      if (sound_digits - 1 > 1)
        break;
      prefix_end = sound_at + sound_digits;
      const u32 delimiters =
          (1u << layout.x) | (1u << (layout.x + layout.y + 1)) |
          (1u << (type_at - 1)) | (1u << (type_at + type_digits));
      if ((nondigits & ((1u << prefix_end) - 1)) != delimiters ||
          (commas & delimiters) != delimiters)
        break;
      mask_index += (type_digits - 1) * 2 + sound_digits - 1;
    }
    // A NUL is the zero padding only at the end of the input.
    const char after = p[prefix_end];
    if (after != ',' && after != '\r' && after != '\n' &&
        (after != '\0' || p + prefix_end != file_end))
      break;

    u32 fields[4];
    f64 time;
#if FOSU_SIMD_X86
    const auto&   masks = kLaneMasks[mask_index];
    const __m256i placed = _mm256_shuffle_epi8(
        _mm256_permutevar8x32_epi32(
            _mm256_sub_epi8(ascii, constants.zero),
            _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.perm))),
        _mm256_load_si256(reinterpret_cast<const __m256i*>(masks.shuf)));
    const __m256i values = _mm256_madd_epi16(
        _mm256_maddubs_epi16(
            placed, _mm256_setr_epi8(0, 100, 10, 1, 0, 100, 10, 1, 0, 100, 10,
                                     1, 0, 0, 10, 1, 0, 0, 10, 1, 0, 0, 10, 1,
                                     10, 1, 10, 1, 10, 1, 10, 1)),
        _mm256_setr_epi16(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 1, 100, 1));
    const __m128i time_groups = _mm256_extracti128_si256(values, 1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(fields),
                     _mm_add_epi32(_mm256_castsi256_si128(values),
                                   _mm_slli_si128(time_groups, 12)));
    const __m128i packed = _mm_packus_epi32(time_groups, time_groups);
    const __m128i combined =
        _mm_madd_epi16(packed, _mm_setr_epi16(0, 0, 10000, 1, 0, 0, 0, 0));
    _mm_store_sd(&time, _mm_cvtepi32_pd(_mm_shuffle_epi32(
                            combined, _MM_SHUFFLE(0, 0, 0, 1))));
#else
    const Bytes32        digits{{vsubq_u8(ascii.val[0], constants.zero),
                                 vsubq_u8(ascii.val[1], constants.zero)}};
    const PrefixShuffle& masks = kPrefixShuffles[mask_index];
    vst1q_u32(fields,
              decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes))));
    const auto time_groups =
        decimal_groups(vqtbl2q_u8(digits, vld1q_u8(masks.bytes + 16)));
    const u32  weights[2] = {10000, 1};
    const auto terms = vmul_u32(vget_high_u32(time_groups), vld1_u32(weights));
    time = vgetq_lane_f64(vcvtq_f64_u64(vmovl_u32(vpadd_u32(terms, terms))), 0);
#endif

    const u32   first_ending = first_line_end32(ascii);
    const char* line_end =
        first_ending < 32 ? p + first_ending : find_line_end(p + 32, file_end);
    fields[0] = std::clamp(fields[0], 0u, 512u);
    fields[1] = std::clamp(fields[1], 0u, 512u);
    ++fast_lines;
    accept_hitobject<F, C>(beatmap, counts, constants, preceding_was_spinner,
                           HitObject{
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
                           },
                           p, p + prefix_end, line_end);
    p = after_line_ending(line_end, file_end);
  }
  beatmap.stats.fast_path_lines += fast_lines;
  preceding_was_spinner_ref = preceding_was_spinner;
  return p;
}

#endif

template <Format F, Client C>
const char* parse_hitobjects_section(Beatmap&         beatmap,
                                     HitObjectCounts& counts,
                                     const char*      p,
                                     const char*      file_end) {
  const HitObjectParseConstants constants;
  bool preceding_was_spinner = counts.preceding_was_spinner;
  while (p < file_end) {
#if FOSU_SIMD
    p = parse_hitobjects_fixed_time<1, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<2, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<3, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<4, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<5, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<6, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    p = parse_hitobjects_fixed_time<7, F, C>(
        beatmap, counts, constants, preceding_was_spinner, p, file_end);
    if (p >= file_end)
      break;
#endif
    // A line no fixed-width loop accepts: blank lines, comments, a section
    // header, or a prefix with another width or spelling.
    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    const char* line_end = find_line_end(p, file_end);
    if (c == '[' && section_header_line<C>(p, line_end))
      break;
    if (!ignored_hitobject_line<C>(p, line_end)) {
      if (const auto object = parse_hitobject_line_scalar<F, C>(
              beatmap, counts, p, line_end, constants)) {
        const size_t count = counts.objects++;
        const auto   kind = classify_hitobject_kind(object->type);
        beatmap.hit_objects[count] = normalize_hitobject(
            *object, kind, count, preceding_was_spinner, F.time_offset);
        preceding_was_spinner = kind == HitObjectKind::Spinner;
      } else [[unlikely]] {
        reject_hitobject_line<F, C>(beatmap, counts, preceding_was_spinner, p,
                                    line_end);
      }
    }
    p = after_line_ending(line_end, file_end);
  }
  counts.preceding_was_spinner = preceding_was_spinner;
  return p;
}

}  // namespace fosu::internal
