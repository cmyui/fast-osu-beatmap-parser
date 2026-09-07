#include <fosu/c_api.h>
#include <fosu/offset_beatmap.hpp>
#include <fosu/parser.hpp>

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

struct fosu_handle {
    fosu::FileBuffer input;
    fosu::OffsetBeatmap map;
    fosu_view result{};
    bool valid = false;
};

namespace {
constexpr size_t kExtra = fosu::kBufferPadding + 6;  // "Normal" default sample set
constexpr size_t kMaxInput = FOSU_MAX_INPUT_SIZE;
static_assert(kMaxInput == fosu::kMaxInputSize);

void reserve(fosu_handle& h, size_t size) {
    if (h.input.capacity < size + kExtra) {
        h.input.data.reset(new char[size + kExtra]);
        h.input.capacity = size + kExtra;
    }
}

fosu_string_ref string_ref(const fosu_handle& h, std::string_view s) {
    if (s.empty()) return {0, 0};
    // All nonempty defaults are the sample-set value "Normal".
    const auto p = reinterpret_cast<uintptr_t>(s.data());
    const auto base = reinterpret_cast<uintptr_t>(h.input.data.get());
    if (p >= base && p - base < h.input.size)
        return {static_cast<uint32_t>(p - base), static_cast<uint32_t>(s.size())};
    assert(s == "Normal");
    return {static_cast<uint32_t>(h.input.size + fosu::kBufferPadding), 6};
}

void publish(fosu_handle& h) {
    const auto& bm = h.map;
    auto& v = h.result;
    v.metadata.format_version = bm.format_version;
    v.metadata.audio_filename = string_ref(h, bm.audio_filename);
    v.metadata.audio_lead_in = bm.audio_lead_in;
    v.metadata.preview_time = bm.preview_time;
    v.metadata.countdown = bm.countdown;
    v.metadata.sample_set = string_ref(h, bm.sample_set);
    v.metadata.stack_leniency = bm.stack_leniency;
    v.metadata.mode = bm.mode;
    v.metadata.letterbox_in_breaks = bm.letterbox_in_breaks;
    v.metadata.widescreen_storyboard = bm.widescreen_storyboard;
    v.metadata.epilepsy_warning = bm.epilepsy_warning;
    v.metadata.special_style = bm.special_style;
    v.metadata.use_skin_sprites = bm.use_skin_sprites;
    v.metadata.samples_match_playback_rate = bm.samples_match_playback_rate;
    v.metadata.countdown_offset = bm.countdown_offset;
    v.metadata.overlay_position = string_ref(h, bm.overlay_position);
    v.metadata.skin_preference = string_ref(h, bm.skin_preference);
    v.metadata.bookmarks = string_ref(h, bm.bookmarks);
    v.metadata.distance_spacing = bm.distance_spacing;
    v.metadata.beat_divisor = bm.beat_divisor;
    v.metadata.grid_size = bm.grid_size;
    v.metadata.timeline_zoom = bm.timeline_zoom;
    v.metadata.title = string_ref(h, bm.title);
    v.metadata.title_unicode = string_ref(h, bm.title_unicode);
    v.metadata.artist = string_ref(h, bm.artist);
    v.metadata.artist_unicode = string_ref(h, bm.artist_unicode);
    v.metadata.creator = string_ref(h, bm.creator);
    v.metadata.version = string_ref(h, bm.version);
    v.metadata.source = string_ref(h, bm.source);
    v.metadata.tags = string_ref(h, bm.tags);
    v.metadata.beatmap_id = bm.beatmap_id;
    v.metadata.beatmap_set_id = bm.beatmap_set_id;
    v.metadata.hp = bm.hp;
    v.metadata.cs = bm.cs;
    v.metadata.od = bm.od;
    v.metadata.ar = bm.ar;
    v.metadata.slider_multiplier = bm.slider_multiplier;
    v.metadata.slider_tick_rate = bm.slider_tick_rate;
    v.metadata.background = string_ref(h, bm.background);
    v.metadata.video = string_ref(h, bm.video);
    v.stats = {bm.stats.fast_path_lines, bm.stats.slow_path_lines,
               bm.stats.malformed_lines, bm.stats.storyboard_lines};
    v.text = h.input.data.get();
    v.source_size = h.input.size;
    v.text_size = h.input.size + kExtra;
    v.hit_objects = bm.hit_objects.data(); v.hit_object_count = bm.hit_objects.size();
    v.sliders = bm.sliders.data(); v.slider_count = bm.sliders.size();
    v.points = bm.slider_points.data(); v.point_count = bm.slider_points.size();
    v.timing_points = bm.timing_points.data(); v.timing_point_count = bm.timing_points.size();
    v.breaks = bm.breaks.data(); v.break_count = bm.breaks.size();
    v.colours = bm.combo_colours.data(); v.colour_count = bm.combo_colours.size();
    h.valid = true;
}

int parse_owned(fosu_handle& h, uint32_t sections) {
    char* p = h.input.data.get();
    memset(p + h.input.size, 0, fosu::kBufferPadding);
    memcpy(p + h.input.size + fosu::kBufferPadding, "Normal", 6);
    fosu::parse_into(p, h.input.size, h.map, {.sections = sections});
    publish(h);
    return FOSU_OK;
}
}  // namespace

extern "C" uint32_t fosu_abi_version() { return FOSU_ABI_VERSION; }
extern "C" fosu_handle* fosu_new() {
    return new (std::nothrow) fosu_handle;
}
extern "C" void fosu_free(fosu_handle* h) { delete h; }
extern "C" const fosu_view* fosu_get_view(const fosu_handle* h) {
    return h && h->valid ? &h->result : nullptr;
}
extern "C" int fosu_parse(fosu_handle* h, const char* data, size_t size, uint32_t sections) {
    if (!h) return FOSU_INVALID_ARGUMENT;
    h->valid = false;
    if ((!data && size) || size > kMaxInput || (sections & ~FOSU_ALL)) return FOSU_INVALID_ARGUMENT;
    try {
        if (h->input.capacity < size + kExtra) {
            // Copy before releasing the old buffer: data may view that buffer.
            std::unique_ptr<char[]> next(new char[size + kExtra]);
            if (size) memcpy(next.get(), data, size);
            h->input.data = std::move(next);
            h->input.capacity = size + kExtra;
        } else if (size) {
            memmove(h->input.data.get(), data, size);
        }
        h->input.size = size;
        return parse_owned(*h, sections);
    } catch (const std::bad_alloc&) { return FOSU_OUT_OF_MEMORY; }
      catch (const std::length_error&) { return FOSU_OUT_OF_MEMORY; }
}
extern "C" int fosu_parse_file(fosu_handle* h, const char* path, uint32_t sections) {
    if (!h) return FOSU_INVALID_ARGUMENT;
    h->valid = false;
    if (!path || (sections & ~FOSU_ALL)) return FOSU_INVALID_ARGUMENT;
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return FOSU_IO_ERROR;
    struct stat st;
    const int stat_result = fstat(fd, &st);
    if (stat_result || st.st_size < 0) {
        const int error = stat_result ? errno : EIO;
        close(fd);
        errno = error;
        return FOSU_IO_ERROR;
    }
    if (static_cast<uint64_t>(st.st_size) > kMaxInput) { close(fd); return FOSU_INVALID_ARGUMENT; }
    int status = FOSU_OK;
    int error = 0;
    try {
        const size_t size = static_cast<size_t>(st.st_size);
        reserve(*h, size);
        size_t got = 0;
        while (got < size) {
            const ssize_t n = read(fd, h->input.data.get() + got, size - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                error = n < 0 ? errno : EIO;
                status = FOSU_IO_ERROR;
                break;
            }
            got += static_cast<size_t>(n);
        }
        if (status == FOSU_OK) h->input.size = got;
    } catch (const std::bad_alloc&) { status = FOSU_OUT_OF_MEMORY; }
      catch (const std::length_error&) { status = FOSU_OUT_OF_MEMORY; }
    close(fd);
    if (status == FOSU_IO_ERROR) errno = error;
    if (status != FOSU_OK) return status;
    try { return parse_owned(*h, sections); }
    catch (const std::bad_alloc&) { return FOSU_OUT_OF_MEMORY; }
    catch (const std::length_error&) { return FOSU_OUT_OF_MEMORY; }
}
