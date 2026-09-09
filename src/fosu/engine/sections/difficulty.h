#pragma once

#include <fosu/engine/parsing/key_value.h>
#include <algorithm>

namespace fosu::internal {

inline std::optional<double> parse_difficulty_rating(std::string_view input) {
  if (const auto value = parse_field_float(input))
    return std::clamp(*value, 0.0, 10.0);
  return std::nullopt;
}

inline std::optional<double> parse_slider_multiplier(std::string_view input) {
  if (const auto value = parse_field_double(input))
    return std::clamp(*value, 0.4, 3.6);
  return std::nullopt;
}

inline std::optional<double> parse_slider_tick_rate(std::string_view input) {
  if (const auto value = parse_field_double(input))
    return std::clamp(*value, 0.5, 8.0);
  return std::nullopt;
}

inline constexpr auto kDifficultyFields = make_string_lookup<FieldParser>({
    {"HPDrainRate", assign_field_value<&BeatmapHeader::hp, parse_difficulty_rating>},
    // parse_document clamps CS after all sections, using the final game mode.
    {"CircleSize", assign_field_value<&BeatmapHeader::cs, parse_field_float>},
    {"OverallDifficulty",
     assign_field_value<&BeatmapHeader::od, parse_difficulty_rating>},
    {"SliderMultiplier",
     assign_field_value<&BeatmapHeader::slider_multiplier, parse_slider_multiplier>},
    {"SliderTickRate",
     assign_field_value<&BeatmapHeader::slider_tick_rate, parse_slider_tick_rate>},
});

inline const char* parse_difficulty_section(Beatmap& beatmap,
                                            std::optional<double>& approach_rate,
                                            const char* p,
                                            const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    const auto field = split_key_value(line);
    if (!field)
      return;
    if (field->key == "ApproachRate") {
      // parse_document supplies the final OD if no valid AR was specified.
      if (const auto value = parse_difficulty_rating(field->value))
        approach_rate = *value;
      else
        ++beatmap.stats.malformed_lines;
    } else {
      parse_key_value<kDifficultyFields>(beatmap, *field);
    }
  });
}

}  // namespace fosu::internal
