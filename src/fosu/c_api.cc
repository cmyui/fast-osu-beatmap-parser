#include <fosu/c_api.h>
#include <fosu/engine/runtime/loader.h>
#include <fosu/parser.h>
#include <fosu/runtime.h>

#include <new>

struct fosu_c_handle {
  explicit fosu_c_handle(const fosu::ParsingEngine& engine) : parser(engine) {}

  fosu::Parser parser;
  fosu_c_view view{};
  fosu_c_error error{FOSU_C_OK, fosu::kNoErrorOffset, 0};
  bool valid = false;
};

namespace {

fosu_c_string string_view(std::string_view value) {
  return {value.data(), value.size()};
}

fosu_c_point point_view(fosu::PathPoint point) {
  return {point.x, point.y};
}

void publish_header(fosu_c_header& out, const fosu::Beatmap& map) {
  out.format_version = map.format_version;
  out.audio_filename = string_view(map.audio_filename);
  out.audio_lead_in = map.audio_lead_in;
  out.preview_time = map.preview_time;
  out.countdown = map.countdown;
  out.sample_set = static_cast<int32_t>(map.sample_set);
  out.sample_volume = map.sample_volume;
  out.stack_leniency = map.stack_leniency;
  out.mode = map.mode;
  out.letterbox_in_breaks = map.letterbox_in_breaks;
  out.widescreen_storyboard = map.widescreen_storyboard;
  out.epilepsy_warning = map.epilepsy_warning;
  out.special_style = map.special_style;
  out.use_skin_sprites = map.use_skin_sprites;
  out.samples_match_playback_rate = map.samples_match_playback_rate;
  out.countdown_offset = map.countdown_offset;
  out.overlay_position = string_view(map.overlay_position);
  out.skin_preference = string_view(map.skin_preference);
  out.bookmarks = string_view(map.bookmarks);
  out.distance_spacing = map.distance_spacing;
  out.beat_divisor = map.beat_divisor;
  out.grid_size = map.grid_size;
  out.timeline_zoom = map.timeline_zoom;
  out.title = string_view(map.title);
  out.title_unicode = string_view(map.title_unicode);
  out.artist = string_view(map.artist);
  out.artist_unicode = string_view(map.artist_unicode);
  out.creator = string_view(map.creator);
  out.version = string_view(map.version);
  out.source = string_view(map.source);
  out.tags = string_view(map.tags);
  out.beatmap_id = map.beatmap_id;
  out.beatmap_set_id = map.beatmap_set_id;
  out.hp = map.hp;
  out.cs = map.cs;
  out.od = map.od;
  out.ar = map.ar;
  out.slider_multiplier = map.slider_multiplier;
  out.slider_tick_rate = map.slider_tick_rate;
  out.background = string_view(map.background);
  out.video = string_view(map.video);
}

// Publish C records after parsing. Primitive arrays and source text remain in
// Parser storage; only records with a different C layout are copied.
bool publish(fosu_c_handle& handle, const fosu::Beatmap& map) {
  auto* arena = fosu::internal::parser_storage(handle.parser).result_arena;
  const size_t checkpoint = fosu::arena_pos(arena);
  auto fail = [&] {
    fosu::arena_pop_to(arena, checkpoint);
    handle.view = {};
    return false;
  };
  auto& view = handle.view;
  publish_header(view.header, map);
  view.stats = {map.stats.fast_path_lines, map.stats.slow_path_lines,
                map.stats.malformed_lines, map.stats.storyboard_lines};
  const auto storage = fosu::internal::parser_storage(handle.parser);
  view.input = {storage.input, storage.input_size};

  if (!map.breaks.empty()) {
    auto* records = fosu::arena_push_array<fosu_c_break>(arena, map.breaks.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.breaks.size(); ++i)
      records[i] = {map.breaks[i].start, map.breaks[i].end};
    view.breaks = records;
  }
  view.break_count = map.breaks.size();
  view.combo_colours = map.combo_colours.data();
  view.combo_colour_count = map.combo_colours.size();

  if (!map.timing_points.empty()) {
    auto* records =
        fosu::arena_push_array<fosu_c_timing_point>(arena, map.timing_points.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.timing_points.size(); ++i) {
      const auto& point = map.timing_points[i];
      records[i] = {point.time,
                    point.beat_length,
                    point.meter,
                    static_cast<int32_t>(point.sample_set),
                    point.sample_index,
                    point.volume,
                    static_cast<uint8_t>(point.uninherited),
                    point.effects};
    }
    view.timing_points = records;
  }
  view.timing_point_count = map.timing_points.size();

  if (!map.hit_objects.empty()) {
    auto* records =
        fosu::arena_push_array<fosu_c_hit_object>(arena, map.hit_objects.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.hit_objects.size(); ++i) {
      const auto& object = map.hit_objects[i];
      records[i] = {object.x,          object.y,
                    object.type,       object.hitsound,
                    object.time,       object.end_time,
                    object.slider,     static_cast<uint8_t>(object.new_combo),
                    object.combo_skip, string_view(object.hit_sample)};
    }
    view.hit_objects = records;
  }
  view.hit_object_count = map.hit_objects.size();

  if (!map.sliders.empty()) {
    auto* records = fosu::arena_push_array<fosu_c_slider>(arena, map.sliders.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.sliders.size(); ++i) {
      const auto& slider = map.sliders[i];
      records[i] = {slider.point_begin,
                    slider.point_count,
                    slider.segment_begin,
                    slider.segment_count,
                    slider.slides,
                    static_cast<uint32_t>(slider.curve_type),
                    slider.length,
                    string_view(slider.edge_sounds),
                    string_view(slider.edge_sets)};
    }
    view.sliders = records;
  }
  view.slider_count = map.sliders.size();

  if (!map.slider_segments.empty()) {
    auto* records =
        fosu::arena_push_array<fosu_c_curve_segment>(arena, map.slider_segments.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.slider_segments.size(); ++i) {
      const auto& segment = map.slider_segments[i];
      records[i] = {static_cast<uint32_t>(segment.type), segment.degree.value_or(0),
                    static_cast<uint8_t>(segment.degree.has_value()), segment.point_begin,
                    segment.point_count};
    }
    view.slider_segments = records;
  }
  view.slider_segment_count = map.slider_segments.size();

  if (!map.slider_points.empty()) {
    auto* records = fosu::arena_push_array<fosu_c_point>(arena, map.slider_points.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.slider_points.size(); ++i)
      records[i] = {map.slider_points[i].x, map.slider_points[i].y};
    view.slider_points = records;
  }
  view.slider_point_count = map.slider_points.size();
  view.velocity_presets = map.velocity_presets.data();
  view.velocity_preset_count = map.velocity_presets.size();

  if (!map.slider_paths.empty()) {
    auto* records = fosu::arena_push_array<fosu_c_path>(arena, map.slider_paths.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.slider_paths.size(); ++i) {
      const auto& path = map.slider_paths[i];
      fosu_c_point* points = nullptr;
      if (!path.points.empty()) {
        points = fosu::arena_push_array<fosu_c_point>(arena, path.points.size());
        if (!points)
          return fail();
        for (size_t j = 0; j < path.points.size(); ++j)
          points[j] = point_view(path.points[j]);
      }
      records[i] = {points, path.cumulative_lengths.data(), path.points.size()};
    }
    view.slider_paths = records;
  }
  view.slider_path_count = map.slider_paths.size();

  if (!map.slider_events.empty()) {
    auto* groups =
        fosu::arena_push_array<fosu_c_slider_events>(arena, map.slider_events.size());
    if (!groups)
      return fail();
    for (size_t i = 0; i < map.slider_events.size(); ++i) {
      const auto events = map.slider_events[i];
      fosu_c_slider_event* records = nullptr;
      if (!events.empty()) {
        records = fosu::arena_push_array<fosu_c_slider_event>(arena, events.size());
        if (!records)
          return fail();
        for (size_t j = 0; j < events.size(); ++j) {
          const auto& event = events[j];
          records[j] = {static_cast<uint32_t>(event.type),
                        event.time,
                        event.span_index,
                        event.span_start_time,
                        event.path_progress,
                        point_view(event.position)};
        }
      }
      groups[i] = {records, events.size()};
    }
    view.slider_events = groups;
  }
  view.slider_event_group_count = map.slider_events.size();

  if (!map.stacking.empty()) {
    auto* records = fosu::arena_push_array<fosu_c_stacking>(arena, map.stacking.size());
    if (!records)
      return fail();
    for (size_t i = 0; i < map.stacking.size(); ++i)
      records[i] = {map.stacking[i].stack_height,
                    point_view(map.stacking[i].stack_offset)};
    view.stacking = records;
  }
  view.stacking_count = map.stacking.size();
  handle.valid = true;
  return true;
}

fosu::ParseOptions parse_options(const fosu_c_options* options) {
  if (!options)
    return {};
  return {
      .sections = options->sections,
      .calculate_slider_end_times = options->calculate_slider_end_times != 0,
      .calculate_slider_paths = options->calculate_slider_paths != 0,
      .calculate_slider_events = options->calculate_slider_events != 0,
      .apply_stacking = options->apply_stacking != 0,
      .mods = static_cast<fosu::Mods>(options->mods),
  };
}

int set_error(fosu_c_handle& handle, fosu::Error error) {
  int status;
  switch (error.code) {
    case fosu::ErrorCode::InvalidInput:
      status = FOSU_C_INVALID_INPUT;
      break;
    case fosu::ErrorCode::IoFailure:
      status = FOSU_C_IO_FAILURE;
      break;
    case fosu::ErrorCode::InputTooLarge:
      status = FOSU_C_INPUT_TOO_LARGE;
      break;
    case fosu::ErrorCode::AllocationFailure:
      status = FOSU_C_ALLOCATION_FAILURE;
      break;
  }
  handle.error = {status, error.input_offset, error.os_code};
  return status;
}

int finish(fosu_c_handle& handle, fosu::Result<fosu::Beatmap*> result) {
  if (!result)
    return set_error(handle, result.error());
  if (!publish(handle, *result.value()))
    return set_error(handle, {fosu::ErrorCode::AllocationFailure});
  handle.error = {FOSU_C_OK, fosu::kNoErrorOffset, 0};
  return FOSU_C_OK;
}

}  // namespace

extern "C" uint32_t fosu_c_abi_version() {
  return FOSU_C_ABI_VERSION;
}

extern "C" const char* fosu_c_backend_name() {
  const auto* engine = fosu::runtime_engine();
  return engine ? fosu::internal::engine_name(engine->kind) : nullptr;
}

extern "C" fosu_c_handle* fosu_c_new() {
  const auto* engine = fosu::runtime_engine();
  return engine ? new (std::nothrow) fosu_c_handle(*engine) : nullptr;
}

extern "C" void fosu_c_free(fosu_c_handle* handle) {
  delete handle;
}

extern "C" int fosu_c_parse(fosu_c_handle* handle,
                            const char* data,
                            size_t size,
                            const fosu_c_options* options) {
  if (!handle)
    return FOSU_C_INVALID_INPUT;
  handle->valid = false;
  handle->view = {};
  return finish(*handle, handle->parser.parse(data, size, parse_options(options)));
}

extern "C" int fosu_c_parse_file(fosu_c_handle* handle,
                                 const char* path,
                                 const fosu_c_options* options) {
  if (!handle)
    return FOSU_C_INVALID_INPUT;
  handle->valid = false;
  handle->view = {};
  return finish(*handle, handle->parser.parse_file(path, parse_options(options)));
}

extern "C" const fosu_c_view* fosu_c_get_view(const fosu_c_handle* handle) {
  return handle && handle->valid ? &handle->view : nullptr;
}

extern "C" fosu_c_error fosu_c_last_error(const fosu_c_handle* handle) {
  return handle ? handle->error
                : fosu_c_error{FOSU_C_INVALID_INPUT, fosu::kNoErrorOffset, 0};
}
