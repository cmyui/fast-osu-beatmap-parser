#pragma once

#include <fosu/beatmap.h>
#include <fosu/engine/header_values.h>
#include <fosu/engine/section_lines.h>

namespace fosu::internal {

struct HeaderField {
  std::string_view key;
  std::string_view value;
};

inline std::optional<HeaderField> split_header_field(std::string_view line) {
  const char* p = line.data();
  const char* end = p + line.size();
  const char* colon = find_byte<':'>(p, end);
  if (colon == end)
    return std::nullopt;
  const char* key_end = colon;
  while (key_end > p && (key_end[-1] == ' ' || key_end[-1] == '\t'))
    --key_end;
  const char* value = colon + 1;
  // Only one separating space is removed. String values retain other whitespace.
  if (value < end && *value == ' ')
    ++value;
  return HeaderField{{p, static_cast<size_t>(key_end - p)},
                     {value, static_cast<size_t>(end - value)}};
}

using HeaderFieldParser = bool (*)(BeatmapHeader&, std::string_view);

template <auto Member, auto ParseValue>
inline bool assign_header_value(BeatmapHeader& header, std::string_view input) {
  const auto value = ParseValue(input);
  if (!value)
    return false;
  header.*Member = *value;
  return true;
}

template <auto Member>
inline bool assign_header_text(BeatmapHeader& header, std::string_view input) {
  header.*Member = input;
  return true;
}

inline bool parse_skin_sprites(BeatmapHeader& header, std::string_view input) {
  header.use_skin_sprites = !input.empty() && input.front() == '1';
  return true;
}

inline constexpr auto kGeneralFields = make_string_lookup<HeaderFieldParser>({
    {"AudioFilename", assign_header_text<&BeatmapHeader::audio_filename>},
    {"AudioLeadIn",
     assign_header_value<&BeatmapHeader::audio_lead_in, parse_header_integer>},
    {"PreviewTime",
     assign_header_value<&BeatmapHeader::preview_time, parse_header_integer>},
    {"CountdownOffset",
     assign_header_value<&BeatmapHeader::countdown_offset, parse_header_integer>},
    {"Countdown", assign_header_value<&BeatmapHeader::countdown, parse_countdown>},
    {"SampleSet",
     assign_header_value<&BeatmapHeader::sample_set, parse_header_sample_set>},
    {"SamplesMatchPlaybackRate",
     assign_header_value<&BeatmapHeader::samples_match_playback_rate,
                         parse_header_boolean>},
    {"StackLeniency",
     assign_header_value<&BeatmapHeader::stack_leniency, parse_header_float>},
    {"Mode", assign_header_value<&BeatmapHeader::mode, parse_mode>},
    {"LetterboxInBreaks",
     assign_header_value<&BeatmapHeader::letterbox_in_breaks, parse_header_boolean>},
    {"WidescreenStoryboard",
     assign_header_value<&BeatmapHeader::widescreen_storyboard, parse_header_boolean>},
    {"EpilepsyWarning",
     assign_header_value<&BeatmapHeader::epilepsy_warning, parse_header_boolean>},
    {"SpecialStyle",
     assign_header_value<&BeatmapHeader::special_style, parse_header_boolean>},
    {"UseSkinSprites", parse_skin_sprites},
    {"OverlayPosition", assign_header_text<&BeatmapHeader::overlay_position>},
    {"SkinPreference", assign_header_text<&BeatmapHeader::skin_preference>},
});

inline constexpr auto kEditorFields = make_string_lookup<HeaderFieldParser>({
    {"Bookmarks", assign_header_text<&BeatmapHeader::bookmarks>},
    {"DistanceSpacing",
     assign_header_value<&BeatmapHeader::distance_spacing, parse_header_double>},
    {"BeatDivisor",
     assign_header_value<&BeatmapHeader::beat_divisor, parse_header_integer>},
    {"GridSize", assign_header_value<&BeatmapHeader::grid_size, parse_header_integer>},
    {"TimelineZoom",
     assign_header_value<&BeatmapHeader::timeline_zoom, parse_header_double>},
});

inline constexpr auto kMetadataFields = make_string_lookup<HeaderFieldParser>({
    {"TitleUnicode", assign_header_text<&BeatmapHeader::title_unicode>},
    {"Title", assign_header_text<&BeatmapHeader::title>},
    {"ArtistUnicode", assign_header_text<&BeatmapHeader::artist_unicode>},
    {"Artist", assign_header_text<&BeatmapHeader::artist>},
    {"Creator", assign_header_text<&BeatmapHeader::creator>},
    {"Version", assign_header_text<&BeatmapHeader::version>},
    {"Source", assign_header_text<&BeatmapHeader::source>},
    {"Tags", assign_header_text<&BeatmapHeader::tags>},
    {"BeatmapSetID",
     assign_header_value<&BeatmapHeader::beatmap_set_id, parse_header_integer>},
    {"BeatmapID", assign_header_value<&BeatmapHeader::beatmap_id, parse_header_integer>},
});

inline constexpr auto kDifficultyFields = make_string_lookup<HeaderFieldParser>({
    {"HPDrainRate", assign_header_value<&BeatmapHeader::hp, parse_header_float>},
    {"CircleSize", assign_header_value<&BeatmapHeader::cs, parse_header_float>},
    {"OverallDifficulty", assign_header_value<&BeatmapHeader::od, parse_header_float>},
    {"SliderMultiplier",
     assign_header_value<&BeatmapHeader::slider_multiplier, parse_header_double>},
    {"SliderTickRate",
     assign_header_value<&BeatmapHeader::slider_tick_rate, parse_header_double>},
});

template <size_t N>
inline void parse_header_field(Beatmap& beatmap,
                               const StringLookup<HeaderFieldParser, N>& fields,
                               const HeaderField& field) {
  if (const auto* parse = fields.find(field.key))
    if (!(*parse)(beatmap, field.value))
      ++beatmap.stats.malformed_lines;
}

template <size_t N>
inline const char* parse_header_section(Beatmap& beatmap,
                                        const StringLookup<HeaderFieldParser, N>& fields,
                                        const char* p,
                                        const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    if (const auto field = split_header_field(line))
      parse_header_field(beatmap, fields, *field);
  });
}

inline const char* parse_difficulty_section(Beatmap& beatmap,
                                            std::optional<double>& approach_rate,
                                            const char* p,
                                            const char* end) {
  return for_each_section_line(p, end, [&](std::string_view line) {
    const auto field = split_header_field(line);
    if (!field)
      return;
    if (field->key == "ApproachRate") {
      if (const auto value = parse_header_float(field->value))
        approach_rate = *value;
      else
        ++beatmap.stats.malformed_lines;
    } else {
      parse_header_field(beatmap, kDifficultyFields, *field);
    }
  });
}

}  // namespace fosu::internal
