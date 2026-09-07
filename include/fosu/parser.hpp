#pragma once
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

#include "beatmap.hpp"
#include "io.hpp"
#include "internal/document_sections.hpp"
#include "internal/events.hpp"
#include "internal/hitobjects.hpp"
#include "internal/timing_section.hpp"
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

// Parser construction is common in convenience and C/Python APIs. Retain one
// inactive mapping without ever sharing a live arena between parser instances.
inline std::atomic<Arena*> parser_arena_pool{};

inline Arena* acquire_parser_arena() {
    Arena* arena = parser_arena_pool.exchange(
        nullptr, std::memory_order_acq_rel);
    if (!arena) return arena_alloc();
    arena_clear(arena);
    return arena;
}

inline void recycle_parser_arena(Arena* arena) {
    if (!arena) return;
    arena_clear(arena);
    Arena* empty = nullptr;
    if (!parser_arena_pool.compare_exchange_strong(
            empty, arena, std::memory_order_acq_rel)) {
        arena_release(arena);
    }
}

inline void clear_parser_arena_pool() {
    arena_release(parser_arena_pool.exchange(
        nullptr, std::memory_order_acq_rel));
}

#ifndef FOSU_MANAGED_ARENA_CLEANUP
struct ParserArenaPoolCleanup {
    ~ParserArenaPoolCleanup() { clear_parser_arena_pool(); }
};
inline ParserArenaPoolCleanup parser_arena_pool_cleanup;
#endif

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

struct BeatmapArraySizes {
    size_t breaks = 0;
    size_t colours = 0;
    size_t timing_points = 0;
    size_t hit_objects = 0;
    size_t sliders = 0;
    size_t slider_points = 0;
};

// Derive safe upper bounds from the shortest accepted spelling of each
// record. Virtual arena space is cheap; only pages containing accepted records
// are touched. This avoids a sizing pass over the input.
inline BeatmapArraySizes maximum_beatmap_array_sizes(
    size_t size, uint32_t selected_sections) {
    BeatmapArraySizes sizes;
    if (selected_sections & kSectionEvents)
        sizes.breaks = size / 5 + 1;          // 2,0,0
    if (selected_sections & kSectionColours)
        sizes.colours = size / 11 + 1;        // Combo:0,0,0
    if (selected_sections & kSectionTimingPoints)
        sizes.timing_points = size / 3 + 1;   // 0,0
    if (selected_sections & kSectionHitObjects) {
        sizes.hit_objects = size / 9 + 1;     // 0,0,0,1,0
        sizes.sliders = size / 14 + 1;        // 0,0,0,2,0,L,0
        sizes.slider_points = size / 4 + 1;   // |0:0
    }
    return sizes;
}

template <typename T>
constexpr size_t arena_array_bytes(size_t count) {
    return count ? count * sizeof(T) + alignof(T) - 1 : 0;
}

inline void allocate_beatmap_arrays(
    Arena* arena, Beatmap& beatmap, const BeatmapArraySizes& sizes) {
    const size_t bytes =
        arena_array_bytes<Break>(sizes.breaks) +
        arena_array_bytes<uint32_t>(sizes.colours) +
        arena_array_bytes<TimingPoint>(sizes.timing_points) +
        arena_array_bytes<HitObject>(sizes.hit_objects) +
        arena_array_bytes<Slider>(sizes.sliders) +
        arena_array_bytes<SliderPoint>(sizes.slider_points);
    if (!bytes) return;

    auto* cursor = static_cast<uint8_t*>(
        arena_push(arena, bytes, alignof(std::max_align_t)));
    if (!cursor) throw std::bad_alloc();
    auto take = [&]<typename T>(size_t count) -> std::span<T> {
        if (!count) return {};
        cursor = reinterpret_cast<uint8_t*>(align_up(
            reinterpret_cast<uintptr_t>(cursor), alignof(T)));
        auto* values = reinterpret_cast<T*>(cursor);
        cursor += count * sizeof(T);
        return {values, count};
    };

    beatmap.breaks = take.template operator()<Break>(sizes.breaks);
    beatmap.combo_colours =
        take.template operator()<uint32_t>(sizes.colours);
    beatmap.timing_points =
        take.template operator()<TimingPoint>(sizes.timing_points);
    beatmap.hit_objects =
        take.template operator()<HitObject>(sizes.hit_objects);
    beatmap.sliders = take.template operator()<Slider>(sizes.sliders);
    beatmap.slider_points =
        take.template operator()<SliderPoint>(sizes.slider_points);
}

// `data` must be followed by kBufferPadding readable zero bytes (io.hpp).
// String fields of the result view into `data`.
inline void parse_beatmap(Arena* arena, const char* data, size_t size,
                          Beatmap& bm,
                          [[maybe_unused]] ParseOptions opts = {}) {
    if (size > kMaxInputSize) throw std::length_error("beatmap input exceeds 64 MiB");
    bm = {};
    if (size == 0) return;
    const char* p = data;
    const char* file_end = data + size;
    const auto array_sizes =
        maximum_beatmap_array_sizes(size, opts.sections);
    allocate_beatmap_arrays(arena, bm, array_sizes);

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
                p = parse_hitobjects_section(
                    bm, hit_object_count, slider_count, slider_point_count,
                    nl ? nl + 1 : file_end, file_end, opts.use_simd);
                sec = Section::Unknown;
                continue;
            } else if (sec == Section::TimingPoints) {
#if FOSU_SIMD
                if (opts.use_simd) {
                    p = parse_timing_points_section(
                        bm, timing_point_count, nl ? nl + 1 : file_end,
                        file_end);
                    sec = Section::Unknown;
                    continue;
                }
#endif
            }
#if FOSU_SIMD
            else if (sec == Section::Events && opts.use_simd) {
                p = parse_events_section(
                    bm, break_count, nl ? nl + 1 : file_end, file_end);
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

}  // namespace internal

class Parser;

namespace internal {
struct ParserStorage {
    Arena* arena;
    const char* input;
    size_t input_size;
    size_t input_storage_size;
};

ParserStorage parser_storage(Parser& parser);
}

class Parser {
public:
    Parser() : arena_(internal::acquire_parser_arena()) {
        if (!arena_) throw std::bad_alloc();
    }

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    ~Parser() { internal::recycle_parser_arena(arena_); }

    const Beatmap& parse(
        const char* data, size_t size, ParseOptions opts = {}) {
        if (!data && size) throw std::invalid_argument("null beatmap input");
        prepare_input(size, data);
        internal::parse_beatmap(arena_, input_, input_size_, beatmap_, opts);
        return beatmap_;
    }

    const Beatmap& parse(const FileBuffer& input, ParseOptions opts = {}) {
        return parse(input.data.get(), input.size, opts);
    }

    const Beatmap& parse_file(const char* path, ParseOptions opts = {}) {
        if (!path) throw std::invalid_argument("null beatmap path");

        const int file = open(path, O_RDONLY);
        if (file < 0) throw std::system_error(errno, std::generic_category());

        struct stat info;
        const int stat_result = fstat(file, &info);
        if (stat_result || info.st_size < 0) {
            const int error = stat_result ? errno : EIO;
            close(file);
            throw std::system_error(error, std::generic_category());
        }
        if (static_cast<uint64_t>(info.st_size) > kMaxInputSize) {
            close(file);
            throw std::length_error("beatmap input exceeds 64 MiB");
        }

        const size_t size = static_cast<size_t>(info.st_size);
        try {
            prepare_input(size, nullptr);
        } catch (...) {
            close(file);
            throw;
        }
        size_t bytes_read = 0;
        while (bytes_read < size) {
            const ssize_t count = read(
                file, input_ + bytes_read, size - bytes_read);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                const int error = count < 0 ? errno : EIO;
                close(file);
                throw std::system_error(error, std::generic_category());
            }
            bytes_read += static_cast<size_t>(count);
        }
        close(file);

        internal::parse_beatmap(arena_, input_, input_size_, beatmap_, opts);
        return beatmap_;
    }

private:
    friend internal::ParserStorage internal::parser_storage(Parser& parser);

    static constexpr size_t kInputExtra = kBufferPadding + 6;

    void prepare_input(size_t size, const char* data) {
        if (size > kMaxInputSize)
            throw std::length_error("beatmap input exceeds 64 MiB");
        arena_clear(arena_);
        input_ = static_cast<char*>(arena_push(arena_, size + kInputExtra, 1));
        if (!input_) throw std::bad_alloc();
        if (data && size) std::memmove(input_, data, size);
        std::memset(input_ + size, 0, kBufferPadding);
        std::memcpy(input_ + size + kBufferPadding, "Normal", 6);
        input_size_ = size;
        beatmap_ = {};
    }

    Arena* arena_;
    char* input_ = nullptr;
    size_t input_size_ = 0;
    Beatmap beatmap_{};
};

namespace internal {
inline ParserStorage parser_storage(Parser& parser) {
    return {
        .arena = parser.arena_,
        .input = parser.input_,
        .input_size = parser.input_size_,
        .input_storage_size = parser.input_size_ + Parser::kInputExtra,
    };
}
}  // namespace internal

}  // namespace fosu
