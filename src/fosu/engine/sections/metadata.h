#pragma once

#include <fosu/engine/parsing/key_value.h>

namespace fosu::internal {

inline bool parse_metadata_field(BeatmapHeader& header, const KeyValue& field) {
  switch (string_hash(field.key)) {
    case "TitleUnicode"_hash:
      if (field.key == "TitleUnicode")
        return assign_field_text<&BeatmapHeader::title_unicode>(header, field.value);
      break;
    case "Title"_hash:
      if (field.key == "Title")
        return assign_field_text<&BeatmapHeader::title>(header, field.value);
      break;
    case "ArtistUnicode"_hash:
      if (field.key == "ArtistUnicode")
        return assign_field_text<&BeatmapHeader::artist_unicode>(header, field.value);
      break;
    case "Artist"_hash:
      if (field.key == "Artist")
        return assign_field_text<&BeatmapHeader::artist>(header, field.value);
      break;
    case "Creator"_hash:
      if (field.key == "Creator")
        return assign_field_text<&BeatmapHeader::creator>(header, field.value);
      break;
    case "Version"_hash:
      if (field.key == "Version")
        return assign_field_text<&BeatmapHeader::version>(header, field.value);
      break;
    case "Source"_hash:
      if (field.key == "Source")
        return assign_field_text<&BeatmapHeader::source>(header, field.value);
      break;
    case "Tags"_hash:
      if (field.key == "Tags")
        return assign_field_text<&BeatmapHeader::tags>(header, field.value);
      break;
    case "BeatmapSetID"_hash:
      if (field.key == "BeatmapSetID")
        return assign_field_value<&BeatmapHeader::beatmap_set_id, parse_field_integer>(
            header, field.value);
      break;
    case "BeatmapID"_hash:
      if (field.key == "BeatmapID")
        return assign_field_value<&BeatmapHeader::beatmap_id, parse_field_integer>(
            header, field.value);
      break;
  }
  return true;
}

inline const char* parse_metadata_section(Beatmap& beatmap,
                                          const char* p,
                                          const char* end) {
  return parse_key_value_section<parse_metadata_field>(beatmap, p, end);
}

}  // namespace fosu::internal
