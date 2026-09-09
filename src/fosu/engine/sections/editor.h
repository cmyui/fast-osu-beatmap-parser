#pragma once

#include <fosu/engine/parsing/key_value.h>

namespace fosu::internal {

inline constexpr auto kEditorFields = make_string_lookup<FieldParser>({
    {"Bookmarks", assign_field_text<&BeatmapHeader::bookmarks>},
    {"DistanceSpacing",
     assign_field_value<&BeatmapHeader::distance_spacing, parse_field_double>},
    {"BeatDivisor",
     assign_field_value<&BeatmapHeader::beat_divisor, parse_field_integer>},
    {"GridSize", assign_field_value<&BeatmapHeader::grid_size, parse_field_integer>},
    {"TimelineZoom",
     assign_field_value<&BeatmapHeader::timeline_zoom, parse_field_double>},
});

inline const char* parse_editor_section(Beatmap& beatmap,
                                        const char* p,
                                        const char* end) {
  return parse_key_value_section(beatmap, kEditorFields, p, end);
}

}  // namespace fosu::internal
