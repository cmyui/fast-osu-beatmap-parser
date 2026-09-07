#pragma once
#include "beatmap.hpp"
#include "io.hpp"
#include "internal/document_sections.hpp"
#include "internal/events.hpp"
#include "internal/timing_section.hpp"
#include "internal/vector_sink.hpp"
#include "internal/section_names.hpp"

namespace fosu {

// Section-selection bits for ParseOptions::sections. A caller that only
// needs, say, OverallDifficulty can parse just [Difficulty]: unwanted
// sections are skipped with a single memchr jump (no line iteration) and
// parsing stops entirely once every requested section has been consumed
// — [Difficulty] lives in the first ~2KB of a file whose remaining ~98%
// is hit objects, timing and events, so a difficulty-only parse is
// ~20-30x cheaper than a full one.
enum : uint32_t {
    kSectionGeneral = 1u << 1,
    kSectionEditor = 1u << 2,
    kSectionMetadata = 1u << 3,
    kSectionDifficulty = 1u << 4,
    kSectionEvents = 1u << 5,
    kSectionTimingPoints = 1u << 6,
    kSectionColours = 1u << 7,
    kSectionHitObjects = 1u << 8,
    kAllSections = 0xFFFFFFFFu,
};

struct ParseOptions {
    bool use_simd = true;  // false forces the scalar hitobject path (benchmarking)
    uint32_t sections = kAllSections;  // bitmask of kSection*
};

namespace internal {

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

}  // namespace internal

// `data` must be followed by kBufferPadding readable zero bytes (io.hpp).
// String fields of the result view into `data`; keep the buffer alive.
// parse_into clears bm (keeping vector capacity) and fills it; pass the
// same Beatmap across calls to parse many files without allocating.
template <typename Map>
inline void parse_into(const char* data, size_t size, Map& bm,
                       [[maybe_unused]] ParseOptions opts = {}) {
    using namespace internal;
    if (size > kMaxInputSize) throw std::length_error("beatmap input exceeds 64 MiB");
    reset_for_reuse(bm);
    bm.set_input(data);
    if (size == 0) return;
    const char* p = data;
    const char* file_end = data + size;
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
                // /16: the shortest hitobject line observed across 167
                // popular ranked maps is 15 bytes + newline; a smaller
                // divisor only over-reserves (untouched pages are free),
                // while under-reserving costs a full-array growth memmove.
                bm.hit_objects.reserve(
                    bm.hit_objects.size() +
                    static_cast<size_t>(file_end - line_end) / 16 + 1);
                p = parse_hitobjects_section(bm, nl ? nl + 1 : file_end, file_end,
                                             opts.use_simd);
                sec = Section::Unknown;
                continue;
            } else if (sec == Section::TimingPoints) {
#if FOSU_SIMD_X86
                if (opts.use_simd) {
                    p = parse_timing_points_section(
                        bm, nl ? nl + 1 : file_end, file_end);
                    sec = Section::Unknown;
                    continue;
                }
#endif
                bm.timing_points.reserve(256);
            }
#if FOSU_SIMD_X86
            else if (sec == Section::Events && opts.use_simd) {
                p = parse_events_section(bm, nl ? nl + 1 : file_end, file_end);
                sec = Section::Unknown;
                continue;
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
                if (split_kv(p, len, k, v)) parse_colour_kv(bm, k, v);
                break;
            }
            case Section::Events:
                parse_event_line(bm, p, len);
                break;
            case Section::TimingPoints:
                parse_timing_point_line(bm, p, len);
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
}

template <typename Map>
inline void parse_into(const FileBuffer& buf, Map& bm,
                       ParseOptions opts = {}) {
    parse_into(buf.data.get(), buf.size, bm, opts);
}

inline Beatmap parse(const char* data, size_t size, ParseOptions opts = {}) {
    Beatmap bm;
    parse_into(data, size, bm, opts);
    return bm;
}

inline Beatmap parse(const FileBuffer& buf, ParseOptions opts = {}) {
    Beatmap bm;
    parse_into(buf.data.get(), buf.size, bm, opts);
    return bm;
}

}  // namespace fosu
