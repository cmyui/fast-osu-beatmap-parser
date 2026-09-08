#include <fosu/bindings/c_api.h>
#include <fosu/engine/loader.h>
#include <fosu/parser.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <new>

namespace {

constexpr size_t kMaxInput = FOSU_MAX_INPUT_SIZE;
static_assert(kMaxInput == fosu::kMaxInputSize);

struct Handle {
  explicit Handle(const fosu::ParsingEngine& engine) : parser(engine) {}
  fosu::Parser parser;
  fosu_view result{};
  bool valid = false;
};

fosu_string_ref string_ref(const fosu::internal::ParserStorage& storage,
                           std::string_view string) {
  if (string.empty())
    return {0, 0};

  const auto address = reinterpret_cast<uintptr_t>(string.data());
  const auto input = reinterpret_cast<uintptr_t>(storage.input);
  assert(address >= input && address - input <= storage.input_storage_size &&
         string.size() <= storage.input_storage_size - (address - input));
  return {
      static_cast<uint32_t>(address - input),
      static_cast<uint32_t>(string.size()),
  };
}

template <typename T>
T* allocate_records(fosu::Arena* arena, size_t count) {
  if (count == 0)
    return nullptr;
  return fosu::arena_push_array<T>(arena, count);
}

bool publish(Handle& handle, const fosu::Beatmap& map) {
  auto& view = handle.result;
  const auto storage = fosu::internal::parser_storage(handle.parser);
  const size_t checkpoint = fosu::arena_pos(storage.arena);
  const auto allocation_failure = [&] {
    fosu::arena_pop_to(storage.arena, checkpoint);
    handle.result = {};
    return false;
  };

  view.metadata.format_version = map.format_version;
  view.metadata.audio_filename = string_ref(storage, map.audio_filename);
  view.metadata.audio_lead_in = map.audio_lead_in;
  view.metadata.preview_time = map.preview_time;
  view.metadata.countdown = map.countdown;
  view.metadata.sample_set = static_cast<fosu_sample_set>(map.sample_set);
  view.metadata.stack_leniency = map.stack_leniency;
  view.metadata.mode = map.mode;
  view.metadata.letterbox_in_breaks = map.letterbox_in_breaks;
  view.metadata.widescreen_storyboard = map.widescreen_storyboard;
  view.metadata.epilepsy_warning = map.epilepsy_warning;
  view.metadata.special_style = map.special_style;
  view.metadata.use_skin_sprites = map.use_skin_sprites;
  view.metadata.samples_match_playback_rate = map.samples_match_playback_rate;
  view.metadata.countdown_offset = map.countdown_offset;
  view.metadata.overlay_position = string_ref(storage, map.overlay_position);
  view.metadata.skin_preference = string_ref(storage, map.skin_preference);
  view.metadata.bookmarks = string_ref(storage, map.bookmarks);
  view.metadata.distance_spacing = map.distance_spacing;
  view.metadata.beat_divisor = map.beat_divisor;
  view.metadata.grid_size = map.grid_size;
  view.metadata.timeline_zoom = map.timeline_zoom;
  view.metadata.title = string_ref(storage, map.title);
  view.metadata.title_unicode = string_ref(storage, map.title_unicode);
  view.metadata.artist = string_ref(storage, map.artist);
  view.metadata.artist_unicode = string_ref(storage, map.artist_unicode);
  view.metadata.creator = string_ref(storage, map.creator);
  view.metadata.version = string_ref(storage, map.version);
  view.metadata.source = string_ref(storage, map.source);
  view.metadata.tags = string_ref(storage, map.tags);
  view.metadata.beatmap_id = map.beatmap_id;
  view.metadata.beatmap_set_id = map.beatmap_set_id;
  view.metadata.hp = map.hp;
  view.metadata.cs = map.cs;
  view.metadata.od = map.od;
  view.metadata.ar = map.ar;
  view.metadata.slider_multiplier = map.slider_multiplier;
  view.metadata.slider_tick_rate = map.slider_tick_rate;
  view.metadata.background = string_ref(storage, map.background);
  view.metadata.video = string_ref(storage, map.video);

  view.stats = {
      map.stats.fast_path_lines,
      map.stats.slow_path_lines,
      map.stats.malformed_lines,
      map.stats.storyboard_lines,
  };
  view.text = storage.input;
  view.source_size = storage.input_size;
  view.text_size = storage.input_storage_size;

  auto* hit_objects =
      allocate_records<fosu_hit_object>(storage.arena, map.hit_objects.size());
  if (!hit_objects && !map.hit_objects.empty())
    return allocation_failure();
  for (size_t i = 0; i < map.hit_objects.size(); ++i) {
    const auto& source = map.hit_objects[i];
    hit_objects[i] = {
        source.x,        source.y,    source.type,
        source.hitsound, source.time, source.end_time,
        source.slider,   0,           string_ref(storage, source.hit_sample),
    };
  }
  view.hit_objects = hit_objects;
  view.hit_object_count = map.hit_objects.size();

  auto* sliders = allocate_records<fosu_slider>(storage.arena, map.sliders.size());
  if (!sliders && !map.sliders.empty())
    return allocation_failure();
  for (size_t i = 0; i < map.sliders.size(); ++i) {
    const auto& source = map.sliders[i];
    sliders[i] = {
        source.point_begin,
        source.point_count,
        source.slides,
        static_cast<fosu_curve_type>(source.curve_type),
        source.length,
        string_ref(storage, source.edge_sounds),
        string_ref(storage, source.edge_sets),
    };
  }
  view.sliders = sliders;
  view.slider_count = map.sliders.size();

  auto* points = allocate_records<fosu_point>(storage.arena, map.slider_points.size());
  if (!points && !map.slider_points.empty())
    return allocation_failure();
  for (size_t i = 0; i < map.slider_points.size(); ++i)
    points[i] = {map.slider_points[i].x, map.slider_points[i].y};
  view.points = points;
  view.point_count = map.slider_points.size();

  auto* timing_points =
      allocate_records<fosu_timing_point>(storage.arena, map.timing_points.size());
  if (!timing_points && !map.timing_points.empty())
    return allocation_failure();
  for (size_t i = 0; i < map.timing_points.size(); ++i) {
    const auto& source = map.timing_points[i];
    timing_points[i] = {
        source.time,
        source.beat_length,
        source.meter,
        static_cast<fosu_sample_set>(source.sample_set),
        source.sample_index,
        source.volume,
        static_cast<uint8_t>(source.uninherited),
        {},
        source.effects,
    };
  }
  view.timing_points = timing_points;
  view.timing_point_count = map.timing_points.size();

  auto* breaks = allocate_records<fosu_break>(storage.arena, map.breaks.size());
  if (!breaks && !map.breaks.empty())
    return allocation_failure();
  for (size_t i = 0; i < map.breaks.size(); ++i)
    breaks[i] = {map.breaks[i].start, map.breaks[i].end};
  view.breaks = breaks;
  view.break_count = map.breaks.size();

  auto* colours = allocate_records<uint32_t>(storage.arena, map.combo_colours.size());
  if (!colours && !map.combo_colours.empty())
    return allocation_failure();
  if (colours) {
    std::memcpy(colours, map.combo_colours.data(),
                map.combo_colours.size() * sizeof(uint32_t));
  }
  view.colours = colours;
  view.colour_count = map.combo_colours.size();
  handle.valid = true;
  return true;
}

const fosu_view* view(const Handle* handle) {
  return handle && handle->valid ? &handle->result : nullptr;
}

int parse(Handle* handle, const char* data, size_t size, uint32_t sections) {
  if (!handle)
    return FOSU_INVALID_ARGUMENT;
  handle->valid = false;
  if ((!data && size) || size > kMaxInput || (sections & ~FOSU_ALL))
    return FOSU_INVALID_ARGUMENT;
  auto parsed = handle->parser.parse(data, size, {.sections = sections});
  if (!parsed)
    return parsed.error().code == fosu::ErrorCode::AllocationFailure
               ? FOSU_OUT_OF_MEMORY
               : FOSU_INVALID_ARGUMENT;
  return publish(*handle, *parsed.value()) ? FOSU_OK : FOSU_OUT_OF_MEMORY;
}

int parse_file(Handle* handle, const char* path, uint32_t sections) {
  if (!handle)
    return FOSU_INVALID_ARGUMENT;
  handle->valid = false;
  if (!path || (sections & ~FOSU_ALL))
    return FOSU_INVALID_ARGUMENT;
  auto parsed = handle->parser.parse_file(path, {.sections = sections});
  if (!parsed) {
    switch (parsed.error().code) {
      case fosu::ErrorCode::IoFailure:
        errno = parsed.error().os_code;
        return FOSU_IO_ERROR;
      case fosu::ErrorCode::AllocationFailure:
        return FOSU_OUT_OF_MEMORY;
      case fosu::ErrorCode::InvalidInput:
      case fosu::ErrorCode::InputTooLarge:
        return FOSU_INVALID_ARGUMENT;
    }
  }
  return publish(*handle, *parsed.value()) ? FOSU_OK : FOSU_OUT_OF_MEMORY;
}

struct Cleanup {
  ~Cleanup() {
    fosu::internal::clear_parser_arena_pool();
    fosu::internal::unload_engine();
  }
};
Cleanup cleanup;

}  // namespace

extern "C" uint32_t fosu_abi_version() {
  return FOSU_ABI_VERSION;
}
extern "C" const char* fosu_backend_name() {
  const auto* engine = fosu::internal::selected_engine();
  return engine ? fosu::internal::engine_name(engine->kind) : nullptr;
}
extern "C" int fosu_backend_available(const char* name) {
  if (!name)
    return false;
  const auto* kind = fosu::internal::find_engine_kind(name);
  return kind && fosu::internal::engine_available(*kind);
}
extern "C" fosu_handle* fosu_new() {
  const auto* engine = fosu::internal::selected_engine();
  if (!engine)
    return nullptr;
  auto* handle = new (std::nothrow) Handle(*engine);
  if (!handle)
    return nullptr;
  if (!fosu::internal::parser_storage(handle->parser).arena) {
    delete handle;
    return nullptr;
  }
  return reinterpret_cast<fosu_handle*>(handle);
}
extern "C" void fosu_free(fosu_handle* handle) {
  delete reinterpret_cast<Handle*>(handle);
}
extern "C" const fosu_view* fosu_get_view(const fosu_handle* handle) {
  return view(reinterpret_cast<const Handle*>(handle));
}
extern "C" int fosu_parse(fosu_handle* handle,
                          const char* data,
                          size_t size,
                          uint32_t sections) {
  return parse(reinterpret_cast<Handle*>(handle), data, size, sections);
}
extern "C" int fosu_parse_file(fosu_handle* handle, const char* path, uint32_t sections) {
  return parse_file(reinterpret_cast<Handle*>(handle), path, sections);
}
