#pragma once

#include <type_traits>
#include "../beatmap_header.hpp"
#include "../string_lookup.hpp"
#include "byte_scan.hpp"
#include "scalar_parse.hpp"

namespace fosu::internal {

static_assert(std::is_standard_layout_v<BeatmapHeader>);

enum class KT : uint8_t { Str, I32, F32, F64, Bool, I64, Mode, Countdown, RawBool, SampleSet };
struct KvEntry {
  KT type;
  uint16_t off;  // offset within the BeatmapHeader subobject
};
#define KV(k, t, field)                                                      \
  StringEntry<KvEntry> {                                                     \
    k, {                                                                     \
      KT::t, static_cast<uint16_t>(__builtin_offsetof(BeatmapHeader, field)) \
    }                                                                        \
  }
inline constexpr auto kGeneral = make_string_lookup<KvEntry>({
    KV("AudioFilename", Str, audio_filename),
    KV("AudioLeadIn", I32, audio_lead_in),
    KV("PreviewTime", I32, preview_time),
    KV("CountdownOffset", I32, countdown_offset),
    KV("Countdown", Countdown, countdown),
    KV("SampleSet", SampleSet, sample_set),
    KV("SamplesMatchPlaybackRate", Bool, samples_match_playback_rate),
    KV("StackLeniency", F32, stack_leniency),
    KV("Mode", Mode, mode),
    KV("LetterboxInBreaks", Bool, letterbox_in_breaks),
    KV("WidescreenStoryboard", Bool, widescreen_storyboard),
    KV("EpilepsyWarning", Bool, epilepsy_warning),
    KV("SpecialStyle", Bool, special_style),
    KV("UseSkinSprites", RawBool, use_skin_sprites),
    KV("OverlayPosition", Str, overlay_position),
    KV("SkinPreference", Str, skin_preference),
});
inline constexpr auto kEditor = make_string_lookup<KvEntry>({
    KV("Bookmarks", Str, bookmarks),
    KV("DistanceSpacing", F64, distance_spacing),
    KV("BeatDivisor", I32, beat_divisor),
    KV("GridSize", I32, grid_size),
    KV("TimelineZoom", F64, timeline_zoom),
});
inline constexpr auto kMetadata = make_string_lookup<KvEntry>({
    KV("TitleUnicode", Str, title_unicode),
    KV("Title", Str, title),
    KV("ArtistUnicode", Str, artist_unicode),
    KV("Artist", Str, artist),
    KV("Creator", Str, creator),
    KV("Version", Str, version),
    KV("Source", Str, source),
    KV("Tags", Str, tags),
    KV("BeatmapSetID", I64, beatmap_set_id),
    KV("BeatmapID", I64, beatmap_id),
});
inline constexpr auto kDifficulty = make_string_lookup<KvEntry>({
    KV("HPDrainRate", F32, hp),
    KV("CircleSize", F32, cs),
    KV("OverallDifficulty", F32, od),
    KV("ApproachRate", F32, ar),
    KV("SliderMultiplier", F64, slider_multiplier),
    KV("SliderTickRate", F64, slider_tick_rate),
});
#undef KV


// Enum.Parse accepts named constants (including comma-separated combinations)
// and the full underlying int32 range, unlike Parsing.ParseInt's symmetric bound.
inline bool parse_legacy_enum(std::string_view value,
                              const StringLookup<int32_t, 4>& names,
                              int32_t& out) {
  const char* p = skip_numeric_space(value.data(), value.data() + value.size());
  const char* end = value.data() + value.size();
  while (end > p && skip_numeric_space(end - 1, end) == end)
    --end;
  int64_t number;
  const char* q = parse_i64(p, end, number);
  if (q != p && q == end && number >= INT32_MIN && number <= INT32_MAX) {
    out = static_cast<int32_t>(number);
    return true;
  }
  out = 0;
  do {
    const auto* comma = find_byte<','>(p, end);
    const char* part_end = comma;
    while (part_end > p && skip_numeric_space(part_end - 1, part_end) == part_end)
      --part_end;
    const auto* named_value = names.find({p, static_cast<size_t>(part_end - p)});
    if (!named_value)
      return false;
    out |= *named_value;
    if (comma == end)
      return true;
    p = skip_numeric_space(comma + 1, end);
  } while (p < end);
  return false;
}

// Returns true exactly when an ApproachRate line was consumed.
// Floating fallback is selected at compile time by the caller.
template <auto ParseDouble, size_t N>
inline bool parse_kv_line(BeatmapHeader& bm,
                          const StringLookup<KvEntry, N>& table,
                          const char* p,
                          size_t len,
                          uint32_t* malformed = nullptr) {
  const auto* colon = find_byte<':'>(p, p + len);
  if (colon == p + len)
    return false;
  size_t key_len = static_cast<size_t>(colon - p);
  while (key_len && (p[key_len - 1] == ' ' || p[key_len - 1] == '\t'))
    --key_len;
  const auto* entry = table.find({p, key_len});
  if (!entry)
    return false;
  const KvEntry& e = *entry;
  size_t off = static_cast<size_t>(colon - p);
  ++off;
  if (off < len && p[off] == ' ')
    ++off;
  const std::string_view v(p + off, len - off);
  auto invalid = [&] {
    if (malformed)
      ++*malformed;
    return false;
  };
  auto complete = [&](const char* q) {
    if (q == v.data())
      return false;
    const char* end = v.data() + v.size();
    while (q < end && (*q == ' ' || *q == '\t'))
      ++q;
    return q == end;
  };
  char* f = reinterpret_cast<char*>(&bm) + e.off;
  switch (e.type) {
    case KT::Str:
      *reinterpret_cast<std::string_view*>(f) = v;
      break;
    case KT::Countdown:
    case KT::SampleSet: {
      static constexpr auto countdown = make_string_lookup<int32_t>({
          {"None", 0},
          {"Normal", 1},
          {"HalfSpeed", 2},
          {"DoubleSpeed", 3},
      });
      static constexpr auto samples = make_string_lookup<int32_t>({
          {"None", 0},
          {"Normal", 1},
          {"Soft", 2},
          {"Drum", 3},
      });
      int32_t value;
      if (!parse_legacy_enum(v, e.type == KT::Countdown ? countdown : samples, value))
        return invalid();
      if (e.type == KT::Countdown)
        *reinterpret_cast<int32_t*>(f) = value;
      else
        *reinterpret_cast<std::string_view*>(f) = v;
      break;
    }
    case KT::I32:
    case KT::Mode: {
      int64_t value;
      const char* q = parse_osu_int(v.data(), v.data() + v.size(), value);
      if (!complete(q) || (e.type == KT::Mode && (value < 0 || value > 3)))
        return invalid();
      *reinterpret_cast<int32_t*>(f) = static_cast<int32_t>(value);
      break;
    }
    case KT::F32:
    case KT::F64: {
      double value;
      const char* q = ParseDouble(v.data(), v.data() + v.size(), value);
      if (!complete(q))
        return invalid();
      if (e.type == KT::F32) {
        // Preserve the raw double, but test the official float domain.
        // Only large boundary values need a second conversion.
        if (value < -2147483520.0 || value > 2147483520.0) {
          float checked;
          if (!complete(parse_osu_float(v.data(), v.data() + v.size(), checked)))
            return invalid();
        }
      } else if (value < -INT32_MAX || value > INT32_MAX)
        return invalid();
      *reinterpret_cast<double*>(f) = value;
      break;
    }
    case KT::Bool: {
      int64_t value;
      if (!complete(parse_osu_int(v.data(), v.data() + v.size(), value)))
        return invalid();
      *reinterpret_cast<bool*>(f) = value == 1;
      break;
    }
    case KT::RawBool:
      *reinterpret_cast<bool*>(f) = !v.empty() && v[0] == '1';
      break;
    case KT::I64: {
      int64_t id;
      if (!complete(parse_osu_int(v.data(), v.data() + v.size(), id)))
        return invalid();
      *reinterpret_cast<int64_t*>(f) = id;
      break;
    }
  }
  return e.off == __builtin_offsetof(BeatmapHeader, ar);
}

}  // namespace fosu::internal
