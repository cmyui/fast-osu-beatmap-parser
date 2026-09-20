// Construct detached Python values using the CPython stable ABI.
#include <fosu/beatmap.h>
#include <fosu/bindings/records.h>
#include <fosu/compiler.h>
#include <fosu/engine/runtime/loader.h>
#include <fosu/enums.h>
#include <fosu/mods.h>
#include <fosu/parse_options.h>
#include <fosu/parser.h>
#include <fosu/result.h>
#include <fosu/slider_event.h>
#include <fosu/slider_path.h>
#include <fosu/types.h>

#include <Python.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <structmember.h>
#include <utility>
#include <vector>

namespace {
using fosu::f32;
using fosu::f64;
using fosu::i32;
using fosu::i64;
using fosu::u32;

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
struct PauseGC {
  int was_enabled = PyGC_Disable();
  ~PauseGC() {
    if (was_enabled)
      PyGC_Enable();
  }
};
PythonRef integer(long long n) {
  return PythonRef(PyLong_FromLongLong(n));
}
PythonRef number(f64 n) {
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

// NumberStyles.Integer uses ASCII digits, a sign/ASCII whitespace, and int32
// range. The .NET parser also accepts trailing NULs after whitespace.
bool bookmark(const char* p, const char* end, i32& out) {
  auto space = [](unsigned char c) {
    return c == ' ' || (c >= 9 && c <= 13);
  };
  while (p != end && space(*p))
    ++p;
  bool negative = false;
  if (p != end && (*p == '+' || *p == '-'))
    negative = *p++ == '-';
  const char* digits = p;
  u32         value = 0, limit = negative ? 2147483648u : 2147483647u;
  while (p != end && *p >= '0' && *p <= '9') {
    u32 digit = static_cast<unsigned>(*p++ - '0');
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
  out = static_cast<i32>(negative ? -static_cast<i64>(value) : value);
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
  f_curve_segments,
  f_degree,
  f_sample_volume,
  f_velocity_presets,
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
                             "storyboard_lines",
                             "curve_segments",
                             "degree",
                             "sample_volume",
                             "velocity_presets"};
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
  t_curve_segment,
  t_event_type,
  t_sound,
  t_mode,
  t_sample,
  t_curve,
  type_count
};
constexpr int         record_type_count = t_curve_segment + 1;
constexpr const char* type_names[] = {
    "Point",     "Circle",       "Slider",          "Spinner",
    "HoldNote",  "TimingPoint",  "Break",           "ParseStats",
    "PathPoint", "SliderPath",   "Beatmap",         "SliderEvent",
    "Stacking",  "CurveSegment", "SliderEventType", "HitSound",
    "GameMode",  "SampleSet",    "CurveType"};
struct State {
  PyObject*  model;
  PyObject*  types[type_count];
  Py_ssize_t slots[record_type_count][field_count];
  Py_ssize_t record_sizes[record_type_count];
  PyObject*  hit_object_base;
  PyObject*  sounds[16];
  PyObject*  samples[4];
  PyObject*  curves[4];
  PyObject*  event_types[5];
  PyObject*  coordinates[513];
  PyObject*  bookmark_whitespace;
};

PyObject* make_record(PyObject* module, PyObject* args) {
  const char* name;
  PyObject*   fields;
  if (!PyArg_ParseTuple(args, "sO:_record", &name, &fields))
    return nullptr;
  if (!PyTuple_Check(fields)) {
    PyErr_SetString(PyExc_TypeError, "fields must be a tuple");
    return nullptr;
  }
  constexpr const char* names[] = {
      "fosu._model.Point",     "fosu._model.Circle",
      "fosu._model.Slider",    "fosu._model.Spinner",
      "fosu._model.HoldNote",  "fosu._model.TimingPoint",
      "fosu._model.Break",     "fosu._model.ParseStats",
      "fosu._model.PathPoint", "fosu._model.SliderPath",
      "fosu._model.Beatmap",   "fosu._model.SliderEvent",
      "fosu._model.Stacking",  "fosu._model.CurveSegment",
      "fosu._model._HitObject"};
  int kind = 0;
  while (kind < record_type_count && std::strcmp(name, type_names[kind]) != 0)
    ++kind;
  if (kind == record_type_count && std::strcmp(name, "_HitObject") != 0) {
    PyErr_SetString(PyExc_TypeError, "unknown FOSU record");
    return nullptr;
  }
  auto*      state = static_cast<State*>(PyModule_GetState(module));
  PyObject*& type =
      kind == record_type_count ? state->hit_object_base : state->types[kind];
  if (type) {
    PyErr_SetString(PyExc_TypeError, "record already defined");
    return nullptr;
  }
  Py_ssize_t count = PyTuple_Size(fields);
  if (count > field_count) {
    PyErr_SetString(PyExc_TypeError, "too many record fields");
    return nullptr;
  }
  try {
    std::vector<PyMemberDef> members(count + 1);
    for (Py_ssize_t i = 0; i < count; ++i) {
      PyObject* field = PyTuple_GetItem(fields, i);
      if (!PyUnicode_Check(field)) {
        PyErr_SetString(PyExc_TypeError, "field names must be strings");
        return nullptr;
      }
      int index = 0;
      for (; index < field_count; ++index) {
        int equal = PyUnicode_CompareWithASCIIString(field, field_names[index]);
        if (equal == -1 && PyErr_Occurred())
          return nullptr;
        if (equal == 0)
          break;
      }
      if (index == field_count) {
        PyErr_SetString(PyExc_TypeError, "unknown record field");
        return nullptr;
      }
      for (Py_ssize_t j = 0; j < i; ++j) {
        if (members[j].name == field_names[index]) {
          PyErr_SetString(PyExc_TypeError, "duplicate record field");
          return nullptr;
        }
      }
      members[i] = {
          field_names[index], T_OBJECT_EX,
          static_cast<Py_ssize_t>(sizeof(RecordObject) + i * sizeof(PyObject*)),
          READONLY, nullptr};
    }
    PyType_Slot slots[] = {
        {Py_tp_new, reinterpret_cast<void*>(record_new)},
        {Py_tp_repr, reinterpret_cast<void*>(record_repr)},
        {Py_tp_richcompare, reinterpret_cast<void*>(record_equal)},
        {Py_tp_hash, reinterpret_cast<void*>(PyObject_HashNotImplemented)},
        {Py_tp_methods, record_methods},
        {Py_tp_dealloc, reinterpret_cast<void*>(record_dealloc)},
        {Py_tp_traverse, reinterpret_cast<void*>(record_traverse)},
        {Py_tp_clear, reinterpret_cast<void*>(record_clear)},
        {Py_tp_members, members.data()},
        {0, nullptr}};
    PyType_Spec spec = {
        names[kind],
        static_cast<int>(sizeof(RecordObject) + count * sizeof(PyObject*)), 0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, slots};
    if (kind == record_type_count)
      spec.flags |= Py_TPFLAGS_BASETYPE;
    PythonRef bases(PyTuple_New(kind >= t_circle && kind <= t_hold ? 1 : 0));
    if (kind >= t_circle && kind <= t_hold) {
      if (!state->hit_object_base) {
        PyErr_SetString(PyExc_TypeError, "hit-object base is not defined");
        return nullptr;
      }
      const auto* base = record_members(
          reinterpret_cast<PyTypeObject*>(state->hit_object_base));
      for (Py_ssize_t i = 0; base[i].name; ++i) {
        if (i >= count || base[i].name != members[i].name) {
          PyErr_SetString(PyExc_TypeError,
                          "hit-object fields must preserve the base layout");
          return nullptr;
        }
      }
      if (PyTuple_SetItem(bases, 0, Py_NewRef(state->hit_object_base)) < 0)
        return nullptr;
    }
    // CPython copies the definitions. Their names refer to static strings.
    type = PyType_FromSpecWithBases(
        &spec, kind >= t_circle && kind <= t_hold ? bases.p : nullptr);
    if (!type)
      return nullptr;
    if (kind < record_type_count) {
      state->record_sizes[kind] = count;
      for (int field = 0; field < field_count; ++field)
        state->slots[kind][field] = -1;
      for (Py_ssize_t i = 0; i < count; ++i) {
        for (int field = 0; field < field_count; ++field) {
          if (members[i].name == field_names[field])
            state->slots[kind][field] = i;
        }
      }
    }
    return Py_NewRef(type);
  } catch (const PythonError&) {
    return nullptr;
  } catch (const std::bad_alloc&) {
    return PyErr_NoMemory();
  }
}

PyObject* restore_record(PyObject* module, PyObject* type) {
  const auto* state = static_cast<State*>(PyModule_GetState(module));
  for (int kind = 0; kind < record_type_count; ++kind) {
    if (type == state->types[kind])
      return allocate_record(reinterpret_cast<PyTypeObject*>(type),
                             state->record_sizes[kind]);
  }
  PyErr_SetString(PyExc_TypeError, "expected a FOSU record type");
  return nullptr;
}

struct BeatmapConverter {
  const fosu::Beatmap& map;
  const State&         state;
  PythonRef            default_hit_sample;
  BeatmapConverter(const fosu::Beatmap& map, const State& state)
      : map(map),
        state(state),
        default_hit_sample(
            PyUnicode_DecodeUTF8("0:0:0:0:", 8, "surrogateescape")) {}

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
    FOSU_UNREACHABLE();  // Only validated native enums reach conversion.
  }

  struct Value {
    Field     field;
    PythonRef value;
  };
  PythonRef allocate(PythonType kind) {
    return PythonRef(
        allocate_record(reinterpret_cast<PyTypeObject*>(state.types[kind]),
                        state.record_sizes[kind]));
  }
  void set_field(PyObject*  object,
                 PythonType kind,
                 Field      field,
                 PythonRef  value) {
    const auto index = state.slots[kind][field];
    if (index < 0) {
      PyErr_SetString(PyExc_TypeError, "record is missing an expected field");
      throw PythonError{};
    }
    record_fields(object)[index] = value.release();
  }
  PythonRef record(PythonType kind, std::span<Value> values) {
    PythonRef out = allocate(kind);
    for (auto& value : values)
      set_field(out, kind, value.field, std::move(value.value));
    return out;
  }
  PythonRef sound(u32 value) {
    if (value < 16)
      return retain(state.sounds[value]);
    // Preserve unknown bits supported by HitSound's IntFlag contract.
    return PythonRef(PyObject_CallFunction(state.types[t_sound], "I", value));
  }
  PythonRef string(std::string_view s) {
    return PythonRef(PyUnicode_DecodeUTF8(s.empty() ? "" : s.data(), s.size(),
                                          "surrogateescape"));
  }
  PythonRef coordinate(f32 value) {
    if (value >= 0 && value <= 512 && !std::signbit(value)) {
      auto index = static_cast<u32>(value);
      if (value == static_cast<f32>(index))
        return retain(state.coordinates[index]);
    }
    return number(value);
  }
  PythonRef point(f32 x, f32 y) {
    PythonRef out = allocate(t_point);
    set_field(out, t_point, f_x, coordinate(x));
    set_field(out, t_point, f_y, coordinate(y));
    return out;
  }
  PythonRef path_point(f64 x, f64 y) {
    PythonRef out = allocate(t_path_point);
    set_field(out, t_path_point, f_x, number(x));
    set_field(out, t_path_point, f_y, number(y));
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
    const auto kind = h.type & 1   ? t_circle
                      : h.type & 2 ? t_slider
                      : h.type & 8 ? t_spinner
                                   : t_hold;
    PythonRef  out = allocate(kind);
    PythonRef  time = number(h.time);
    set_field(out, kind, f_time, retain(time));
    set_field(out, kind, f_end_time,
              kind == t_circle ? std::move(time) : number(h.end_time));
    set_field(out, kind, f_x, coordinate(h.x));
    set_field(out, kind, f_y, coordinate(h.y));
    set_field(out, kind, f_hitsound, sound(h.hitsound));
    set_field(out, kind, f_type, integer(h.type));
    set_field(out, kind, f_new_combo, boolean(h.new_combo));
    set_field(out, kind, f_combo_skip, integer(h.combo_skip));
    set_field(out, kind, f_hit_sample,
              h.hit_sample == "0:0:0:0:" ? retain(default_hit_sample)
                                         : string(h.hit_sample));
    set_field(out, kind, f_stacking, stacking(h));
    if (kind == t_slider) {
      const auto& s = map.sliders[h.slider];
      set_field(out, kind, f_slides, integer(s.slides));
      set_field(out, kind, f_events, slider_events(h.slider));
      set_field(out, kind, f_path,
                map.slider_paths.empty()
                    ? none()
                    : slider_path(map.slider_paths[h.slider]));
      set_field(out, kind, f_length, number(s.length));
      set_field(out, kind, f_curve_type, curve(s.curve_type));
      set_field(out, kind, f_curve_segments, curve_segments(h, s));
      set_field(out, kind, f_edge_sounds, string(s.edge_sounds));
      set_field(out, kind, f_edge_sets, string(s.edge_sets));
      set_field(out, kind, f_control_points,
                list(s.point_count + 1, [&](size_t j) {
                  if (j == 0)
                    return point(h.x, h.y);
                  const auto& p = map.slider_points[s.point_begin + j - 1];
                  return point(p.x, p.y);
                }));
    }
    return out;
  }
  PythonRef curve_segments(const fosu::HitObject& object,
                           const fosu::Slider&    slider) {
    return list(slider.segment_count, [&](size_t i) {
      const auto& segment = map.slider_segments[slider.segment_begin + i];
      const bool  head = i == 0;
      Value       values[] = {
          {f_type, curve(segment.type)},
          {f_degree, segment.degree ? integer(*segment.degree) : none()},
          {f_control_points, list(segment.point_count + head, [&](size_t j) {
             if (head && !j)
               return point(object.x, object.y);
             const auto& value =
                 map.slider_points[slider.point_begin + segment.point_begin +
                                   j - head];
             return point(value.x, value.y);
           })}};
      return record(t_curve_segment, values);
    });
  }

  PythonRef timing_point(const fosu::TimingPoint& t) {
    PythonRef out = allocate(t_timing);
    set_field(out, t_timing, f_time, number(t.time));
    set_field(out, t_timing, f_beat_length, number(t.beat_length));
    set_field(out, t_timing, f_meter, integer(t.meter));
    set_field(out, t_timing, f_sample_set, sample_set(t.sample_set));
    set_field(out, t_timing, f_sample_index, integer(t.sample_index));
    set_field(out, t_timing, f_volume, integer(t.volume));
    set_field(out, t_timing, f_uninherited, boolean(t.uninherited));
    set_field(out, t_timing, f_effects, integer(t.effects));
    return out;
  }
  PythonRef stacking(const fosu::HitObject& object) {
    if (map.stacking.empty())
      return none();
    const auto& value = map.stacking[&object - map.hit_objects.data()];
    Value       values[] = {
        {f_stack_height, integer(value.stack_height)},
        {f_stack_offset,
         path_point(value.stack_offset.x, value.stack_offset.y)},
    };
    return record(t_stacking, values);
  }
  PythonRef slider_events(size_t index) {
    const auto events = map.slider_events.empty()
                            ? std::span<fosu::SliderEvent>{}
                            : map.slider_events[index];
    return list(events.size(), [&](size_t i) {
      const auto& e = events[i];
      Value       values[] = {
          {f_type, retain(state.event_types[static_cast<int>(e.type)])},
          {f_time, number(e.time)},
          {f_span_index, integer(e.span_index)},
          {f_span_start_time, number(e.span_start_time)},
          {f_path_progress, number(e.path_progress)},
          {f_position, path_point(e.position.x, e.position.y)},
      };
      return record(t_event, values);
    });
  }
  PythonRef slider_path(const fosu::SliderPath& path) {
    Value values[] = {
        {f_points, list(path.points.size(),
                        [&](size_t i) {
                          return path_point(path.points[i].x, path.points[i].y);
                        })},
        {f_cumulative_lengths, list(path.cumulative_lengths.size(),
                                    [&](size_t i) {
                                      return number(path.cumulative_lengths[i]);
                                    })},
    };
    return record(t_path, values);
  }
  PythonRef break_period(const fosu::Break& period) {
    Value values[] = {{f_start, number(period.start)},
                      {f_end, number(period.end)}};
    return record(t_break, values);
  }
  PythonRef stats() {
    Value values[] = {
        {f_fast_path_lines, integer(map.stats.fast_path_lines)},
        {f_slow_path_lines, integer(map.stats.slow_path_lines)},
        {f_malformed_lines, integer(map.stats.malformed_lines)},
        {f_storyboard_lines, integer(map.stats.storyboard_lines)}};
    return record(t_stats, values);
  }
  PythonRef bookmark_list(PyObject* bookmarks) {
    PythonRef marks(PyList_New(0));
    // osu's legacy decoder uses invariant int.TryParse and skips invalid
    // tokens. SplitKeyVal trims .NET whitespace around the complete field
    // first.
    PythonRef trimmed(PyObject_CallMethod(bookmarks, "strip", "O",
                                          state.bookmark_whitespace));
    PythonRef encoded(
        PyUnicode_AsEncodedString(trimmed, "utf-8", "surrogateescape"));
    char*      bookmark_data;
    Py_ssize_t bookmark_size;
    if (PyBytes_AsStringAndSize(encoded, &bookmark_data, &bookmark_size) < 0)
      throw PythonError{};
    const char* begin = bookmark_data;
    const char* end = begin + bookmark_size;
    while (begin != end) {
      const char* stop = begin;
      while (stop != end && *stop != ',')
        ++stop;
      i32 value;
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
    PythonRef   tags = string(m.tags), bookmarks = string(m.bookmarks);
    Value       values[] = {
        {f_format_version, integer(m.format_version)},
        {f_audio_filename, string(m.audio_filename)},
        {f_audio_lead_in, integer(m.audio_lead_in)},
        {f_preview_time, optional_integer(m.preview_time)},
        {f_countdown, integer(m.countdown)},
        {f_sample_set, sample_set(m.sample_set)},
        {f_sample_volume, integer(m.sample_volume)},
        {f_stack_leniency, number(m.stack_leniency)},
        {f_mode, PythonRef(PyObject_CallFunctionObjArgs(
                     state.types[t_mode], integer(m.mode).p, nullptr))},
        {f_letterbox_in_breaks, boolean(m.letterbox_in_breaks)},
        {f_widescreen_storyboard, boolean(m.widescreen_storyboard)},
        {f_epilepsy_warning, boolean(m.epilepsy_warning)},
        {f_special_style, boolean(m.special_style)},
        {f_use_skin_sprites, boolean(m.use_skin_sprites)},
        {f_samples_match_playback_rate, boolean(m.samples_match_playback_rate)},
        {f_countdown_offset, integer(m.countdown_offset)},
        {f_overlay_position, string(m.overlay_position)},
        {f_skin_preference, string(m.skin_preference)},
        {f_velocity_presets, list(m.velocity_presets.size(),
                                  [&](size_t i) {
                                    return number(m.velocity_presets[i]);
                                  })},
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
         })}};
    return record(t_beatmap, values);
  }
};
PyObject* parse_impl(PyObject* module, PyObject* args, bool file) {
  PyObject*     arg;
  long          sections;
  int           calculate_slider_end_times;
  int           calculate_slider_paths;
  int           calculate_slider_events;
  int           apply_stacking;
  unsigned long mods;
  if (!PyArg_ParseTuple(args, "Olppppk", &arg, &sections,
                        &calculate_slider_end_times, &calculate_slider_paths,
                        &calculate_slider_events, &apply_stacking, &mods))
    return nullptr;
  if (sections < 0 ||
      (static_cast<unsigned long>(sections) &
       ~static_cast<unsigned long>(fosu::kAllSections)) ||
      mods > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "invalid parse options");
    return nullptr;
  }
  const fosu::ParseOptions options{
      .sections = static_cast<u32>(sections),
      .calculate_slider_end_times = calculate_slider_end_times != 0,
      .calculate_slider_paths = calculate_slider_paths != 0,
      .calculate_slider_events = calculate_slider_events != 0,
      .apply_stacking = apply_stacking != 0,
      .mods = static_cast<fosu::Mods>(mods),
  };
  try {
    PythonRef input = file ? PythonRef(PyOS_FSPath(arg)) : retain(arg);
    if (file && PyUnicode_Check(input))
      input = PythonRef(PyUnicode_EncodeFSDefault(input));
    char*      bytes;
    Py_ssize_t size;
    if (PyBytes_AsStringAndSize(input, &bytes, &size) < 0)
      throw PythonError{};
    if (file && std::memchr(bytes, 0, size)) {
      PyErr_SetString(PyExc_ValueError, "embedded null byte");
      throw PythonError{};
    }
    const auto*    engine = fosu::internal::selected_engine();
    // Backend selection was checked when the module was imported.
    fosu::Parser   parser(*engine);
    // Both native entry points are noexcept, including allocation and I/O
    // failures.
    PyThreadState* thread = PyEval_SaveThread();
    auto           result = file ? parser.parse_file(bytes, options)
                                 : parser.parse(bytes, size, options);
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
          PyErr_SetString(
              PyExc_ValueError,
              "beatmap input cannot fit in the process address space");
          break;
        case fosu::ErrorCode::InvalidInput:
          PyErr_SetString(PyExc_ValueError, "invalid input");
          break;
      }
      throw PythonError{};
    }
    auto*            state = static_cast<State*>(PyModule_GetState(module));
    PauseGC          pause_gc;
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
    {"_record", make_record, METH_VARARGS, nullptr},
    {"_restore_record", restore_record, METH_O, nullptr},
    {"parse", parse, METH_VARARGS,
     "Parse bytes into detached eager Python records."},
    {"parse_file", parse_file, METH_VARARGS,
     "Read and parse a file into detached eager Python records."},
    {nullptr, nullptr, 0, nullptr}};
int traverse(PyObject* m, visitproc visit, void* arg) {
  auto* s = static_cast<State*>(PyModule_GetState(m));
  if (!s)
    return 0;
  Py_VISIT(s->model);
  for (auto* type : s->types) {
    Py_VISIT(type);
  }
  Py_VISIT(s->hit_object_base);
  for (auto* sound : s->sounds) {
    Py_VISIT(sound);
  }
  for (auto* sample : s->samples) {
    Py_VISIT(sample);
  }
  for (auto* curve : s->curves) {
    Py_VISIT(curve);
  }
  for (auto* type : s->event_types) {
    Py_VISIT(type);
  }
  for (auto* coordinate : s->coordinates) {
    Py_VISIT(coordinate);
  }
  Py_VISIT(s->bookmark_whitespace);
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
  Py_CLEAR(s->hit_object_base);
  for (auto*& sound : s->sounds) {
    Py_CLEAR(sound);
  }
  for (auto*& sample : s->samples) {
    Py_CLEAR(sample);
  }
  for (auto*& curve : s->curves) {
    Py_CLEAR(curve);
  }
  for (auto*& type : s->event_types) {
    Py_CLEAR(type);
  }
  for (auto*& coordinate : s->coordinates) {
    Py_CLEAR(coordinate);
  }
  Py_CLEAR(s->bookmark_whitespace);
  return 0;
}
void free_module(void* m) {
  clear(static_cast<PyObject*>(m));
}
int exec_module(PyObject* m) {
  try {
    const auto* engine = fosu::internal::selected_engine();
    if (!engine) {
      PyErr_SetString(PyExc_ImportError,
                      "FOSU_BACKEND requests an unavailable backend");
      return -1;
    }
    if (PyModule_AddStringConstant(
            m, "backend", fosu::internal::engine_name(engine->kind)) < 0)
      return -1;
    auto*     s = static_cast<State*>(PyModule_GetState(m));
    PythonRef package(PyObject_GetAttrString(m, "__package__"));
    PythonRef name(PyUnicode_FromFormat("%U._model", package.p));
    s->model = PyImport_Import(name);
    if (!s->model)
      return -1;
    for (int i = 0; i < type_count; ++i) {
      if (i < record_type_count)
        continue;
      s->types[i] = PyObject_GetAttrString(s->model, type_names[i]);
      if (!s->types[i])
        return -1;
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
    constexpr const char* curves[] = {"BEZIER", "CATMULL", "LINEAR",
                                      "PERFECT_CURVE"};
    for (int i = 0; i < 4; ++i) {
      s->curves[i] = PyObject_GetAttrString(s->types[t_curve], curves[i]);
      if (!s->curves[i])
        return -1;
    }
    for (int i = 0; i <= 512; ++i) {
      s->coordinates[i] = PyFloat_FromDouble(i);
      if (!s->coordinates[i])
        return -1;
    }
    for (int i = 0; i < 5; ++i) {
      s->event_types[i] = PyObject_CallFunction(s->types[t_event_type], "i", i);
      if (!s->event_types[i])
        return -1;
    }
    constexpr wchar_t bookmark_whitespace[] =
        L"\t\n\v\f\r \u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004"
        L"\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";
    s->bookmark_whitespace = PyUnicode_FromWideChar(
        bookmark_whitespace,
        static_cast<Py_ssize_t>(sizeof(bookmark_whitespace) / sizeof(wchar_t) -
                                1));
    if (!s->bookmark_whitespace)
      return -1;
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
PyModuleDef      definition = {PyModuleDef_HEAD_INIT,
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
