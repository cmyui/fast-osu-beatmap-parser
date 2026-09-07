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
    uint32_t key;      // first four bytes of the key
    uint8_t dis_pos;   // 0: no disambiguation; else byte index to test
    char dis_ch;  // expected byte at dis_pos for THIS entry
    uint8_t key_len;
    KT type;
    uint16_t off;      // offset within the BeatmapHeader subobject
    int32_t dflt_i;
    double dflt_f;
};
#define KV(k, pos, ch, len, t, field, di, df) \
    KvEntry{key4(k[0], k[1], k[2], k[3]), pos, ch, len, KT::t, static_cast<uint16_t>(__builtin_offsetof(BeatmapHeader, field)), di, df}
constexpr KvEntry kGeneral[] = {
    KV("Audi", 5, 'F', 13, Str, audio_filename, 0, 0),
    KV("Audi", 5, 'L', 11, I32, audio_lead_in, 0, 0),
    KV("Prev", 0, 0, 11, I32, preview_time, -1, 0),
    KV("Coun", 9, 'O', 15, I32, countdown_offset, 0, 0),
    KV("Coun", 9, 0, 9, I32, countdown, 1, 0),  // dis_ch 0: matches when the other did not
    KV("Samp", 6, 'S', 9, Str, sample_set, 0, 0),
    KV("Samp", 6, 0, 24, Bool, samples_match_playback_rate, 0, 0),
    KV("Stac", 0, 0, 13, F64, stack_leniency, 0, 0.7),
    KV("Mode", 0, 0, 4, I32, mode, 0, 0),
    KV("Lett", 0, 0, 17, Bool, letterbox_in_breaks, 0, 0),
    KV("Wide", 0, 0, 20, Bool, widescreen_storyboard, 0, 0),
    KV("Epil", 0, 0, 15, Bool, epilepsy_warning, 0, 0),
    KV("Spec", 0, 0, 12, Bool, special_style, 0, 0),
    KV("UseS", 0, 0, 14, Bool, use_skin_sprites, 0, 0),
    KV("Over", 0, 0, 15, Str, overlay_position, 0, 0),
    KV("Skin", 0, 0, 14, Str, skin_preference, 0, 0),
};
constexpr KvEntry kEditor[] = {
    KV("Book", 0, 0, 9, Str, bookmarks, 0, 0),
    KV("Dist", 0, 0, 15, F64, distance_spacing, 0, 0),
    KV("Beat", 0, 0, 11, I32, beat_divisor, 4, 0),
    KV("Grid", 0, 0, 8, I32, grid_size, 4, 0),
    KV("Time", 0, 0, 12, F64, timeline_zoom, 0, 1),
};
constexpr KvEntry kMetadata[] = {
    KV("Titl", 5, 'U', 12, Str, title_unicode, 0, 0),
    KV("Titl", 5, 0, 5, Str, title, 0, 0),
    KV("Arti", 6, 'U', 13, Str, artist_unicode, 0, 0),
    KV("Arti", 6, 0, 6, Str, artist, 0, 0),
    KV("Crea", 0, 0, 7, Str, creator, 0, 0),
    KV("Vers", 0, 0, 7, Str, version, 0, 0),
    KV("Sour", 0, 0, 6, Str, source, 0, 0),
    KV("Tags", 0, 0, 4, Str, tags, 0, 0),
    KV("Beat", 7, 'S', 12, I64, beatmap_set_id, 0, 0),
    KV("Beat", 7, 0, 9, I64, beatmap_id, 0, 0),
};
constexpr KvEntry kDifficulty[] = {
    KV("HPDr", 0, 0, 11, F64, hp, 0, 5),
    KV("Circ", 0, 0, 10, F64, cs, 0, 5),
    KV("Over", 0, 0, 17, F64, od, 0, 5),
    KV("Appr", 0, 0, 12, F64, ar, 0, 5),
    KV("Slid", 6, 'M', 16, F64, slider_multiplier, 0, 1.4),
    KV("Slid", 6, 0, 14, F64, slider_tick_rate, 0, 1),
};
#undef KV


// Returns true exactly when an ApproachRate line was consumed.
// Floating fallback is selected at compile time by the caller.
template <auto ParseDouble, size_t N>
inline bool parse_kv_line(BeatmapHeader& bm, const KvEntry (&table)[N],
                          const char* p, size_t len) {
    const uint32_t key = load_u32_le(p);
    for (size_t i = 0; i < N; ++i) {
        const KvEntry& e = table[i];
        if (e.key != key) continue;
        // Like the original switches, disambiguation may read through byte 9;
        // callers provide 128 readable zero bytes beyond the file.
        if (e.dis_pos && e.dis_ch && p[e.dis_pos] != e.dis_ch) continue;
        size_t off = e.key_len + 1;
        if (off < len && p[off] == ' ') ++off;
        const std::string_view v(p + off, len > off ? len - off : 0);
        char* f = reinterpret_cast<char*>(&bm) + e.off;
        switch (e.type) {
            case KT::Str: *reinterpret_cast<std::string_view*>(f) = v; break;
            case KT::I32: {
                int64_t value;
                const char* q = parse_i64(v.data(), v.data() + v.size(), value);
                *reinterpret_cast<int32_t*>(f) = q == v.data() ? e.dflt_i : clamp_i32(value);
                break;
            }
            case KT::F64: {
                double value;
                const char* q = ParseDouble(v.data(), v.data() + v.size(), value);
                *reinterpret_cast<double*>(f) = q == v.data() ? e.dflt_f : value;
                break;
            }
            case KT::Bool: *reinterpret_cast<bool*>(f) = !v.empty() && v[0] == '1'; break;
            case KT::I64: {
                int64_t id;
                if (parse_i64(v.data(), v.data() + v.size(), id) != v.data())
                    *reinterpret_cast<int64_t*>(f) = id;
                break;
            }
        }
        return e.off == __builtin_offsetof(BeatmapHeader, ar);
    }
    return false;
}

}  // namespace fosu::detail
