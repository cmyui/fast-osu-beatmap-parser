#pragma once

#include <fosu/beatmap.h>
#include <fosu/beatmap_header.h>
#include <fosu/engine/parsing/field_values.h>
#include <fosu/engine/parsing/key_value.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/parse_options.h>
#include <fosu/types.h>

#include <algorithm>
#include <optional>
#include <string_view>

namespace fosu::internal {

// stable reads HP, CS, OD and AR before v13 with byte.Parse: an integer in
// [0, 255]. It clamps all of them to [0, 10] as it reads them, but a v13+
// circle size to the key range in mania. Lazer clamps CS after every section,
// with the final mode; see parse_document.
template <Client C, bool CircleSize = false>
std::optional<f64> parse_difficulty_rating(const BeatmapHeader& header,
                                           std::string_view     input) {
  std::optional<f64> value;
  if (C == Client::Stable && header.format_version < 13) {
    if (const auto byte = parse_field_integer(input);
        byte && *byte >= 0 && *byte <= 255)
      value = *byte;
  } else {
    value = parse_field_float(input);
  }
  if (!value || (C == Client::Lazer && CircleSize))
    return value;
  if (C == Client::Stable && CircleSize && header.mode == 3 &&
      header.format_version >= 13)
    return std::clamp(*value, 1.0, 18.0);
  return std::clamp(*value, 0.0, 10.0);
}

template <Client C, auto Member, bool CircleSize = false>
bool assign_difficulty_rating(BeatmapHeader& header, std::string_view input) {
  const auto value = parse_difficulty_rating<C, CircleSize>(header, input);
  if (!value)
    return false;
  header.*Member = *value;
  return true;
}

inline std::optional<f64> parse_slider_multiplier(std::string_view input) {
  if (const auto value = parse_field_double(input))
    return std::clamp(*value, 0.4, 3.6);
  return std::nullopt;
}

inline std::optional<f64> parse_slider_tick_rate(std::string_view input) {
  if (const auto value = parse_field_double(input))
    return std::clamp(*value, 0.5, 8.0);
  return std::nullopt;
}

template <Client C>
inline constexpr auto kDifficultyFields = make_string_lookup<FieldParser>({
    {"HPDrainRate", assign_difficulty_rating<C, &BeatmapHeader::hp>},
    {"CircleSize", assign_difficulty_rating<C, &BeatmapHeader::cs, true>},
    {"OverallDifficulty", assign_difficulty_rating<C, &BeatmapHeader::od>},
    {"SliderMultiplier", assign_field_value<&BeatmapHeader::slider_multiplier,
                                            parse_slider_multiplier>},
    {"SliderTickRate", assign_field_value<&BeatmapHeader::slider_tick_rate,
                                          parse_slider_tick_rate>},
});

template <Client C>
const char* parse_difficulty_section(Beatmap&            beatmap,
                                     std::optional<f64>& approach_rate,
                                     const char*         p,
                                     const char*         end) {
  return for_each_section_line<C>(p, end, [&](std::string_view line) {
    const auto field = split_key_value(line);
    if (!field)
      return;
    if (field->key == "ApproachRate") {
      // parse_document supplies the final OD if no valid AR was specified.
      if (const auto value = parse_difficulty_rating<C>(beatmap, field->value))
        approach_rate = *value;
      else
        ++beatmap.stats.malformed_lines;
    } else {
      parse_key_value(beatmap, kDifficultyFields<C>, *field);
    }
  });
}

}  // namespace fosu::internal
