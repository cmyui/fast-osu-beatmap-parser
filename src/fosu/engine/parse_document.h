#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/hit_objects/slider.h>
#include <fosu/engine/parsing/key_value.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/parsing/section_names.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/engine/sections/colours.h>
#include <fosu/engine/sections/difficulty.h>
#include <fosu/engine/sections/editor.h>
#include <fosu/engine/sections/events.h>
#include <fosu/engine/sections/general.h>
#include <fosu/engine/sections/hit_objects.h>
#include <fosu/engine/sections/metadata.h>
#include <fosu/engine/sections/timing_points.h>
#include <fosu/format.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fosu::internal {

static_assert(kSectionGeneral == 1u << static_cast<i32>(Section::General) &&
                  kSectionDifficulty ==
                      1u << static_cast<i32>(Section::Difficulty) &&
                  kSectionHitObjects ==
                      1u << static_cast<i32>(Section::HitObjects),
              "public section bits mirror the internal Section ordinals");

// The values each client gives fields a map omits, where they differ.
inline void apply_stable_defaults(Beatmap& beatmap) {
  beatmap.countdown = 1;  // Countdown.Normal
  beatmap.beatmap_id = 0;
}

inline void apply_lazer_defaults(Beatmap& beatmap) {
  beatmap.countdown = 0;  // CountdownType.None
  beatmap.beatmap_id = -1;
}

// Both clients read the version as the whole integer after the header's last
// 'v' ("osu file format v5v4" is version 4) and refuse a map whose integer
// does not parse. stable reads it with Int32.Parse, and from a line that only
// needs to start with "osu file format"; lazer reads it with its ParseInt.
template <Client C>
bool parse_format_version(Beatmap& beatmap, std::string_view line) {
  if (!line.starts_with(C == Client::Stable ? "osu file format"
                                            : "osu file format v"))
    return true;
  const size_t v = line.rfind('v');
  const char*  number = line.data() + (v == std::string_view::npos ? 0 : v + 1);
  const char*  end = line.data() + line.size();
  i64          value;
  const char*  next;
  if constexpr (C == Client::Stable) {
    const char* first = skip_numeric_space(number, end);
    next = skip_numeric_space(parse_i64(first, end, value), end);
    if (next == first || value < INT32_MIN || value > INT32_MAX)
      return false;
  } else {
    next = parse_osu_int(number, end, value);
    if (next == number)
      return false;
  }
  if (next != end)
    return false;
  beatmap.format_version = static_cast<i32>(value);
  return true;
}

inline const char* skip_bom(const char* p, const char* end) {
  if (end - p >= 3 && static_cast<u8>(p[0]) == 0xEF &&
      static_cast<u8>(p[1]) == 0xBB && static_cast<u8>(p[2]) == 0xBF)
    p += 3;
  return p;
}

// The 1-based number of the line starting at `line`. Like .NET's StreamReader,
// "\r\n", "\n" and a lone "\r" each end a line.
inline u32 line_number(const char* begin, const char* line) {
  u32 number = 1;
  for (const char* p = begin; p < line; ++p)
    number += *p == '\n' || (*p == '\r' && (p + 1 == line || p[1] != '\n'));
  return number;
}

// stable reads General, Metadata, Difficulty and TimingPoints with a separate
// header reader that never leaves [HitObjects] once it enters it.
inline bool read_by_stable_header_reader(Section section) {
  return section == Section::General || section == Section::Metadata ||
         section == Section::Difficulty || section == Section::TimingPoints;
}

// Where the sections start, and the section in effect before the first
// header.
struct Preamble {
  const char* body;
  Section     section;
  ParseError  error;
};

// stable takes the version from the first line only, whatever it is: a
// header there starts its section for the object reader alone. Lazer reads
// it from the first non-blank line, and reads every line before the first
// header as [General].
template <Client C>
Preamble parse_preamble(Beatmap& beatmap, const char* begin, const char* end) {
  const char* p = skip_bom(begin, end);
  if constexpr (C == Client::Stable) {
    const auto first = read_line(p, end);
    if (!parse_format_version<C>(beatmap, first.text))
      return {.body = end,
              .section = Section::None,
              .error = {ParseErrorCode::Unloadable, 1}};
    Section section = Section::None;
    if (section_header_line<C>(first.text.data(),
                               first.text.data() + first.text.size())) {
      section = match_section<C>(first.text, section);
      if (read_by_stable_header_reader(section))
        section = Section::None;
    }
    return {.body = first.next, .section = section, .error = {}};
  } else {
    for (const char* q = p; q < end;) {
      const auto line = read_line(q, end);
      if (const auto text = trim_field(line.text); !text.empty()) {
        if (!parse_format_version<C>(beatmap, text))
          return {.body = end,
                  .section = Section::None,
                  .error = {ParseErrorCode::Unloadable, line_number(begin, q)}};
        break;
      }
      q = line.next;
    }
    // The version line itself has no key, so it reads as no General field.
    return {.body = p, .section = Section::General, .error = {}};
  }
}

// Parses every section, with the map's format rules and the target client's
// behaviour fixed at compile time. Each section consumes its body and returns
// the next header or EOF; framing and scalar fallbacks stay inside the
// section.
template <Format F, Client C>
ParseError parse_sections(const char*  begin,
                          Preamble     preamble,
                          const char*  end,
                          Beatmap&     beatmap,
                          ParseOptions options,
                          size_t&      velocity_preset_count,
                          bool&        velocity_presets_seen) {
  size_t             break_count = 0, colour_count = 0, timing_point_count = 0;
  HitObjectCounts    counts;
  std::optional<f64> approach_rate;
  u32                pending = options.sections & 0x1FEu;
  bool               hit_objects_seen = false;
  const char*        p = preamble.body;
  Section            section = preamble.section;

  while (true) {
    const u32 bit = 1u << static_cast<i32>(section);
    if (!(options.sections & bit)) {
      if (!pending)
        break;
      p = skip_section<C>(p, end);
    } else {
      pending &= ~bit;
      switch (section) {
        case Section::General:
          p = parse_general_section<F, C>(beatmap, p, end);
          break;
        case Section::Editor:
          p = parse_editor_section<F, C>(beatmap, velocity_preset_count,
                                         velocity_presets_seen, p, end);
          break;
        case Section::Metadata:
          p = parse_metadata_section<C>(beatmap, p, end);
          break;
        case Section::Difficulty:
          p = parse_difficulty_section<C>(beatmap, approach_rate, p, end);
          break;
        case Section::Events:
          p = parse_events_section<F, C>(beatmap, break_count, p, end);
          break;
        case Section::TimingPoints:
          p = parse_timing_points_section<F, C>(beatmap, timing_point_count, p,
                                                end);
          break;
        case Section::Colours:
          p = parse_colours_section<C>(beatmap, colour_count, p, end);
          break;
        case Section::HitObjects:
          p = parse_hitobjects_section<F, C>(beatmap, counts, p, end);
          break;
        case Section::None:
        case Section::Unknown:
          p = skip_section<C>(p, end);
          break;
      }
    }
    if (p >= end)
      break;
    const auto header = read_line(p, end);
    p = header.next;
    section = match_section<C>(header.text, section);
    if constexpr (C == Client::Stable) {
      if (hit_objects_seen && read_by_stable_header_reader(section))
        section = Section::Unknown;
      hit_objects_seen |= section == Section::HitObjects;
    }
  }

  if (counts.unloadable_line)
    return {ParseErrorCode::Unloadable,
            line_number(begin, counts.unloadable_line)};
  // An omitted (or wholly invalid) AR inherits the final OD across sections.
  beatmap.ar = approach_rate.value_or(beatmap.od);
  // In lazer, General may follow Difficulty or repeat; CS depends on the
  // final mode.
  if constexpr (C == Client::Lazer)
    beatmap.cs = beatmap.mode == 3 ? std::clamp(beatmap.cs, 1.0, 18.0)
                                   : std::clamp(beatmap.cs, 0.0, 10.0);
  if ((options.sections & kSectionEditor) && !velocity_presets_seen &&
      beatmap.velocity_presets.size() >= 3)
    velocity_preset_count = set_default_velocity_presets(beatmap);
  beatmap.breaks = beatmap.breaks.first(break_count);
  beatmap.combo_colours = beatmap.combo_colours.first(colour_count);
  beatmap.timing_points = beatmap.timing_points.first(timing_point_count);
  beatmap.hit_objects = beatmap.hit_objects.first(counts.objects);
  beatmap.sliders = beatmap.sliders.first(counts.sliders);
  beatmap.slider_segments =
      beatmap.slider_segments.first(counts.slider_segments);
  beatmap.slider_points = beatmap.slider_points.first(counts.slider_points);
  beatmap.velocity_presets =
      beatmap.velocity_presets.first(velocity_preset_count);
  return {};
}

template <Client C>
ParseError parse_document_as(std::span<const char> input,
                             Beatmap&              beatmap,
                             ParseOptions          options) {
  if constexpr (C == Client::Stable)
    apply_stable_defaults(beatmap);
  else
    apply_lazer_defaults(beatmap);
  size_t      velocity_preset_count = 0;
  bool        velocity_presets_seen = false;
  const char* begin = input.data();
  const char* end = begin + input.size();
  const auto  preamble = parse_preamble<C>(beatmap, begin, end);
  if (preamble.error.code != ParseErrorCode::None)
    return preamble.error;
  return with_format(beatmap.format_version, C, [&]<Format F>() {
    return parse_sections<F, C>(begin, preamble, end, beatmap, options,
                                velocity_preset_count, velocity_presets_seen);
  });
}

// Compiled once per engine ISA. The target client and then the format version
// select the parse.
// Input has kBufferPadding readable zero bytes; string views refer into it.
inline ParseError parse_document(std::span<const char> input,
                                 Beatmap&              beatmap,
                                 ParseOptions          options) noexcept {
  return options.client == Client::Lazer
             ? parse_document_as<Client::Lazer>(input, beatmap, options)
             : parse_document_as<Client::Stable>(input, beatmap, options);
}

// The ISA this code was compiled for, not a runtime choice based on the host
// CPU.
inline constexpr ParsingEngine compiled_engine{
#if FOSU_SIMD_X86
    EngineKind::Avx2,
#elif FOSU_SIMD_NEON
    EngineKind::Neon,
#else
    EngineKind::Scalar,
#endif
    parse_document,
};

}  // namespace fosu::internal
