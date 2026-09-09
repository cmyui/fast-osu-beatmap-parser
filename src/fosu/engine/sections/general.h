#pragma once

#include <fosu/engine/parsing/key_value.h>
#include <fosu/engine/parsing/sample_sets.h>

namespace fosu::internal {

inline std::optional<int32_t> parse_mode(std::string_view input) {
  const auto value = parse_field_integer(input);
  if (!value || *value < 0 || *value > 3)
    return std::nullopt;
  return value;
}

// Legacy Enum.Parse accepts names, comma-separated combinations, and the full
// int32 range. Individual domains can impose stricter rules on that spelling.
inline std::optional<int32_t> parse_legacy_enum(std::string_view input,
                                                const StringLookup<int32_t, 4>& names) {
  const char* p = skip_numeric_space(input.data(), input.data() + input.size());
  const char* end = input.data() + input.size();
  while (end > p && skip_numeric_space(end - 1, end) == end)
    --end;
  int64_t number;
  const char* q = parse_i64(p, end, number);
  if (q != p && q == end && number >= INT32_MIN && number <= INT32_MAX)
    return static_cast<int32_t>(number);
  int32_t value = 0;
  do {
    const auto* comma = find_byte<','>(p, end);
    const char* part_end = comma;
    while (part_end > p && skip_numeric_space(part_end - 1, part_end) == part_end)
      --part_end;
    const auto* named_value = names.find({p, static_cast<size_t>(part_end - p)});
    if (!named_value)
      return std::nullopt;
    value |= *named_value;
    if (comma == end)
      return value;
    p = skip_numeric_space(comma + 1, end);
  } while (p < end);
  return std::nullopt;
}

inline std::optional<int32_t> parse_countdown(std::string_view input) {
  static constexpr auto names = make_string_lookup<int32_t>({
      {"None", 0},
      {"Normal", 1},
      {"HalfSpeed", 2},
      {"DoubleSpeed", 3},
  });
  return parse_legacy_enum(input, names);
}

inline std::optional<SampleSet> parse_field_sample_set(std::string_view input) {
  static constexpr auto names = make_string_lookup<int32_t>({
      {"None", 0},
      {"Normal", 1},
      {"Soft", 2},
      {"Drum", 3},
  });
  // Sample sets are choices, not flags.
  if (input.find(',') != std::string_view::npos)
    return std::nullopt;
  const auto value = parse_legacy_enum(input, names);
  return value ? parse_sample_set(*value) : std::nullopt;
}

inline bool parse_skin_sprites(BeatmapHeader& header, std::string_view input) {
  header.use_skin_sprites = !input.empty() && input.front() == '1';
  return true;
}

inline bool parse_preview_time(BeatmapHeader& header, std::string_view input) {
  const auto time = parse_field_integer(input);
  if (!time)
    return false;
  const int offset = header.format_version < 5 && *time != -1 ? 24 : 0;
  header.preview_time = static_cast<int32_t>(static_cast<uint32_t>(*time) + offset);
  return true;
}

inline bool parse_general_field(BeatmapHeader& header, const KeyValue& field) {
  switch (string_hash(field.key)) {
    case "AudioFilename"_hash:
      if (field.key == "AudioFilename")
        return assign_field_text<&BeatmapHeader::audio_filename>(header, field.value);
      break;
    case "AudioLeadIn"_hash:
      if (field.key == "AudioLeadIn")
        return assign_field_value<&BeatmapHeader::audio_lead_in, parse_field_integer>(
            header, field.value);
      break;
    case "PreviewTime"_hash:
      if (field.key == "PreviewTime")
        return parse_preview_time(header, field.value);
      break;
    case "CountdownOffset"_hash:
      if (field.key == "CountdownOffset")
        return assign_field_value<&BeatmapHeader::countdown_offset, parse_field_integer>(
            header, field.value);
      break;
    case "Countdown"_hash:
      if (field.key == "Countdown")
        return assign_field_value<&BeatmapHeader::countdown, parse_countdown>(
            header, field.value);
      break;
    case "SampleSet"_hash:
      if (field.key == "SampleSet")
        return assign_field_value<&BeatmapHeader::sample_set, parse_field_sample_set>(
            header, field.value);
      break;
    case "SamplesMatchPlaybackRate"_hash:
      if (field.key == "SamplesMatchPlaybackRate")
        return assign_field_value<&BeatmapHeader::samples_match_playback_rate,
                                  parse_field_boolean>(header, field.value);
      break;
    case "StackLeniency"_hash:
      if (field.key == "StackLeniency")
        return assign_field_value<&BeatmapHeader::stack_leniency, parse_field_float>(
            header, field.value);
      break;
    case "Mode"_hash:
      if (field.key == "Mode")
        return assign_field_value<&BeatmapHeader::mode, parse_mode>(header, field.value);
      break;
    case "LetterboxInBreaks"_hash:
      if (field.key == "LetterboxInBreaks")
        return assign_field_value<&BeatmapHeader::letterbox_in_breaks,
                                  parse_field_boolean>(header, field.value);
      break;
    case "WidescreenStoryboard"_hash:
      if (field.key == "WidescreenStoryboard")
        return assign_field_value<&BeatmapHeader::widescreen_storyboard,
                                  parse_field_boolean>(header, field.value);
      break;
    case "EpilepsyWarning"_hash:
      if (field.key == "EpilepsyWarning")
        return assign_field_value<&BeatmapHeader::epilepsy_warning, parse_field_boolean>(
            header, field.value);
      break;
    case "SpecialStyle"_hash:
      if (field.key == "SpecialStyle")
        return assign_field_value<&BeatmapHeader::special_style, parse_field_boolean>(
            header, field.value);
      break;
    case "UseSkinSprites"_hash:
      if (field.key == "UseSkinSprites")
        return parse_skin_sprites(header, field.value);
      break;
    case "OverlayPosition"_hash:
      if (field.key == "OverlayPosition")
        return assign_field_text<&BeatmapHeader::overlay_position>(header, field.value);
      break;
    case "SkinPreference"_hash:
      if (field.key == "SkinPreference")
        return assign_field_text<&BeatmapHeader::skin_preference>(header, field.value);
      break;
  }
  return true;
}

inline const char* parse_general_section(Beatmap& beatmap,
                                         const char* p,
                                         const char* end) {
  return parse_key_value_section<parse_general_field>(beatmap, p, end);
}

}  // namespace fosu::internal
