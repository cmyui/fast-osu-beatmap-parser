#pragma once

#include <fosu/engine/parsing/section_names.h>
#include <fosu/engine/parsing_engine.h>
#include <fosu/engine/sections/colours.h>
#include <fosu/engine/sections/difficulty.h>
#include <fosu/engine/sections/editor.h>
#include <fosu/engine/sections/events.h>
#include <fosu/engine/sections/general.h>
#include <fosu/engine/sections/hit_objects.h>
#include <fosu/engine/sections/metadata.h>
#include <fosu/engine/sections/timing_points.h>

namespace fosu::internal {

static_assert(kSectionGeneral == 1u << static_cast<int>(Section::General) &&
                  kSectionDifficulty == 1u << static_cast<int>(Section::Difficulty) &&
                  kSectionHitObjects == 1u << static_cast<int>(Section::HitObjects),
              "public section bits mirror the internal Section ordinals");

inline const char* parse_preamble(Beatmap& beatmap, const char* p, const char* end) {
  if (end - p >= 3 && static_cast<uint8_t>(p[0]) == 0xEF &&
      static_cast<uint8_t>(p[1]) == 0xBB && static_cast<uint8_t>(p[2]) == 0xBF)
    p += 3;
  return for_each_section_line(p, end, [&](std::string_view line) {
    const size_t version = line.find("osu file format v");
    if (version != std::string_view::npos) {
      int64_t value;
      const char* number = line.data() + version + 17;
      if (parse_i64(number, line.data() + line.size(), value) != number)
        beatmap.format_version = clamp_i32(value);
    }
  });
}

// Compiled once per engine ISA. Each section consumes its body and returns
// the next header or EOF; framing and scalar fallbacks stay inside the section.
// Input has kBufferPadding readable zero bytes; string views refer into it.
inline void parse_document(std::span<const char> input,
                           Beatmap& beatmap,
                           ParseOptions options) noexcept {
  if (input.empty())
    return;
  const char* end = input.data() + input.size();
  const char* p = parse_preamble(beatmap, input.data(), end);
  const int time_offset = beatmap.format_version < 5 ? 24 : 0;
  size_t break_count = 0, colour_count = 0, timing_point_count = 0;
  size_t hit_object_count = 0, slider_count = 0, slider_point_count = 0;
  size_t slider_segment_count = 0, velocity_preset_count = 0;
  std::optional<double> approach_rate;
  uint32_t pending = options.sections & 0x1FEu;

  while (p < end) {
    const auto header = read_line(p, end);
    const auto section = match_section(header.text);
    const uint32_t bit = 1u << static_cast<int>(section);
    p = header.next;
    if (!(options.sections & bit)) {
      if (!pending)
        break;
      p = skip_section(p, end);
      continue;
    }
    pending &= ~bit;

    switch (section) {
      case Section::General:
        p = parse_general_section(beatmap, p, end);
        break;
      case Section::Editor:
        p = parse_editor_section(beatmap, velocity_preset_count, p, end);
        break;
      case Section::Metadata:
        p = parse_metadata_section(beatmap, p, end);
        break;
      case Section::Difficulty:
        p = parse_difficulty_section(beatmap, approach_rate, p, end);
        break;
      case Section::Events:
        p = parse_events_section(beatmap, break_count, p, end, time_offset);
        break;
      case Section::TimingPoints:
        p = parse_timing_points_section(beatmap, timing_point_count, p, end, time_offset);
        break;
      case Section::Colours:
        p = parse_colours_section(beatmap, colour_count, p, end);
        break;
      case Section::HitObjects:
        p = parse_hitobjects_section(beatmap, hit_object_count, slider_count,
                                     slider_segment_count, slider_point_count, p, end,
                                     time_offset);
        break;
      case Section::None:
      case Section::Unknown:
        p = skip_section(p, end);
        break;
    }
  }

  // An omitted (or wholly invalid) AR inherits the final OD across sections.
  beatmap.ar = approach_rate.value_or(beatmap.od);
  // General may follow Difficulty or repeat; CS depends on the final mode.
  beatmap.cs = beatmap.mode == 3 ? std::clamp(beatmap.cs, 1.0, 18.0)
                                 : std::clamp(beatmap.cs, 0.0, 10.0);
  beatmap.breaks = beatmap.breaks.first(break_count);
  beatmap.combo_colours = beatmap.combo_colours.first(colour_count);
  beatmap.timing_points = beatmap.timing_points.first(timing_point_count);
  beatmap.hit_objects = beatmap.hit_objects.first(hit_object_count);
  beatmap.sliders = beatmap.sliders.first(slider_count);
  beatmap.slider_segments = beatmap.slider_segments.first(slider_segment_count);
  beatmap.slider_points = beatmap.slider_points.first(slider_point_count);
  beatmap.velocity_presets = beatmap.velocity_presets.first(velocity_preset_count);
}

// The ISA this code was compiled for, not a runtime choice based on the host CPU.
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
