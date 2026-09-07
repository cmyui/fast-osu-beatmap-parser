#pragma once

#include <type_traits>
#include "../header.hpp"
#include "../scalar_parse.hpp"

namespace fosu::detail {

inline constexpr uint32_t key4(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

static_assert(std::is_standard_layout_v<BeatmapHeader>);

enum class KT : uint8_t { Str, I32, F32, F64, Bool, I64, Mode, Countdown, RawBool, SampleSet };
struct KvEntry {
    const char* name;
    uint32_t key;      // first four bytes of the key
    uint8_t key_len;
    KT type;
    uint16_t off;      // offset within the BeatmapHeader subobject
};
#define KV(k, t, field) \
    KvEntry{k, key4(k[0], k[1], k[2], k[3]), sizeof(k) - 1, KT::t, static_cast<uint16_t>(__builtin_offsetof(BeatmapHeader, field))}
constexpr KvEntry kGeneral[] = {
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
};
constexpr KvEntry kEditor[] = {
    KV("Bookmarks", Str, bookmarks),
    KV("DistanceSpacing", F64, distance_spacing),
    KV("BeatDivisor", I32, beat_divisor),
    KV("GridSize", I32, grid_size),
    KV("TimelineZoom", F64, timeline_zoom),
};
constexpr KvEntry kMetadata[] = {
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
};
constexpr KvEntry kDifficulty[] = {
    KV("HPDrainRate", F32, hp),
    KV("CircleSize", F32, cs),
    KV("OverallDifficulty", F32, od),
    KV("ApproachRate", F32, ar),
    KV("SliderMultiplier", F64, slider_multiplier),
    KV("SliderTickRate", F64, slider_tick_rate),
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

// Returns true exactly when an ApproachRate line was consumed.
// Floating fallback is selected at compile time by the caller.
template <auto ParseDouble, size_t N>
inline bool parse_kv_line(BeatmapHeader& bm, const KvEntry (&table)[N],
                          const char* p, size_t len, uint32_t* malformed = nullptr) {
    const uint32_t key = load_u32_le(p);
    for (size_t i = 0; i < N; ++i) {
        const KvEntry& e = table[i];
        if (e.key != key) continue;
        if (len <= e.key_len || memcmp(p, e.name, e.key_len) != 0) continue;
        size_t off = e.key_len;
        while (off < len && (p[off] == ' ' || p[off] == '\t')) ++off;
        if (off == len || p[off] != ':') continue;
        ++off;
        if (off < len && p[off] == ' ') ++off;
        const std::string_view v(p + off, len - off);
        auto invalid = [&] { if (malformed) ++*malformed; return false; };
        auto complete = [&](const char* q) {
            if (q == v.data()) return false;
            const char* end = v.data() + v.size();
            while (q < end && (*q == ' ' || *q == '\t')) ++q;
            return q == end;
        };
        char* f = reinterpret_cast<char*>(&bm) + e.off;
        switch (e.type) {
            case KT::Str: *reinterpret_cast<std::string_view*>(f) = v; break;
            case KT::Countdown:
            case KT::SampleSet: {
                constexpr std::string_view countdown[] = {"None", "Normal", "HalfSpeed", "DoubleSpeed"};
                constexpr std::string_view samples[] = {"None", "Normal", "Soft", "Drum"};
                int32_t value;
                if (!parse_legacy_enum(v, e.type == KT::Countdown ? countdown : samples, value)) return invalid();
                if (e.type == KT::Countdown) *reinterpret_cast<int32_t*>(f) = value;
                else *reinterpret_cast<std::string_view*>(f) = v;
                break;
            }
            case KT::I32:
            case KT::Mode: {
                int64_t value;
                const char* q = parse_osu_int(v.data(), v.data() + v.size(), value);
                if (!complete(q) || (e.type == KT::Mode && (value < 0 || value > 3))) return invalid();
                *reinterpret_cast<int32_t*>(f) = static_cast<int32_t>(value);
                break;
            }
            case KT::F32:
            case KT::F64: {
                double value;
                const char* q = ParseDouble(v.data(), v.data() + v.size(), value);
                if (!complete(q)) return invalid();
                if (e.type == KT::F32) {
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
            case KT::Bool: {
                int64_t value;
                if (!complete(parse_osu_int(v.data(), v.data() + v.size(), value))) return invalid();
                *reinterpret_cast<bool*>(f) = value == 1;
                break;
            }
            case KT::RawBool: *reinterpret_cast<bool*>(f) = !v.empty() && v[0] == '1'; break;
            case KT::I64: {
                int64_t id;
                if (!complete(parse_osu_int(v.data(), v.data() + v.size(), id))) return invalid();
                *reinterpret_cast<int64_t*>(f) = id;
                break;
            }
        }
        return e.off == __builtin_offsetof(BeatmapHeader, ar);
    }
    return false;
}

}  // namespace fosu::detail
