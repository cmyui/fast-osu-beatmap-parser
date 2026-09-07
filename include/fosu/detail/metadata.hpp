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

enum class KT : uint8_t { Str, I32, F64, Bool, I64 };
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
    KV("Countdown", I32, countdown),
    KV("SampleSet", Str, sample_set),
    KV("SamplesMatchPlaybackRate", Bool, samples_match_playback_rate),
    KV("StackLeniency", F64, stack_leniency),
    KV("Mode", I32, mode),
    KV("LetterboxInBreaks", Bool, letterbox_in_breaks),
    KV("WidescreenStoryboard", Bool, widescreen_storyboard),
    KV("EpilepsyWarning", Bool, epilepsy_warning),
    KV("SpecialStyle", Bool, special_style),
    KV("UseSkinSprites", Bool, use_skin_sprites),
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
    KV("HPDrainRate", F64, hp),
    KV("CircleSize", F64, cs),
    KV("OverallDifficulty", F64, od),
    KV("ApproachRate", F64, ar),
    KV("SliderMultiplier", F64, slider_multiplier),
    KV("SliderTickRate", F64, slider_tick_rate),
};
#undef KV


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
            case KT::I32: {
                int64_t value;
                const char* q = parse_i64(v.data(), v.data() + v.size(), value);
                if (!complete(q)) return invalid();
                *reinterpret_cast<int32_t*>(f) = clamp_i32(value);
                break;
            }
            case KT::F64: {
                double value;
                const char* q = ParseDouble(v.data(), v.data() + v.size(), value);
                if (!complete(q)) return invalid();
                *reinterpret_cast<double*>(f) = value;
                break;
            }
            case KT::Bool: *reinterpret_cast<bool*>(f) = !v.empty() && v[0] == '1'; break;
            case KT::I64: {
                int64_t id;
                if (!complete(parse_i64(v.data(), v.data() + v.size(), id))) return invalid();
                *reinterpret_cast<int64_t*>(f) = id;
                break;
            }
        }
        return e.off == __builtin_offsetof(BeatmapHeader, ar);
    }
    return false;
}

}  // namespace fosu::detail
