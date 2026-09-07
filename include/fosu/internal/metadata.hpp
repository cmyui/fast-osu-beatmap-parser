#pragma once

#include <type_traits>
#include "beatmap_header.hpp"
#include "scalar_parse.hpp"

namespace fosu::internal {

inline constexpr uint32_t key4(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

static_assert(std::is_standard_layout_v<BeatmapHeader>);

// Acceptance rules are separate from storage types: both floating-point
// categories retain a double, even when the decoder validates as float32.
enum class MetadataKind : uint8_t {
    Text, Int32, Float32Domain, Float64Domain, IntegerBoolean, Int64,
    Mode, Countdown, LeadingOneBoolean, SampleSet,
};

template <MetadataKind Kind, typename T>
concept MetadataStorage =
    ((Kind == MetadataKind::Text || Kind == MetadataKind::SampleSet) &&
     std::is_same_v<T, std::string_view>) ||
    ((Kind == MetadataKind::Int32 || Kind == MetadataKind::Mode ||
      Kind == MetadataKind::Countdown) && std::is_same_v<T, int32_t>) ||
    ((Kind == MetadataKind::Float32Domain || Kind == MetadataKind::Float64Domain) &&
     std::is_same_v<T, double>) ||
    ((Kind == MetadataKind::IntegerBoolean || Kind == MetadataKind::LeadingOneBoolean) &&
     std::is_same_v<T, bool>) ||
    (Kind == MetadataKind::Int64 && std::is_same_v<T, int64_t>);

struct MetadataField {
    const char* name;
    uint32_t key_prefix;  // first four bytes of the key
    uint8_t key_length;
    MetadataKind kind;
    uint16_t offset;      // offset within the BeatmapHeader subobject
};

// Keep the compact offset table, but reject incompatible field declarations
// while compiling it. The macro derives the member type and offset together.
template <MetadataKind Kind, typename T, size_t Offset, size_t N>
    requires MetadataStorage<Kind, T>
consteval MetadataField metadata_field(const char (&name)[N]) {
    static_assert(N >= 5 && N - 1 <= UINT8_MAX);
    static_assert(Offset <= UINT16_MAX);
    return {name, key4(name[0], name[1], name[2], name[3]),
            N - 1, Kind, static_cast<uint16_t>(Offset)};
}
#define KV(k, kind, field) \
    metadata_field<MetadataKind::kind, decltype(BeatmapHeader::field), \
                   __builtin_offsetof(BeatmapHeader, field)>(k)
constexpr MetadataField kGeneral[] = {
    KV("AudioFilename", Text, audio_filename),
    KV("AudioLeadIn", Int32, audio_lead_in),
    KV("PreviewTime", Int32, preview_time),
    KV("CountdownOffset", Int32, countdown_offset),
    KV("Countdown", Countdown, countdown),
    KV("SampleSet", SampleSet, sample_set),
    KV("SamplesMatchPlaybackRate", IntegerBoolean, samples_match_playback_rate),
    KV("StackLeniency", Float32Domain, stack_leniency),
    KV("Mode", Mode, mode),
    KV("LetterboxInBreaks", IntegerBoolean, letterbox_in_breaks),
    KV("WidescreenStoryboard", IntegerBoolean, widescreen_storyboard),
    KV("EpilepsyWarning", IntegerBoolean, epilepsy_warning),
    KV("SpecialStyle", IntegerBoolean, special_style),
    KV("UseSkinSprites", LeadingOneBoolean, use_skin_sprites),
    KV("OverlayPosition", Text, overlay_position),
    KV("SkinPreference", Text, skin_preference),
};
constexpr MetadataField kEditor[] = {
    KV("Bookmarks", Text, bookmarks),
    KV("DistanceSpacing", Float64Domain, distance_spacing),
    KV("BeatDivisor", Int32, beat_divisor),
    KV("GridSize", Int32, grid_size),
    KV("TimelineZoom", Float64Domain, timeline_zoom),
};
constexpr MetadataField kMetadata[] = {
    KV("TitleUnicode", Text, title_unicode),
    KV("Title", Text, title),
    KV("ArtistUnicode", Text, artist_unicode),
    KV("Artist", Text, artist),
    KV("Creator", Text, creator),
    KV("Version", Text, version),
    KV("Source", Text, source),
    KV("Tags", Text, tags),
    KV("BeatmapSetID", Int64, beatmap_set_id),
    KV("BeatmapID", Int64, beatmap_id),
};
constexpr MetadataField kDifficulty[] = {
    KV("HPDrainRate", Float32Domain, hp),
    KV("CircleSize", Float32Domain, cs),
    KV("OverallDifficulty", Float32Domain, od),
    KV("ApproachRate", Float32Domain, ar),
    KV("SliderMultiplier", Float64Domain, slider_multiplier),
    KV("SliderTickRate", Float64Domain, slider_tick_rate),
};
#undef KV


// Enum.Parse accepts named constants (including comma-separated combinations)
// and the full underlying int32 range, unlike Parsing.ParseInt's symmetric bound.
inline bool parse_legacy_enum(std::string_view value, const std::string_view (&names)[4], int32_t& out) {
    const char* p = skip_numeric_space(value.data(), value.data() + value.size());
    const char* end = value.data() + value.size();
    while (end > p && skip_numeric_space(end - 1, end) == end) --end;
    int64_t number;
    const char* q = parse_i64(p, end, number);
    if (q != p && q == end && number >= INT32_MIN && number <= INT32_MAX) {
        out = static_cast<int32_t>(number);
        return true;
    }
    out = 0;
    do {
        const auto* comma = static_cast<const char*>(memchr(p, ',', end - p));
        const char* part_end = comma ? comma : end;
        while (part_end > p && skip_numeric_space(part_end - 1, part_end) == part_end) --part_end;
        bool found = false;
        for (int i = 0; i < 4; ++i) if (std::string_view(p, part_end - p) == names[i]) { out |= i; found = true; }
        if (!found) return false;
        if (!comma) return true;
        p = skip_numeric_space(comma + 1, end);
    } while (p < end);
    return false;
}

// Identifies the member actually assigned, without giving the generic parser
// knowledge of difficulty defaults. An ignored/rejected line assigns nothing.
struct FieldAssignment {
    const void* destination = nullptr;

    template <typename T>
    bool assigned_to(const T& field) const { return destination == &field; }
};

// Floating fallback is selected at compile time by the caller.
template <auto ParseDouble, size_t N>
inline FieldAssignment parse_kv_line(
    BeatmapHeader& bm, const MetadataField (&table)[N], const char* p, size_t len,
    uint32_t* malformed = nullptr) {
    const uint32_t key = load_u32_le(p);
    for (size_t i = 0; i < N; ++i) {
        const MetadataField& e = table[i];
        if (e.key_prefix != key) continue;
        if (len <= e.key_length || memcmp(p, e.name, e.key_length) != 0) continue;
        size_t off = e.key_length;
        while (off < len && (p[off] == ' ' || p[off] == '\t')) ++off;
        if (off == len || p[off] != ':') continue;
        ++off;
        if (off < len && p[off] == ' ') ++off;
        const std::string_view v(p + off, len - off);
        auto invalid = [&] { if (malformed) ++*malformed; return FieldAssignment{}; };
        auto complete = [&](const char* q) {
            if (q == v.data()) return false;
            const char* end = v.data() + v.size();
            while (q < end && (*q == ' ' || *q == '\t')) ++q;
            return q == end;
        };
        char* f = reinterpret_cast<char*>(&bm) + e.offset;
        switch (e.kind) {
            case MetadataKind::Text: *reinterpret_cast<std::string_view*>(f) = v; break;
            case MetadataKind::Countdown:
            case MetadataKind::SampleSet: {
                constexpr std::string_view countdown[] = {"None", "Normal", "HalfSpeed", "DoubleSpeed"};
                constexpr std::string_view samples[] = {"None", "Normal", "Soft", "Drum"};
                int32_t value;
                if (!parse_legacy_enum(v, e.kind == MetadataKind::Countdown ? countdown : samples, value)) return invalid();
                if (e.kind == MetadataKind::Countdown) *reinterpret_cast<int32_t*>(f) = value;
                else *reinterpret_cast<std::string_view*>(f) = v;
                break;
            }
            case MetadataKind::Int32:
            case MetadataKind::Mode: {
                int64_t value;
                const char* q = parse_osu_int(v.data(), v.data() + v.size(), value);
                if (!complete(q) || (e.kind == MetadataKind::Mode && (value < 0 || value > 3))) return invalid();
                *reinterpret_cast<int32_t*>(f) = static_cast<int32_t>(value);
                break;
            }
            case MetadataKind::Float32Domain:
            case MetadataKind::Float64Domain: {
                double value;
                const char* q = ParseDouble(v.data(), v.data() + v.size(), value);
                if (!complete(q)) return invalid();
                if (e.kind == MetadataKind::Float32Domain) {
                    // Preserve the raw double, but test the official float domain.
                    // Only large boundary values need a second conversion.
                    if (value < -2147483520.0 || value > 2147483520.0) {
                        float checked;
                        if (!complete(parse_osu_float(v.data(), v.data() + v.size(), checked))) return invalid();
                    }
                } else if (value < -INT32_MAX || value > INT32_MAX) return invalid();
                *reinterpret_cast<double*>(f) = value;
                break;
            }
            case MetadataKind::IntegerBoolean: {
                int64_t value;
                if (!complete(parse_osu_int(v.data(), v.data() + v.size(), value))) return invalid();
                *reinterpret_cast<bool*>(f) = value == 1;
                break;
            }
            case MetadataKind::LeadingOneBoolean: *reinterpret_cast<bool*>(f) = !v.empty() && v[0] == '1'; break;
            case MetadataKind::Int64: {
                int64_t id;
                if (!complete(parse_osu_int(v.data(), v.data() + v.size(), id))) return invalid();
                *reinterpret_cast<int64_t*>(f) = id;
                break;
            }
        }
        return {f};
    }
    return {};
}

}  // namespace fosu::internal
