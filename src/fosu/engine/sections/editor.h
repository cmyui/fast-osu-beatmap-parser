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

inline const char* parse_editor_section(Beatmap& beatmap,
                                        const char* p,
                                        const char* end) {
  return parse_key_value_section(beatmap, kEditorFields, p, end);
}

}  // namespace fosu::internal
