#ifndef FOSU_C_API_H
#define FOSU_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(FOSU_BUILDING_RUNTIME)
#define FOSU_C_API __declspec(dllexport)
#else
#define FOSU_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define FOSU_C_API __attribute__((visibility("default")))
#else
#define FOSU_C_API
#endif

#define FOSU_C_ABI_VERSION 1u
#define FOSU_C_NO_SLIDER 0xFFFFFFFFu

enum fosu_c_section {
  FOSU_C_GENERAL = 1u << 1,
  FOSU_C_EDITOR = 1u << 2,
  FOSU_C_METADATA = 1u << 3,
  FOSU_C_DIFFICULTY = 1u << 4,
  FOSU_C_EVENTS = 1u << 5,
  FOSU_C_TIMING_POINTS = 1u << 6,
  FOSU_C_COLOURS = 1u << 7,
  FOSU_C_HIT_OBJECTS = 1u << 8,
  FOSU_C_ALL_SECTIONS = 0x1FEu
};

enum fosu_c_mod {
  FOSU_C_EASY = 1u << 1,
  FOSU_C_HARD_ROCK = 1u << 4,
  FOSU_C_DOUBLE_TIME = 1u << 6,
  FOSU_C_HALF_TIME = 1u << 8,
  FOSU_C_NIGHTCORE = 1u << 9
};

enum fosu_c_status {
  FOSU_C_OK = 0,
  FOSU_C_INVALID_INPUT = 1,
  FOSU_C_IO_FAILURE = 2,
  FOSU_C_INPUT_TOO_LARGE = 3,
  FOSU_C_ALLOCATION_FAILURE = 4
};

enum fosu_c_curve_type {
  FOSU_C_BEZIER = 'B',
  FOSU_C_CATMULL = 'C',
  FOSU_C_LINEAR = 'L',
  FOSU_C_PERFECT_CURVE = 'P'
};

enum fosu_c_sample_set {
  FOSU_C_SAMPLE_NONE = 0,
  FOSU_C_SAMPLE_NORMAL = 1,
  FOSU_C_SAMPLE_SOFT = 2,
  FOSU_C_SAMPLE_DRUM = 3
};

enum fosu_c_slider_event_type {
  FOSU_C_SLIDER_HEAD = 0,
  FOSU_C_SLIDER_TICK = 1,
  FOSU_C_SLIDER_REPEAT = 2,
  FOSU_C_SLIDER_LEGACY_LAST_TICK = 3,
  FOSU_C_SLIDER_TAIL = 4
};

typedef struct fosu_c_string {
  const char* data;
  size_t size;
} fosu_c_string;

typedef struct fosu_c_point {
  float x, y;
} fosu_c_point;

typedef struct fosu_c_hit_object {
  float x, y;
  uint32_t type, hitsound;
  double time, end_time;
  uint32_t slider;
  uint8_t new_combo, combo_skip;
  fosu_c_string hit_sample;
} fosu_c_hit_object;

typedef struct fosu_c_slider {
  uint32_t point_begin, point_count;
  uint32_t segment_begin, segment_count;
  int32_t slides;
  uint32_t curve_type;
  double length;
  fosu_c_string edge_sounds, edge_sets;
} fosu_c_slider;

typedef struct fosu_c_curve_segment {
  uint32_t type;
  uint32_t degree;
  uint8_t has_degree;
  uint32_t point_begin, point_count;
} fosu_c_curve_segment;

typedef struct fosu_c_timing_point {
  double time, beat_length;
  int32_t meter, sample_set, sample_index, volume;
  uint8_t uninherited;
  uint32_t effects;
} fosu_c_timing_point;

typedef struct fosu_c_break {
  double start, end;
} fosu_c_break;

typedef struct fosu_c_stats {
  uint32_t fast_path_lines, slow_path_lines, malformed_lines, storyboard_lines;
} fosu_c_stats;

typedef struct fosu_c_path {
  const fosu_c_point* points;
  const double* cumulative_lengths;
  size_t point_count;
} fosu_c_path;

typedef struct fosu_c_slider_event {
  uint32_t type;
  double time;
  int32_t span_index;
  double span_start_time, path_progress;
  fosu_c_point position;
} fosu_c_slider_event;

typedef struct fosu_c_slider_events {
  const fosu_c_slider_event* events;
  size_t count;
} fosu_c_slider_events;

typedef struct fosu_c_stacking {
  int32_t stack_height;
  fosu_c_point stack_offset;
} fosu_c_stacking;

typedef struct fosu_c_header {
  int32_t format_version;
  fosu_c_string audio_filename;
  int32_t audio_lead_in, preview_time, countdown, sample_set, sample_volume;
  double stack_leniency;
  int32_t mode;
  uint8_t letterbox_in_breaks, widescreen_storyboard, epilepsy_warning;
  uint8_t special_style, use_skin_sprites, samples_match_playback_rate;
  int32_t countdown_offset;
  fosu_c_string overlay_position, skin_preference, bookmarks;
  double distance_spacing;
  int32_t beat_divisor, grid_size;
  double timeline_zoom;
  fosu_c_string title, title_unicode, artist, artist_unicode, creator;
  fosu_c_string version, source, tags;
  int64_t beatmap_id, beatmap_set_id;
  double hp, cs, od, ar, slider_multiplier, slider_tick_rate;
  fosu_c_string background, video;
} fosu_c_header;

// All data is borrowed from the handle and remains valid until its next parse
// attempt or destruction. A failed parse invalidates the previous view.
typedef struct fosu_c_view {
  fosu_c_header header;
  fosu_c_stats stats;
  fosu_c_string input;
  const fosu_c_break* breaks;
  size_t break_count;
  const uint32_t* combo_colours;
  size_t combo_colour_count;
  const fosu_c_timing_point* timing_points;
  size_t timing_point_count;
  const fosu_c_hit_object* hit_objects;
  size_t hit_object_count;
  const fosu_c_slider* sliders;
  size_t slider_count;
  const fosu_c_curve_segment* slider_segments;
  size_t slider_segment_count;
  const fosu_c_point* slider_points;
  size_t slider_point_count;
  const double* velocity_presets;
  size_t velocity_preset_count;
  const fosu_c_path* slider_paths;
  size_t slider_path_count;
  const fosu_c_slider_events* slider_events;
  size_t slider_event_group_count;
  const fosu_c_stacking* stacking;
  size_t stacking_count;
} fosu_c_view;

typedef struct fosu_c_options {
  uint32_t sections;
  uint8_t calculate_slider_end_times, calculate_slider_paths;
  uint8_t calculate_slider_events, apply_stacking;
  uint32_t mods;
} fosu_c_options;

typedef struct fosu_c_error {
  int32_t status;
  size_t input_offset;
  int32_t os_code;
} fosu_c_error;

typedef struct fosu_c_handle fosu_c_handle;

#ifdef __cplusplus
extern "C" {
#endif
FOSU_C_API uint32_t fosu_c_abi_version(void);
FOSU_C_API const char* fosu_c_backend_name(void);
FOSU_C_API fosu_c_handle* fosu_c_new(void);
FOSU_C_API void fosu_c_free(fosu_c_handle* handle);
// NULL options request the defaults: all sections, no derived work, no mods.
FOSU_C_API int fosu_c_parse(fosu_c_handle* handle,
                            const char* data,
                            size_t size,
                            const fosu_c_options* options);
FOSU_C_API int fosu_c_parse_file(fosu_c_handle* handle,
                                 const char* path,
                                 const fosu_c_options* options);
FOSU_C_API const fosu_c_view* fosu_c_get_view(const fosu_c_handle* handle);
FOSU_C_API fosu_c_error fosu_c_last_error(const fosu_c_handle* handle);
#ifdef __cplusplus
}
#endif

#endif
