// Construct detached Python values using the CPython stable ABI.
#include <Python.h>
#include <fosu/engine/loader.h>
#include <fosu/parser.h>
#include <cerrno>
#include <cstring>
#include <new>
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
  f_x,
  f_y,
  f_span_count,
  f_length,
  f_curve_type,
  f_raw_edge_sounds,
  f_raw_edge_sets,
  f_start_time,
  f_end_time,
  f_hit_sound,
  f_raw_type,
  f_raw_end_time,
  f_is_new_combo,
  f_combo_skip,
  f_raw_hit_sample,
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
  f_tags,
  f_bookmarks,
  f_raw_tags,
  f_raw_bookmarks,
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
const char* field_names[] = {"x",
                             "y",
                             "span_count",
                             "length",
                             "curve_type",
                             "raw_edge_sounds",
                             "raw_edge_sets",
                             "start_time",
                             "end_time",
                             "hit_sound",
                             "raw_type",
                             "raw_end_time",
                             "is_new_combo",
                             "combo_skip",
                             "raw_hit_sample",
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
                             "tags",
                             "bookmarks",
                             "raw_tags",
                             "raw_bookmarks",
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
struct State {
  PyObject* model;
  PyObject* names[field_count];
};

struct BeatmapConverter {
  const fosu::Beatmap& map;
  PyObject** keys;
  PythonRef point_type, circle_type, slider_type, spinner_type, hold_type;
  PythonRef timing_type, break_type, stats_type, beatmap_type, sound_type, mode_type;
  PythonRef sample_type, curve_type;
  PythonRef samples[4];
  PythonRef bezier, catmull, linear, perfect_curve;
  PythonRef sounds{PyDict_New()};
  BeatmapConverter(const fosu::Beatmap& map, PyObject* model, PyObject** keys)
      : map(map),
        keys(keys),
        point_type(PyObject_GetAttrString(model, "Point")),
        circle_type(PyObject_GetAttrString(model, "Circle")),
        slider_type(PyObject_GetAttrString(model, "Slider")),
        spinner_type(PyObject_GetAttrString(model, "Spinner")),
        hold_type(PyObject_GetAttrString(model, "HoldNote")),
        timing_type(PyObject_GetAttrString(model, "TimingPoint")),
        break_type(PyObject_GetAttrString(model, "Break")),
        stats_type(PyObject_GetAttrString(model, "ParseStats")),
        beatmap_type(PyObject_GetAttrString(model, "Beatmap")),
        sound_type(PyObject_GetAttrString(model, "HitSound")),
        mode_type(PyObject_GetAttrString(model, "GameMode")),
        sample_type(PyObject_GetAttrString(model, "SampleSet")),
        curve_type(PyObject_GetAttrString(model, "CurveType")),
        samples{PythonRef(PyObject_GetAttrString(sample_type, "NONE")),
                PythonRef(PyObject_GetAttrString(sample_type, "NORMAL")),
                PythonRef(PyObject_GetAttrString(sample_type, "SOFT")),
                PythonRef(PyObject_GetAttrString(sample_type, "DRUM"))},
        bezier(PyObject_GetAttrString(curve_type, "BEZIER")),
        catmull(PyObject_GetAttrString(curve_type, "CATMULL")),
        linear(PyObject_GetAttrString(curve_type, "LINEAR")),
        perfect_curve(PyObject_GetAttrString(curve_type, "PERFECT_CURVE")) {}

  PythonRef sample_set(fosu::SampleSet value) {
    return retain(samples[static_cast<int>(value)]);
  }

  PythonRef curve(fosu::CurveType value) {
    switch (value) {
      case fosu::CurveType::Bezier:
        return retain(bezier);
      case fosu::CurveType::Catmull:
        return retain(catmull);
      case fosu::CurveType::Linear:
        return retain(linear);
      case fosu::CurveType::PerfectCurve:
        return retain(perfect_curve);
    }
    __builtin_unreachable();  // Only validated native enums reach conversion.
  }

  PythonRef make(PyObject* type) {
    // Fixed, plain dataclasses: no custom __new__, __init__, or post-init hooks.
    return PythonRef(PyType_GenericAlloc(reinterpret_cast<PyTypeObject*>(type), 0));
  }
  void set(PyObject* obj, Field field, PythonRef value) {
    if (PyObject_SetAttr(obj, keys[field], value) < 0)
      throw PythonError{};
  }
  PythonRef sound(uint32_t value) {
    PythonRef key = integer(value);
    if (PyObject* found = PyDict_GetItemWithError(sounds, key))
      return retain(found);
    if (PyErr_Occurred())
      throw PythonError{};
    PythonRef result(PyObject_CallFunctionObjArgs(sound_type.p, key.p, nullptr));
    if (PyDict_SetItem(sounds, key, result) < 0)
      throw PythonError{};
    return result;
  }
  PythonRef string(std::string_view s) {
    return PythonRef(
        PyUnicode_DecodeUTF8(s.empty() ? "" : s.data(), s.size(), "surrogateescape"));
  }
  PythonRef point(int x, int y) {
    PythonRef out = make(point_type);
    set(out, f_x, integer(x));
    set(out, f_y, integer(y));
    return out;
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
    PythonRef out = make(circle       ? circle_type.p
                         : slider     ? slider_type.p
                         : h.type & 8 ? spinner_type.p
                                      : hold_type.p);
    set(out, f_start_time, number(h.time));
    set(out, f_end_time, circle ? number(h.time) : slider ? none() : number(h.end_time));
    set(out, f_x, integer(h.x));
    set(out, f_y, integer(h.y));
    set(out, f_hit_sound, sound(h.hitsound));
    set(out, f_raw_type, integer(h.type));
    set(out, f_raw_end_time, number(h.end_time));
    set(out, f_is_new_combo, boolean(h.type & 4));
    set(out, f_combo_skip, integer((h.type >> 4) & 7));
    set(out, f_raw_hit_sample, string(h.hit_sample));
    if (slider) {
      const auto& s = map.sliders[h.slider];
      set(out, f_span_count, integer(s.slides));
      set(out, f_length, number(s.length));
      set(out, f_curve_type, curve(s.curve_type));
      set(out, f_raw_edge_sounds, string(s.edge_sounds));
      set(out, f_raw_edge_sets, string(s.edge_sets));
      set(out, f_control_points, list(s.point_count + 1, [&](size_t j) {
            if (j == 0)
              return point(h.x, h.y);
            const auto& p = map.slider_points[s.point_begin + j - 1];
            return point(p.x, p.y);
          }));
    }
    return out;
  }
  PythonRef timing_point(const fosu::TimingPoint& t) {
    PythonRef out = make(timing_type);
    set(out, f_time, number(t.time));
    set(out, f_beat_length, number(t.beat_length));
    set(out, f_meter, integer(t.meter));
    set(out, f_sample_set, sample_set(t.sample_set));
    set(out, f_sample_index, integer(t.sample_index));
    set(out, f_volume, integer(t.volume));
    set(out, f_uninherited, boolean(t.uninherited));
    set(out, f_effects, integer(t.effects));
    return out;
  }
  PythonRef break_period(const fosu::Break& period) {
    PythonRef out = make(break_type);
    set(out, f_start, number(period.start));
    set(out, f_end, number(period.end));
    return out;
  }
  PythonRef stats() {
    PythonRef out = make(stats_type);
    set(out, f_fast_path_lines, integer(map.stats.fast_path_lines));
    set(out, f_slow_path_lines, integer(map.stats.slow_path_lines));
    set(out, f_malformed_lines, integer(map.stats.malformed_lines));
    set(out, f_storyboard_lines, integer(map.stats.storyboard_lines));
    return out;
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
    PythonRef out = make(beatmap_type);
    const auto& m = map;
    set(out, f_format_version, integer(m.format_version));
    set(out, f_audio_filename, string(m.audio_filename));
    set(out, f_audio_lead_in, integer(m.audio_lead_in));
    set(out, f_preview_time, optional_integer(m.preview_time));
    set(out, f_countdown, integer(m.countdown));
    set(out, f_sample_set, sample_set(m.sample_set));
    set(out, f_stack_leniency, number(m.stack_leniency));
    set(out, f_mode,
        PythonRef(PyObject_CallFunctionObjArgs(mode_type.p, integer(m.mode).p, nullptr)));
    set(out, f_letterbox_in_breaks, boolean(m.letterbox_in_breaks));
    set(out, f_widescreen_storyboard, boolean(m.widescreen_storyboard));
    set(out, f_epilepsy_warning, boolean(m.epilepsy_warning));
    set(out, f_special_style, boolean(m.special_style));
    set(out, f_use_skin_sprites, boolean(m.use_skin_sprites));
    set(out, f_samples_match_playback_rate, boolean(m.samples_match_playback_rate));
    set(out, f_countdown_offset, integer(m.countdown_offset));
    set(out, f_overlay_position, string(m.overlay_position));
    set(out, f_skin_preference, string(m.skin_preference));
    set(out, f_distance_spacing, number(m.distance_spacing));
    set(out, f_beat_divisor, integer(m.beat_divisor));
    set(out, f_grid_size, integer(m.grid_size));
    set(out, f_timeline_zoom, number(m.timeline_zoom));
    set(out, f_title, string(m.title));
    set(out, f_title_unicode, string(m.title_unicode));
    set(out, f_artist, string(m.artist));
    set(out, f_artist_unicode, string(m.artist_unicode));
    set(out, f_creator, string(m.creator));
    set(out, f_version, string(m.version));
    set(out, f_source, string(m.source));
    set(out, f_beatmap_id, optional_integer(m.beatmap_id));
    set(out, f_beatmap_set_id, optional_integer(m.beatmap_set_id));
    set(out, f_hp, number(m.hp));
    set(out, f_cs, number(m.cs));
    set(out, f_od, number(m.od));
    set(out, f_ar, number(m.ar));
    set(out, f_slider_multiplier, number(m.slider_multiplier));
    set(out, f_slider_tick_rate, number(m.slider_tick_rate));
    set(out, f_background, string(m.background));
    set(out, f_video, string(m.video));
    PythonRef tags = string(m.tags), bookmarks = string(m.bookmarks);
    set(out, f_tags, PythonRef(PyUnicode_Split(tags, nullptr, -1)));
    set(out, f_bookmarks, bookmark_list(bookmarks));
    set(out, f_raw_tags, std::move(tags));
    set(out, f_raw_bookmarks, std::move(bookmarks));
    set(out, f_hit_objects, list(map.hit_objects.size(), [&](size_t i) {
          return hit_object(map.hit_objects[i]);
        }));
    set(out, f_timing_points, list(map.timing_points.size(), [&](size_t i) {
          return timing_point(map.timing_points[i]);
        }));
    set(out, f_breaks, list(map.breaks.size(), [&](size_t i) {
          return break_period(map.breaks[i]);
        }));
    set(out, f_stats, stats());
    set(out, f_combo_colours, list(map.combo_colours.size(), [&](size_t i) {
          return integer(map.combo_colours[i]);
        }));
    return out;
  }
};
PyObject* parse_impl(PyObject* module, PyObject* arg, bool file) {
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
    auto result = file ? parser.parse_file(bytes) : parser.parse(bytes, size);
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
    BeatmapConverter converter(*result.value(), state->model, state->names);
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
    {"parse", parse, METH_O, "Parse bytes into a detached dataclass graph."},
    {"parse_file", parse_file, METH_O,
     "Read and parse a file into a detached dataclass graph."},
    {nullptr, nullptr, 0, nullptr}};
int traverse(PyObject* m, visitproc visit, void* arg) {
  auto* s = static_cast<State*>(PyModule_GetState(m));
  if (!s)
    return 0;
  Py_VISIT(s->model);
  for (auto* name : s->names) {
    Py_VISIT(name);
  }
  return 0;
}
int clear(PyObject* m) {
  auto* s = static_cast<State*>(PyModule_GetState(m));
  if (!s)
    return 0;
  Py_CLEAR(s->model);
  for (auto*& name : s->names) {
    Py_CLEAR(name);
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
    for (int i = 0; i < field_count; ++i) {
      s->names[i] = PyUnicode_InternFromString(field_names[i]);
      if (!s->names[i])
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
