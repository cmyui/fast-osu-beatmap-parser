#pragma once

#include <fosu/engine/parsing/key_value.h>

namespace fosu::internal {

inline constexpr auto kMetadataFields = make_string_lookup<FieldParser>({
    {"TitleUnicode", assign_field_text<&BeatmapHeader::title_unicode>},
    {"Title", assign_field_text<&BeatmapHeader::title>},
    {"ArtistUnicode", assign_field_text<&BeatmapHeader::artist_unicode>},
    {"Artist", assign_field_text<&BeatmapHeader::artist>},
    {"Creator", assign_field_text<&BeatmapHeader::creator>},
    {"Version", assign_field_text<&BeatmapHeader::version>},
    {"Source", assign_field_text<&BeatmapHeader::source>},
    {"Tags", assign_field_text<&BeatmapHeader::tags>},
    {"BeatmapSetID",
     assign_field_value<&BeatmapHeader::beatmap_set_id, parse_field_integer>},
    {"BeatmapID", assign_field_value<&BeatmapHeader::beatmap_id, parse_field_integer>},
});

inline const char* parse_metadata_section(Beatmap& beatmap,
                                          const char* p,
                                          const char* end) {
  return parse_key_value_section<kMetadataFields>(beatmap, p, end);
}

}  // namespace fosu::internal
