#ifndef FOSU_C_API_H
#define FOSU_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define FOSU_API __attribute__((visibility("default")))
#else
#define FOSU_API
#endif

#define FOSU_ABI_VERSION 3
#define FOSU_MAX_INPUT_SIZE (64u * 1024u * 1024u)
#define FOSU_NO_SLIDER 0xFFFFFFFFu

// Offsets address the backing text returned by fosu_get_view. Empty strings
// have length zero. The backing text includes input padding.
typedef struct fosu_string_ref {
  uint32_t offset, length;
} fosu_string_ref;

typedef enum fosu_curve_type {
  FOSU_CURVE_BEZIER = 'B',
  FOSU_CURVE_CATMULL = 'C',
  FOSU_CURVE_LINEAR = 'L',
  FOSU_CURVE_PERFECT_CURVE = 'P'
} fosu_curve_type;

typedef enum fosu_sample_set {
  FOSU_SAMPLE_NONE = 0,
  FOSU_SAMPLE_NORMAL = 1,
  FOSU_SAMPLE_SOFT = 2,
  FOSU_SAMPLE_DRUM = 3
} fosu_sample_set;

typedef struct fosu_hit_object {
  int32_t x, y;
  uint32_t type, hitsound;
  double time, end_time;
  uint32_t slider, reserved;
  fosu_string_ref hit_sample;
} fosu_hit_object;

typedef struct fosu_slider {
  uint32_t point_begin, point_count;
  int32_t slides;
  fosu_curve_type curve_type;
  double length;
  fosu_string_ref edge_sounds, edge_sets;
} fosu_slider;

typedef struct fosu_point {
  int32_t x, y;
} fosu_point;

typedef struct fosu_timing_point {
  double time, beat_length;
  int32_t meter;
  fosu_sample_set sample_set;
  int32_t sample_index, volume;
  uint8_t uninherited;
  uint8_t reserved[3];
  uint32_t effects;
} fosu_timing_point;

typedef struct fosu_break {
  double start, end;
} fosu_break;

typedef struct fosu_stats {
  uint32_t fast_path_lines, slow_path_lines, malformed_lines, storyboard_lines;
} fosu_stats;

typedef struct fosu_metadata {
  int32_t format_version;
  fosu_string_ref audio_filename;
  int32_t audio_lead_in;
  int32_t preview_time;
  int32_t countdown;
  fosu_sample_set sample_set;
  double stack_leniency;
  int32_t mode;
  uint8_t letterbox_in_breaks;
  uint8_t widescreen_storyboard;
  uint8_t epilepsy_warning;
  uint8_t special_style;
  uint8_t use_skin_sprites;
  uint8_t samples_match_playback_rate;
  int32_t countdown_offset;
  fosu_string_ref overlay_position;
  fosu_string_ref skin_preference;
  fosu_string_ref bookmarks;
  double distance_spacing;
  int32_t beat_divisor;
  int32_t grid_size;
  double timeline_zoom;
  fosu_string_ref title;
  fosu_string_ref title_unicode;
  fosu_string_ref artist;
  fosu_string_ref artist_unicode;
  fosu_string_ref creator;
  fosu_string_ref version;
  fosu_string_ref source;
  fosu_string_ref tags;
  int64_t beatmap_id;
  int64_t beatmap_set_id;
  double hp;
  double cs;
  double od;
  double ar;
  double slider_multiplier;
  double slider_tick_rate;
  fosu_string_ref background;
  fosu_string_ref video;
} fosu_metadata;

// All pointers remain valid until the next parse on the handle or fosu_free.
// A failed parse invalidates the previous view. Different handles may be used
// concurrently; calls that mutate the same handle must be serialized.
typedef struct fosu_view {
  fosu_metadata metadata;
  fosu_stats stats;
  const char* text;
  size_t text_size, source_size;
  const fosu_hit_object* hit_objects;
  size_t hit_object_count;
  const fosu_slider* sliders;
  size_t slider_count;
  const fosu_point* points;
  size_t point_count;
  const fosu_timing_point* timing_points;
  size_t timing_point_count;
  const fosu_break* breaks;
  size_t break_count;
  const uint32_t* colours;
  size_t colour_count;
} fosu_view;

typedef struct fosu_handle fosu_handle;

enum fosu_status {
  FOSU_OK = 0,
  FOSU_INVALID_ARGUMENT = 1,
  FOSU_IO_ERROR = 2,
  FOSU_OUT_OF_MEMORY = 3
};
enum fosu_sections {
  FOSU_GENERAL = 1 << 1,
  FOSU_EDITOR = 1 << 2,
  FOSU_METADATA = 1 << 3,
  FOSU_DIFFICULTY = 1 << 4,
  FOSU_EVENTS = 1 << 5,
  FOSU_TIMING_POINTS = 1 << 6,
  FOSU_COLOURS = 1 << 7,
  FOSU_HIT_OBJECTS = 1 << 8,
  FOSU_ALL = 0x1fe
};

#ifdef __cplusplus
extern "C" {
#endif
FOSU_API uint32_t fosu_abi_version(void);

// Selection is fixed on first use for this loaded library. FOSU_BACKEND may be
// auto, scalar, avx2 or neon. NULL means the requested backend is unknown/unavailable;
// fosu_new then returns NULL. Availability includes CPU and OS support.
FOSU_API const char* fosu_backend_name(void);
FOSU_API int fosu_backend_available(const char* name);
FOSU_API fosu_handle* fosu_new(void);
FOSU_API void fosu_free(fosu_handle* handle);
// Copies data into owned, padded storage. Capacity is reused across calls.
// Input is capped at FOSU_MAX_INPUT_SIZE bytes. Malformed records are counted
// and skipped; unknown sections/keys are ignored. This is not a ranking validator.
FOSU_API int fosu_parse(fosu_handle* handle,
                        const char* data,
                        size_t size,
                        uint32_t sections);
// On FOSU_IO_ERROR, errno identifies the failing operation (EIO for early EOF).
FOSU_API int fosu_parse_file(fosu_handle* handle, const char* path, uint32_t sections);
// NULL before a successful parse, or after a failed parse.
FOSU_API const fosu_view* fosu_get_view(const fosu_handle* handle);
#ifdef __cplusplus
}
#endif
#endif
