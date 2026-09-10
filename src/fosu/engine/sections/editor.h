#pragma once

#include <fosu/engine/parsing/key_value.h>
#include <algorithm>

namespace fosu::internal {

inline std::optional<double> parse_editor_scale(std::string_view input) {
  if (const auto value = parse_field_double(input))
    return std::max(0.0, *value);
  return std::nullopt;
}

inline std::optional<int32_t> parse_beat_divisor(std::string_view input) {
  if (const auto value = parse_field_integer(input))
    return std::clamp(*value, 1, 64);
  return std::nullopt;
}

inline constexpr auto kEditorFields = make_string_lookup<FieldParser>({
    {"Bookmarks", assign_field_text<&BeatmapHeader::bookmarks>},
    {"DistanceSpacing",
     assign_field_value<&BeatmapHeader::distance_spacing, parse_editor_scale>},
    {"BeatDivisor", assign_field_value<&BeatmapHeader::beat_divisor, parse_beat_divisor>},
    {"GridSize", assign_field_value<&BeatmapHeader::grid_size, parse_field_integer>},
    {"TimelineZoom",
     assign_field_value<&BeatmapHeader::timeline_zoom, parse_editor_scale>},
});

inline bool parse_velocity_presets(Beatmap& beatmap,
                                   size_t& count,
                                   std::string_view input) {
  const size_t begin = count;
  const char* p = input.data();
  const char* end = p + input.size();
  while (p < end) {
    const char* comma = find_byte<','>(p, end);
    const auto value = parse_field_double({p, static_cast<size_t>(comma - p)});
    if (!value) {
      count = begin;
      return false;
    }
    if (count == beatmap.velocity_presets.size()) {
      count = begin;
      return false;
    }
    beatmap.velocity_presets[count++] = *value;
    p = comma == end ? end : comma + 1;
  }
  return true;
}

inline const char* parse_editor_section(Beatmap& beatmap,
                                        size_t& velocity_preset_count,
                                        const char* p,
                                        const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    const auto field = split_key_value(line);
    if (!field)
      return;
    if (field->key == "VelocityPresets") {
      if (!parse_velocity_presets(beatmap, velocity_preset_count, field->value))
        ++beatmap.stats.malformed_lines;
      return;
    }
    parse_key_value(beatmap, kEditorFields, *field);
  });
}

}  // namespace fosu::internal
