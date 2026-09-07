#pragma once
#include "../../parsing_engine.hpp"
#include "document_sections.hpp"
#include "events.hpp"
#include "hitobjects.hpp"
#include "section_names.hpp"
#include "timing_section.hpp"

namespace fosu::internal {
static_assert(offsetof(HitObject, x) == 0 && offsetof(HitObject, y) == 4 &&
                  offsetof(HitObject, type) == 8 &&
                  offsetof(HitObject, hitsound) == 12,
              "AVX2 prefix path stores {x,y,type,hitsound} as one vector");

static_assert(kSectionGeneral == 1u << static_cast<int>(Section::General) &&
                  kSectionDifficulty ==
                      1u << static_cast<int>(Section::Difficulty) &&
                  kSectionHitObjects ==
                      1u << static_cast<int>(Section::HitObjects),
              "public section bits mirror the internal Section ordinals");

// `data` must be followed by kBufferPadding readable zero bytes (io.hpp).
// String fields of the result view into `data`.
template <bool UseSimd>
inline void parse_document(
    std::span<const char> input, Beatmap& bm, ParseOptions opts) noexcept {
    if (input.empty()) return;
    const char* p = input.data();
    const char* file_end = p + input.size();
    const size_t size = input.size();
    size_t break_count = 0;
    size_t colour_count = 0;
    size_t timing_point_count = 0;
    size_t hit_object_count = 0;
    size_t slider_count = 0;
    size_t slider_point_count = 0;
    if (size >= 3 && static_cast<uint8_t>(p[0]) == 0xEF &&
        static_cast<uint8_t>(p[1]) == 0xBB && static_cast<uint8_t>(p[2]) == 0xBF)
        p += 3;

    Section sec = Section::None;
    bool ar_specified = false;
    // Wanted sections not yet consumed; once empty, any further unwanted
    // header ends the parse.
    uint32_t pending = opts.sections & 0x1FEu;

    while (p < file_end) {
        // The fused section loops consume [TimingPoints]/[HitObjects] —
        // 95%+ of file bytes — so this loop only walks headers, metadata,
        // and events, where per-line memchr is free. (A 32-byte SIMD line
        // probe here measured exactly zero in the ablation audit: its
        // value was eroded to nothing when the fused sections landed.)
        if (*p == '\r' || *p == '\n') { ++p; continue; }
        const char* nl =
            static_cast<const char*>(memchr(p, '\n', file_end - p));
        const char* line_end = nl ? nl : file_end;
        if (line_end > p && line_end[-1] == '\r') --line_end;
        const size_t len = static_cast<size_t>(line_end - p);

        if (len == 0) goto next_line;
        if (*p == '[') {
            sec = match_section({p, len});
            const uint32_t sec_bit = 1u << static_cast<int>(sec);
            if (!(opts.sections & sec_bit)) {
                if (pending == 0) break;  // everything wanted is done
                const char* start = nl ? nl + 1 : file_end;
                const auto* nb = static_cast<const char*>(memchr(
                    start, '[', static_cast<size_t>(file_end - start)));
                // A bracket inside a value/comment is not a section header.
                while (nb && nb != start && nb[-1] != '\n' && nb[-1] != '\r') {
                    nb = static_cast<const char*>(memchr(
                        nb + 1, '[', static_cast<size_t>(file_end - nb - 1)));
                }
                p = nb ? nb : file_end;
                sec = Section::Unknown;
                continue;
            }
            pending &= ~sec_bit;
            if (sec == Section::HitObjects) {
                p = parse_hitobjects_section<UseSimd>(
                    bm, hit_object_count, slider_count, slider_point_count,
                    nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
            }
#if FOSU_SIMD
            if constexpr (UseSimd) {
                if (sec == Section::TimingPoints) {
                    p = parse_timing_points_section(
                        bm, timing_point_count, nl ? nl + 1 : file_end, file_end);
                    sec = Section::Unknown;
                    continue;
                }
                if (sec == Section::Events) {
                    p = parse_events_section(
                        bm, break_count, nl ? nl + 1 : file_end, file_end);
                    sec = Section::Unknown;
                    continue;
                }
            }
#endif
            goto next_line;
        }
        if (fosu::internal::ignored_line(p, line_end)) goto next_line;

        switch (sec) {
            case Section::None: {
                const std::string_view line{p, len};
                const size_t v = line.find("osu file format v");
                if (v != std::string_view::npos) {
                    int64_t ver;
                    const char* vp = p + v + 17;
                    if (parse_i64(vp, line_end, ver) != vp)
                        bm.format_version = clamp_i32(ver);
                }
                break;
            }
            case Section::General:
                if (len >= 5) parse_general_line(bm, p, len);
                break;
            case Section::Editor:
                if (len >= 5) parse_editor_line(bm, p, len);
                break;
            case Section::Metadata:
                if (len >= 5) parse_metadata_line(bm, p, len);
                break;
            case Section::Difficulty:
                if (len >= 5) parse_difficulty_line(bm, p, len, ar_specified);
                break;
            case Section::Colours: {
                std::string_view k, v;
                if (split_kv(p, len, k, v))
                    parse_colour_kv(bm, colour_count, k, v);
                break;
            }
            case Section::Events:
                parse_event_line(bm, break_count, p, len);
                break;
            case Section::TimingPoints:
                parse_timing_point_line(
                    bm, timing_point_count, p, len);
                break;
            case Section::HitObjects:  // consumed by parse_hitobjects_section
            case Section::Unknown:
                break;
        }

    next_line:
        p = nl ? nl + 1 : file_end;
    }

    // Old format versions omit ApproachRate; it mirrors OverallDifficulty.
    if (!ar_specified) bm.ar = bm.od;
    bm.breaks = bm.breaks.first(break_count);
    bm.combo_colours = bm.combo_colours.first(colour_count);
    bm.timing_points = bm.timing_points.first(timing_point_count);
    bm.hit_objects = bm.hit_objects.first(hit_object_count);
    bm.sliders = bm.sliders.first(slider_count);
    bm.slider_points = bm.slider_points.first(slider_point_count);
}


inline constexpr ParsingEngine scalar_engine{
    EngineKind::Scalar, parse_document<false>,
};

#if FOSU_SIMD_X86
inline constexpr ParsingEngine native_engine{
    EngineKind::Avx2, parse_document<true>,
};
#elif FOSU_SIMD_NEON
inline constexpr ParsingEngine native_engine{
    EngineKind::Neon, parse_document<true>,
};
#else
inline constexpr auto native_engine = scalar_engine;
#endif

}  // namespace fosu::internal
