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

inline bool parse_editor_field(BeatmapHeader& header, const KeyValue& field) {
  switch (string_hash(field.key)) {
    case "Bookmarks"_hash:
      if (field.key == "Bookmarks")
        return assign_field_text<&BeatmapHeader::bookmarks>(header, field.value);
      break;
    case "DistanceSpacing"_hash:
      if (field.key == "DistanceSpacing")
        return assign_field_value<&BeatmapHeader::distance_spacing, parse_editor_scale>(
            header, field.value);
      break;
    case "BeatDivisor"_hash:
      if (field.key == "BeatDivisor")
        return assign_field_value<&BeatmapHeader::beat_divisor, parse_beat_divisor>(
            header, field.value);
      break;
    case "GridSize"_hash:
      if (field.key == "GridSize")
        return assign_field_value<&BeatmapHeader::grid_size, parse_field_integer>(
            header, field.value);
      break;
    case "TimelineZoom"_hash:
      if (field.key == "TimelineZoom")
        return assign_field_value<&BeatmapHeader::timeline_zoom, parse_editor_scale>(
            header, field.value);
      break;
  }
  return true;
}

inline const char* parse_editor_section(Beatmap& beatmap,
                                        const char* p,
                                        const char* end) {
  return parse_key_value_section<parse_editor_field>(beatmap, p, end);
}

}  // namespace fosu::internal
