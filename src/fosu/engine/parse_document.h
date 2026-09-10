#pragma once

#include <fosu/engine/parsing/chunk_list.h>
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

// Compiled once per engine ISA. Each section consumes its body and returns the
// next header or EOF. Parsed arrays grow in scratch chunks and are flattened
// into exact result-arena spans once the whole document has been accepted.
// Input has kBufferPadding readable zero bytes; string views refer into it.
inline bool parse_document(std::span<const char> input,
                           Beatmap& beatmap,
                           Arena* result_arena,
                           Arena* scratch_arena,
                           ParseOptions options) noexcept {
  if (input.empty())
    return true;
  const TempArena temporary_storage{scratch_arena};
  ChunkList<Break> breaks;
  ChunkList<uint32_t> colours;
  ChunkList<TimingPoint> timing_points;
  ChunkList<HitObject> hit_objects;
  ChunkList<Slider> sliders;
  ChunkList<SliderPoint> slider_points;
  const char* end = input.data() + input.size();
  const char* p = parse_preamble(beatmap, input.data(), end);
  const int time_offset = beatmap.format_version < 5 ? 24 : 0;
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
        p = parse_editor_section(beatmap, p, end);
        break;
      case Section::Metadata:
        p = parse_metadata_section(beatmap, p, end);
        break;
      case Section::Difficulty:
        p = parse_difficulty_section(beatmap, approach_rate, p, end);
        break;
      case Section::Events:
        p = parse_events_section(beatmap, scratch_arena, breaks, p, end, time_offset);
        break;
      case Section::TimingPoints:
        p = parse_timing_points_section(beatmap, scratch_arena, timing_points, p, end,
                                        time_offset);
        break;
      case Section::Colours:
        p = parse_colours_section(beatmap, scratch_arena, colours, p, end);
        break;
      case Section::HitObjects:
        p = parse_hitobjects_section(beatmap, scratch_arena, hit_objects, sliders,
                                     slider_points, p, end, time_offset);
        break;
      case Section::None:
      case Section::Unknown:
        p = skip_section(p, end);
        break;
    }
    if (!p)
      return false;
  }

  // An omitted (or wholly invalid) AR inherits the final OD across sections.
  beatmap.ar = approach_rate.value_or(beatmap.od);
  // General may follow Difficulty or repeat; CS depends on the final mode.
  beatmap.cs = beatmap.mode == 3 ? std::clamp(beatmap.cs, 1.0, 18.0)
                                 : std::clamp(beatmap.cs, 0.0, 10.0);
  auto flat_breaks = flatten_chunk_list(result_arena, breaks);
  auto flat_colours = flatten_chunk_list(result_arena, colours);
  auto flat_timing_points = flatten_chunk_list(result_arena, timing_points);
  auto flat_hit_objects = flatten_chunk_list(result_arena, hit_objects);
  auto flat_sliders = flatten_chunk_list(result_arena, sliders);
  auto flat_slider_points = flatten_chunk_list(result_arena, slider_points);
  if (!flat_breaks || !flat_colours || !flat_timing_points || !flat_hit_objects ||
      !flat_sliders || !flat_slider_points) {
    return false;
  }
  beatmap.breaks = flat_breaks.value();
  beatmap.combo_colours = flat_colours.value();
  beatmap.timing_points = flat_timing_points.value();
  beatmap.hit_objects = flat_hit_objects.value();
  beatmap.sliders = flat_sliders.value();
  beatmap.slider_points = flat_slider_points.value();
  return true;
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
