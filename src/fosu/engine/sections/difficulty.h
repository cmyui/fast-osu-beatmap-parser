#pragma once

#include <fosu/engine/parsing/key_value.h>

namespace fosu::internal {

inline constexpr auto kDifficultyFields = make_string_lookup<FieldParser>({
    {"HPDrainRate", assign_field_value<&BeatmapHeader::hp, parse_field_float>},
    {"CircleSize", assign_field_value<&BeatmapHeader::cs, parse_field_float>},
    {"OverallDifficulty", assign_field_value<&BeatmapHeader::od, parse_field_float>},
    {"SliderMultiplier",
     assign_field_value<&BeatmapHeader::slider_multiplier, parse_field_double>},
    {"SliderTickRate",
     assign_field_value<&BeatmapHeader::slider_tick_rate, parse_field_double>},
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
      if (const auto value = parse_field_float(field->value))
        approach_rate = *value;
      else
        ++beatmap.stats.malformed_lines;
    } else {
      parse_key_value(beatmap, kDifficultyFields, *field);
    }
  });
}

}  // namespace fosu::internal
