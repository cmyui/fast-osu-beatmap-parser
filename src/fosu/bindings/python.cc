// Construct detached Python values using the CPython stable ABI.
#include <Python.h>
#include <fosu/engine/runtime/loader.h>
#include <fosu/parser.h>
#include <cerrno>
#include <cstring>
#include <initializer_list>
#include <new>
#include <span>
#include <utility>

namespace {
struct PythonError {};
struct PythonRef {
  PyObject* p;
  explicit PythonRef(PyObject* p) : p(p) {
    if (!p)
      throw PythonError{};
  }
  ~PythonRef() { Py_XDECREF(p); }
  PythonRef(const PythonRef&) = delete;
  PythonRef(PythonRef&& r) noexcept : p(std::exchange(r.p, nullptr)) {}
  PythonRef& operator=(PythonRef&& r) noexcept {
    Py_XDECREF(p);
    p = std::exchange(r.p, nullptr);
    return *this;
  }
  PyObject* release() { return std::exchange(p, nullptr); }
  operator PyObject*() const { return p; }
};
PythonRef integer(long long n) {
  return PythonRef(PyLong_FromLongLong(n));
}
PythonRef number(double n) {
  return PythonRef(PyFloat_FromDouble(n));
}
PythonRef boolean(bool n) {
  return PythonRef(PyBool_FromLong(n));
}
PythonRef none() {
  Py_INCREF(Py_None);
  return PythonRef(Py_None);
}
PythonRef optional_integer(long long n) {
  return n == -1 ? none() : integer(n);
}
PythonRef retain(PyObject* p) {
  Py_INCREF(p);
  return PythonRef(p);
}

// NumberStyles.Integer uses ASCII digits, a sign/ASCII whitespace, and int32 range.
// The .NET parser also accepts trailing NULs after whitespace.
bool bookmark(const char* p, const char* end, int32_t& out) {
  auto space = [](unsigned char c) {
    return c == ' ' || (c >= 9 && c <= 13);
  };
  while (p != end && space(*p))
    ++p;
  bool negative = false;
  if (p != end && (*p == '+' || *p == '-'))
    negative = *p++ == '-';
  const char* digits = p;
  uint32_t value = 0, limit = negative ? 2147483648u : 2147483647u;
  while (p != end && *p >= '0' && *p <= '9') {
    uint32_t digit = static_cast<unsigned>(*p++ - '0');
    if (value > (limit - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  if (p == digits)
    return false;
  while (p != end && space(*p))
    ++p;
  while (p != end && *p == '\0')
    ++p;
  if (p != end)
    return false;
  out = static_cast<int32_t>(negative ? -static_cast<int64_t>(value) : value);
  return true;
}

// Per-module ownership of the model keeps this compatible with subinterpreters.
enum Field {
  f_stacking,
  f_stack_height,
  f_stack_offset,
  f_events,
  f_span_index,
  f_span_start_time,
  f_path_progress,
  f_position,
  f_path,
  f_points,
  f_cumulative_lengths,
  f_x,
  f_y,
  f_slides,
  f_length,
  f_curve_type,
  f_edge_sounds,
  f_edge_sets,
  f_end_time,
  f_hitsound,
  f_type,
  f_new_combo,
  f_combo_skip,
  f_hit_sample,
  f_control_points,
  f_time,
  f_beat_length,
  f_meter,
  f_sample_set,
  f_sample_index,
  f_volume,
  f_uninherited,
  f_effects,
  f_start,
  f_end,
  f_format_version,
  f_audio_filename,
  f_audio_lead_in,
  f_preview_time,
  f_countdown,
  f_stack_leniency,
  f_mode,
  f_letterbox_in_breaks,
  f_widescreen_storyboard,
  f_epilepsy_warning,
  f_special_style,
  f_use_skin_sprites,
  f_samples_match_playback_rate,
  f_countdown_offset,
  f_overlay_position,
  f_skin_preference,
  f_distance_spacing,
  f_beat_divisor,
  f_grid_size,
  f_timeline_zoom,
  f_title,
  f_title_unicode,
  f_artist,
  f_artist_unicode,
  f_creator,
  f_version,
  f_source,
  f_beatmap_id,
  f_beatmap_set_id,
  f_hp,
  f_cs,
  f_od,
  f_ar,
  f_slider_multiplier,
  f_slider_tick_rate,
  f_background,
  f_video,
  f_tag_list,
  f_bookmark_list,
  f_tags,
  f_bookmarks,
  f_hit_objects,
  f_timing_points,
  f_breaks,
  f_stats,
  f_combo_colours,
  f_fast_path_lines,
  f_slow_path_lines,
  f_malformed_lines,
  f_storyboard_lines,
  field_count
};
const char* field_names[] = {"stacking",
                             "stack_height",
                             "stack_offset",
                             "events",
                             "span_index",
                             "span_start_time",
                             "path_progress",
                             "position",
                             "path",
                             "points",
                             "cumulative_lengths",
                             "x",
                             "y",
                             "slides",
                             "length",
                             "curve_type",
                             "edge_sounds",
                             "edge_sets",
                             "end_time",
                             "hitsound",
                             "type",
                             "new_combo",
                             "combo_skip",
                             "hit_sample",
                             "control_points",
                             "time",
                             "beat_length",
                             "meter",
                             "sample_set",
                             "sample_index",
                             "volume",
                             "uninherited",
                             "effects",
                             "start",
                             "end",
                             "format_version",
                             "audio_filename",
                             "audio_lead_in",
                             "preview_time",
                             "countdown",
                             "stack_leniency",
                             "mode",
                             "letterbox_in_breaks",
                             "widescreen_storyboard",
                             "epilepsy_warning",
                             "special_style",
                             "use_skin_sprites",
                             "samples_match_playback_rate",
                             "countdown_offset",
                             "overlay_position",
                             "skin_preference",
                             "distance_spacing",
                             "beat_divisor",
                             "grid_size",
                             "timeline_zoom",
                             "title",
                             "title_unicode",
                             "artist",
                             "artist_unicode",
                             "creator",
                             "version",
                             "source",
                             "beatmap_id",
                             "beatmap_set_id",
                             "hp",
                             "cs",
                             "od",
                             "ar",
                             "slider_multiplier",
                             "slider_tick_rate",
                             "background",
                             "video",
                             "tag_list",
                             "bookmark_list",
                             "tags",
                             "bookmarks",
                             "hit_objects",
                             "timing_points",
                             "breaks",
                             "stats",
                             "combo_colours",
                             "fast_path_lines",
                             "slow_path_lines",
                             "malformed_lines",
                             "storyboard_lines"};
static_assert(sizeof(field_names) / sizeof(field_names[0]) == field_count);
enum PythonType {
  t_point,
  t_circle,
  t_slider,
  t_spinner,
  t_hold,
  t_timing,
  t_break,
  t_stats,
  t_path_point,
  t_path,
  t_beatmap,
  t_event,
  t_stacking,
  t_event_type,
  t_sound,
  t_mode,
  t_sample,
  t_curve,
  type_count
};
constexpr int record_type_count = t_stacking + 1;
constexpr const char* type_names[] = {
    "Point",    "Circle",          "Slider",    "Spinner",    "HoldNote",  "TimingPoint",
    "Break",    "ParseStats",      "PathPoint", "SliderPath", "Beatmap",   "SliderEvent",
    "Stacking", "SliderEventType", "HitSound",  "GameMode",   "SampleSet", "CurveType"};
struct PythonSlot {
  PyObject* descriptor;
  descrsetfunc assign;
};
struct State {
  PyObject* model;
  PyObject* types[type_count];
  PythonSlot slots[record_type_count][field_count];
  PyObject* sounds[16];
  PyObject* samples[4];
  PyObject* curves[4];
};

struct BeatmapConverter {
  const fosu::Beatmap& map;
  const State& state;
  BeatmapConverter(const fosu::Beatmap& map, const State& state)
      : map(map), state(state) {}

  PythonRef sample_set(fosu::SampleSet value) {
    return retain(state.samples[static_cast<int>(value)]);
  }

  PythonRef curve(fosu::CurveType value) {
    switch (value) {
      case fosu::CurveType::Bezier:
        return retain(state.curves[0]);
      case fosu::CurveType::Catmull:
        return retain(state.curves[1]);
      case fosu::CurveType::Linear:
        return retain(state.curves[2]);
      case fosu::CurveType::PerfectCurve:
        return retain(state.curves[3]);
    }
    __builtin_unreachable();  // Only validated native enums reach conversion.
  }

  struct Value {
    Field field;
    PythonRef value;
  };
  PythonRef record(PythonType kind,
                   std::initializer_list<Value> values,
                   std::span<const Value> common = {}) {
    // The package owns these plain slotted dataclasses. Allocate once and use
    // their cached descriptor setters, without name lookup or Python __init__.
    PythonRef out(
        PyType_GenericAlloc(reinterpret_cast<PyTypeObject*>(state.types[kind]), 0));
    auto assign = [&](const Value& value) {
      const auto& slot = state.slots[kind][value.field];
      if (slot.assign(slot.descriptor, out, value.value) < 0)
        throw PythonError{};
    };
    for (const auto& value : common)
      assign(value);
    for (const auto& value : values)
      assign(value);
    return out;
  }
  PythonRef sound(uint32_t value) {
    if (value < 16)
      return retain(state.sounds[value]);
    // Preserve unknown bits supported by HitSound's IntFlag contract.
    return PythonRef(PyObject_CallFunction(state.types[t_sound], "I", value));
  }
  PythonRef string(std::string_view s) {
    return PythonRef(
        PyUnicode_DecodeUTF8(s.empty() ? "" : s.data(), s.size(), "surrogateescape"));
  }
  PythonRef point(float x, float y) {
    return record(t_point, {{f_x, number(x)}, {f_y, number(y)}});
  }
  template <class F>
  PythonRef list(size_t count, F item) {
    PythonRef out(PyList_New(static_cast<Py_ssize_t>(count)));
    for (size_t i = 0; i < count; ++i) {
      PythonRef value = item(i);
      if (PyList_SetItem(out, i, value.release()) < 0)
        throw PythonError{};
    }
    return out;
  }
  PythonRef hit_object(const fosu::HitObject& h) {
    const bool circle = h.type & 1, slider = !circle && (h.type & 2);
    PythonRef time = number(h.time);
    const Value common[] = {{f_time, retain(time)},
                            {f_stacking, stacking(h)},
                            {f_end_time, circle ? std::move(time) : number(h.end_time)},
                            {f_x, number(h.x)},
                            {f_y, number(h.y)},
                            {f_hitsound, sound(h.hitsound)},
                            {f_type, integer(h.type)},
                            {f_new_combo, boolean(h.new_combo)},
                            {f_combo_skip, integer(h.combo_skip)},
                            {f_hit_sample, string(h.hit_sample)}};
    if (slider) {
      const auto& s = map.sliders[h.slider];
      return record(
          t_slider,
          {{f_slides, integer(s.slides)},
           {f_events, slider_events(h.slider)},
           {f_path,
            map.slider_paths.empty() ? none() : slider_path(map.slider_paths[h.slider])},
           {f_length, number(s.length)},
           {f_curve_type, curve(s.curve_type)},
           {f_edge_sounds, string(s.edge_sounds)},
           {f_edge_sets, string(s.edge_sets)},
           {f_control_points, list(s.point_count + 1,
                                   [&](size_t j) {
                                     if (j == 0)
                                       return point(h.x, h.y);
                                     const auto& p =
                                         map.slider_points[s.point_begin + j - 1];
                                     return point(p.x, p.y);
                                   })}},
          common);
    }
    return record(circle ? t_circle : h.type & 8 ? t_spinner : t_hold, {}, common);
  }
  PythonRef timing_point(const fosu::TimingPoint& t) {
    return record(t_timing, {{f_time, number(t.time)},
                             {f_beat_length, number(t.beat_length)},
                             {f_meter, integer(t.meter)},
                             {f_sample_set, sample_set(t.sample_set)},
                             {f_sample_index, integer(t.sample_index)},
                             {f_volume, integer(t.volume)},
                             {f_uninherited, boolean(t.uninherited)},
                             {f_effects, integer(t.effects)}});
  }
  PythonRef stacking(const fosu::HitObject& object) {
    if (map.stacking.empty())
      return none();
    const auto& value = map.stacking[&object - map.hit_objects.data()];
    return record(
        t_stacking,
        {
            {f_stack_height, integer(value.stack_height)},
            {f_stack_offset, record(t_path_point, {{f_x, number(value.stack_offset.x)},
                                                   {f_y, number(value.stack_offset.y)}})},
        });
  }
  PythonRef slider_events(size_t index) {
    const auto events = map.slider_events.empty() ? std::span<fosu::SliderEvent>{}
                                                  : map.slider_events[index];
    return list(events.size(), [&](size_t i) {
      const auto& e = events[i];
      return record(
          t_event,
          {
              {f_type, PythonRef(PyObject_CallFunction(state.types[t_event_type], "i",
                                                       static_cast<int>(e.type)))},
              {f_time, number(e.time)},
              {f_span_index, integer(e.span_index)},
              {f_span_start_time, number(e.span_start_time)},
              {f_path_progress, number(e.path_progress)},
              {f_position, record(t_path_point, {{f_x, number(e.position.x)},
                                                 {f_y, number(e.position.y)}})},
          });
    });
  }
  PythonRef slider_path(const fosu::SliderPath& path) {
    return record(
        t_path,
        {
            {f_points, list(path.points.size(),
                            [&](size_t i) {
                              return record(t_path_point,
                                            {{f_x, number(path.points[i].x)},
                                             {f_y, number(path.points[i].y)}});
                            })},
            {f_cumulative_lengths, list(path.cumulative_lengths.size(),
                                        [&](size_t i) {
                                          return number(path.cumulative_lengths[i]);
                                        })},
        });
  }
  PythonRef break_period(const fosu::Break& period) {
    return record(t_break,
                  {{f_start, number(period.start)}, {f_end, number(period.end)}});
  }
  PythonRef stats() {
    return record(t_stats, {{f_fast_path_lines, integer(map.stats.fast_path_lines)},
                            {f_slow_path_lines, integer(map.stats.slow_path_lines)},
                            {f_malformed_lines, integer(map.stats.malformed_lines)},
                            {f_storyboard_lines, integer(map.stats.storyboard_lines)}});
  }
  PythonRef bookmark_list(PyObject* bookmarks) {
    PythonRef marks(PyList_New(0));
    // osu's legacy decoder uses invariant int.TryParse and skips invalid tokens.
    // SplitKeyVal trims .NET whitespace around the complete field first.
    PythonRef trimmed(PyObject_CallMethod(
        bookmarks, "strip", "s",
        "\t\n\v\f\r \u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004"
        "\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000"));
    PythonRef encoded(PyUnicode_AsEncodedString(trimmed, "utf-8", "surrogateescape"));
    char* bookmark_data;
    Py_ssize_t bookmark_size;
    if (PyBytes_AsStringAndSize(encoded, &bookmark_data, &bookmark_size) < 0)
      throw PythonError{};
    const char* begin = bookmark_data;
    const char* end = begin + bookmark_size;
    while (begin != end) {
      const char* stop = begin;
      while (stop != end && *stop != ',')
        ++stop;
      int32_t value;
      if (bookmark(begin, stop, value)) {
        PythonRef number = integer(value);
        if (PyList_Append(marks, number) < 0)
          throw PythonError{};
      }
      begin = stop == end ? end : stop + 1;
    }
    return marks;
  }
  PythonRef beatmap() {
    const auto& m = map;
    PythonRef tags = string(m.tags), bookmarks = string(m.bookmarks);
    return record(
        t_beatmap,
        {{f_format_version, integer(m.format_version)},
         {f_audio_filename, string(m.audio_filename)},
         {f_audio_lead_in, integer(m.audio_lead_in)},
         {f_preview_time, optional_integer(m.preview_time)},
         {f_countdown, integer(m.countdown)},
         {f_sample_set, sample_set(m.sample_set)},
         {f_stack_leniency, number(m.stack_leniency)},
         {f_mode, PythonRef(PyObject_CallFunctionObjArgs(state.types[t_mode],
                                                         integer(m.mode).p, nullptr))},
         {f_letterbox_in_breaks, boolean(m.letterbox_in_breaks)},
         {f_widescreen_storyboard, boolean(m.widescreen_storyboard)},
         {f_epilepsy_warning, boolean(m.epilepsy_warning)},
         {f_special_style, boolean(m.special_style)},
         {f_use_skin_sprites, boolean(m.use_skin_sprites)},
         {f_samples_match_playback_rate, boolean(m.samples_match_playback_rate)},
         {f_countdown_offset, integer(m.countdown_offset)},
         {f_overlay_position, string(m.overlay_position)},
         {f_skin_preference, string(m.skin_preference)},
         {f_distance_spacing, number(m.distance_spacing)},
         {f_beat_divisor, integer(m.beat_divisor)},
         {f_grid_size, integer(m.grid_size)},
         {f_timeline_zoom, number(m.timeline_zoom)},
         {f_title, string(m.title)},
         {f_title_unicode, string(m.title_unicode)},
         {f_artist, string(m.artist)},
         {f_artist_unicode, string(m.artist_unicode)},
         {f_creator, string(m.creator)},
         {f_version, string(m.version)},
         {f_source, string(m.source)},
         {f_beatmap_id, optional_integer(m.beatmap_id)},
         {f_beatmap_set_id, optional_integer(m.beatmap_set_id)},
         {f_hp, number(m.hp)},
         {f_cs, number(m.cs)},
         {f_od, number(m.od)},
         {f_ar, number(m.ar)},
         {f_slider_multiplier, number(m.slider_multiplier)},
         {f_slider_tick_rate, number(m.slider_tick_rate)},
         {f_background, string(m.background)},
         {f_video, string(m.video)},
         {f_tag_list, PythonRef(PyUnicode_Split(tags, nullptr, -1))},
         {f_bookmark_list, bookmark_list(bookmarks)},
         {f_tags, std::move(tags)},
         {f_bookmarks, std::move(bookmarks)},
         {f_hit_objects, list(map.hit_objects.size(),
                              [&](size_t i) {
                                return hit_object(map.hit_objects[i]);
                              })},
         {f_timing_points, list(map.timing_points.size(),
                                [&](size_t i) {
                                  return timing_point(map.timing_points[i]);
                                })},
         {f_breaks, list(map.breaks.size(),
                         [&](size_t i) {
                           return break_period(map.breaks[i]);
                         })},
         {f_stats, stats()},
         {f_combo_colours, list(map.combo_colours.size(), [&](size_t i) {
            return integer(map.combo_colours[i]);
          })}});
  }
};
PyObject* parse_impl(PyObject* module, PyObject* args, bool file) {
  PyObject* arg;
  long sections;
  int calculate_slider_end_times;
  int calculate_slider_paths;
  int calculate_slider_events;
  int apply_stacking;
  if (!PyArg_ParseTuple(args, "Olpppp", &arg, &sections, &calculate_slider_end_times,
                        &calculate_slider_paths, &calculate_slider_events,
                        &apply_stacking))
    return nullptr;
  if (sections < 0 || (static_cast<unsigned long>(sections) &
                       ~static_cast<unsigned long>(fosu::kAllSections))) {
    PyErr_SetString(PyExc_ValueError, "invalid sections");
    return nullptr;
  }
  const fosu::ParseOptions options{
      static_cast<uint32_t>(sections), calculate_slider_end_times != 0,
      calculate_slider_paths != 0, calculate_slider_events != 0, apply_stacking != 0};
  try {
    PythonRef input = file ? PythonRef(PyOS_FSPath(arg)) : retain(arg);
    if (file && PyUnicode_Check(input))
      input = PythonRef(PyUnicode_EncodeFSDefault(input));
    char* bytes;
    Py_ssize_t size;
    if (PyBytes_AsStringAndSize(input, &bytes, &size) < 0)
      throw PythonError{};
    if (file && std::memchr(bytes, 0, size)) {
      PyErr_SetString(PyExc_ValueError, "embedded null byte");
      throw PythonError{};
    }
    const auto* engine = fosu::internal::selected_engine();
    // Backend selection was checked when the module was imported.
    fosu::Parser parser(*engine);
    // Both native entry points are noexcept, including allocation and I/O failures.
    PyThreadState* thread = PyEval_SaveThread();
    auto result =
        file ? parser.parse_file(bytes, options) : parser.parse(bytes, size, options);
    PyEval_RestoreThread(thread);
    if (!result) {
      const auto error = result.error();
      switch (error.code) {
        case fosu::ErrorCode::AllocationFailure:
          PyErr_NoMemory();
          break;
        case fosu::ErrorCode::IoFailure:
          errno = error.os_code;
          PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, arg);
          break;
        case fosu::ErrorCode::InputTooLarge:
          PyErr_SetString(PyExc_ValueError, "beatmap input exceeds the supported size");
          break;
        case fosu::ErrorCode::InvalidInput:
          PyErr_SetString(PyExc_ValueError, "invalid input");
          break;
      }
      throw PythonError{};
    }
    auto* state = static_cast<State*>(PyModule_GetState(module));
    BeatmapConverter converter(*result.value(), *state);
    return converter.beatmap().release();
    // Parser destruction releases native storage before the result escapes.
  } catch (PythonError&) {
    return nullptr;
  } catch (std::bad_alloc&) {
    return PyErr_NoMemory();
  }
}
PyObject* parse(PyObject* m, PyObject* arg) {
  return parse_impl(m, arg, false);
}
PyObject* parse_file(PyObject* m, PyObject* arg) {
  return parse_impl(m, arg, true);
}
PyMethodDef methods[] = {
    {"parse", parse, METH_VARARGS, "Parse bytes into a detached dataclass graph."},
    {"parse_file", parse_file, METH_VARARGS,
     "Read and parse a file into a detached dataclass graph."},
    {nullptr, nullptr, 0, nullptr}};
int traverse(PyObject* m, visitproc visit, void* arg) {
  auto* s = static_cast<State*>(PyModule_GetState(m));
  if (!s)
    return 0;
  Py_VISIT(s->model);
  for (auto* type : s->types) {
    Py_VISIT(type);
  }
  for (const auto& row : s->slots)
    for (const auto& slot : row) {
      Py_VISIT(slot.descriptor);
    }
  for (auto* sound : s->sounds) {
    Py_VISIT(sound);
  }
  for (auto* sample : s->samples) {
    Py_VISIT(sample);
  }
  for (auto* curve : s->curves) {
    Py_VISIT(curve);
  }
  return 0;
}
int clear(PyObject* m) {
  auto* s = static_cast<State*>(PyModule_GetState(m));
  if (!s)
    return 0;
  Py_CLEAR(s->model);
  for (auto*& type : s->types) {
    Py_CLEAR(type);
  }
  for (auto& row : s->slots)
    for (auto& slot : row) {
      Py_CLEAR(slot.descriptor);
    }
  for (auto*& sound : s->sounds) {
    Py_CLEAR(sound);
  }
  for (auto*& sample : s->samples) {
    Py_CLEAR(sample);
  }
  for (auto*& curve : s->curves) {
    Py_CLEAR(curve);
  }
  return 0;
}
void free_module(void* m) {
  clear(static_cast<PyObject*>(m));
}
int exec_module(PyObject* m) {
  try {
    const auto* engine = fosu::internal::selected_engine();
    if (!engine) {
      PyErr_SetString(PyExc_ImportError, "FOSU_BACKEND requests an unavailable backend");
      return -1;
    }
    if (PyModule_AddStringConstant(m, "backend",
                                   fosu::internal::engine_name(engine->kind)) < 0)
      return -1;
    auto* s = static_cast<State*>(PyModule_GetState(m));
    PythonRef package(PyObject_GetAttrString(m, "__package__"));
    PythonRef name(PyUnicode_FromFormat("%U._model", package.p));
    s->model = PyImport_Import(name);
    if (!s->model)
      return -1;
    for (int i = 0; i < type_count; ++i) {
      s->types[i] = PyObject_GetAttrString(s->model, type_names[i]);
      if (!s->types[i])
        return -1;
    }
    for (int type = 0; type < record_type_count; ++type) {
      for (int field = 0; field < field_count; ++field) {
        auto& slot = s->slots[type][field];
        slot.descriptor = PyObject_GetAttrString(s->types[type], field_names[field]);
        if (!slot.descriptor) {
          // This field belongs to a different record type.
          if (!PyErr_ExceptionMatches(PyExc_AttributeError))
            return -1;
          PyErr_Clear();
          continue;
        }
        slot.assign = reinterpret_cast<descrsetfunc>(
            PyType_GetSlot(Py_TYPE(slot.descriptor), Py_tp_descr_set));
        if (!slot.assign) {
          PyErr_Format(PyExc_TypeError, "%s.%s must be a writable slot", type_names[type],
                       field_names[field]);
          return -1;
        }
      }
    }
    for (int i = 0; i < 16; ++i) {
      s->sounds[i] = PyObject_CallFunction(s->types[t_sound], "i", i);
      if (!s->sounds[i])
        return -1;
    }
    for (int i = 0; i < 4; ++i) {
      s->samples[i] = PyObject_CallFunction(s->types[t_sample], "i", i);
      if (!s->samples[i])
        return -1;
    }
    constexpr const char* curves[] = {"BEZIER", "CATMULL", "LINEAR", "PERFECT_CURVE"};
    for (int i = 0; i < 4; ++i) {
      s->curves[i] = PyObject_GetAttrString(s->types[t_curve], curves[i]);
      if (!s->curves[i])
        return -1;
    }
    return 0;
  } catch (PythonError&) {
    return -1;
  } catch (std::bad_alloc&) {
    PyErr_NoMemory();
    return -1;
  }
}

PyModuleDef_Slot slots[] = {{Py_mod_exec, reinterpret_cast<void*>(exec_module)},
                            {0, nullptr}};
PyModuleDef definition = {PyModuleDef_HEAD_INIT,
                          "_core",
                          nullptr,
                          sizeof(State),
                          methods,
                          slots,
                          traverse,
                          clear,
                          free_module};
// Release cached arenas and the selected engine when the extension is unloaded,
// not when an individual interpreter releases its module.
struct Cleanup {
  ~Cleanup() {
    fosu::internal::clear_parser_arena_pool();
    fosu::internal::unload_engine();
  }
};
Cleanup cleanup;
}  // namespace
PyMODINIT_FUNC PyInit__core() {
  return PyModuleDef_Init(&definition);
}
