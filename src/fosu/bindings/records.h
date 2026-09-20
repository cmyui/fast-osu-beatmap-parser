#pragma once

#include <Python.h>
#include <cstddef>
#include <structmember.h>

namespace {
PyObject* record_values(PyObject* object, const PyMemberDef* members) {
  Py_ssize_t count = 0;
  while (members[count].name)
    ++count;
  PyObject* values = PyTuple_New(count);
  if (!values)
    return nullptr;
  for (Py_ssize_t i = 0; i < count; ++i) {
    auto* value = *reinterpret_cast<PyObject**>(
        reinterpret_cast<char*>(object) + members[i].offset);
    Py_XINCREF(value);
    if (!value || PyTuple_SetItem(values, i, value) < 0) {
      Py_DECREF(values);
      if (!value)
        PyErr_SetString(PyExc_AttributeError, "record field is unset");
      return nullptr;
    }
  }
  return values;
}
PyObject* record_equal(PyObject*          left,
                       PyObject*          right,
                       int                op,
                       const PyMemberDef* members) {
  if (Py_TYPE(left) != Py_TYPE(right) || (op != Py_EQ && op != Py_NE)) {
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
  }
  PyObject* a = record_values(left, members);
  if (!a)
    return nullptr;
  PyObject* b = record_values(right, members);
  PyObject* result = b ? PyObject_RichCompare(a, b, op) : nullptr;
  Py_DECREF(a);
  Py_XDECREF(b);
  return result;
}
PyObject* record_reduce(PyObject* object, const PyMemberDef* members) {
  PyObject* values = record_values(object, members);
  if (!values)
    return nullptr;
  PyObject* result =
      PyTuple_Pack(2, reinterpret_cast<PyObject*>(Py_TYPE(object)), values);
  Py_DECREF(values);
  return result;
}
PyObject* record_repr(PyObject*          object,
                      const char*        name,
                      const PyMemberDef* members) {
  int entered = Py_ReprEnter(object);
  if (entered < 0)
    return nullptr;
  if (entered)
    return PyUnicode_FromString("...");
  PyObject* fields = PyList_New(0);
  PyObject* separator = nullptr;
  PyObject* joined = nullptr;
  PyObject* result = nullptr;
  if (!fields)
    goto done;
  for (const auto* member = members; member->name; ++member) {
    auto* value = *reinterpret_cast<PyObject**>(
        reinterpret_cast<char*>(object) + member->offset);
    PyObject* field =
        PyUnicode_FromFormat("%s=%R", member->name, value ? value : Py_None);
    if (!field)
      goto done;
    int added = PyList_Append(fields, field);
    Py_DECREF(field);
    if (added < 0)
      goto done;
  }
  separator = PyUnicode_FromString(", ");
  if (!separator)
    goto done;
  joined = PyUnicode_Join(separator, fields);
  if (joined)
    result = PyUnicode_FromFormat("%s(%U)", name, joined);
done:
  Py_XDECREF(joined);
  Py_XDECREF(separator);
  Py_XDECREF(fields);
  Py_ReprLeave(object);
  return result;
}

struct CircleObject {
  // clang-format off
  PyObject_HEAD
  PyObject* time;
  PyObject* x;
  PyObject* y;
  PyObject* hitsound;
  PyObject* type;
  PyObject* new_combo;
  PyObject* combo_skip;
  PyObject* hit_sample;
  PyObject* stacking;
  PyObject* end_time;
  // clang-format on
};
int circle_traverse(PyObject* object, visitproc visit, void* arg) {
  auto* circle = reinterpret_cast<CircleObject*>(object);
  Py_VISIT(Py_TYPE(object));
  Py_VISIT(circle->time);
  Py_VISIT(circle->x);
  Py_VISIT(circle->y);
  Py_VISIT(circle->hitsound);
  Py_VISIT(circle->type);
  Py_VISIT(circle->new_combo);
  Py_VISIT(circle->combo_skip);
  Py_VISIT(circle->hit_sample);
  Py_VISIT(circle->stacking);
  Py_VISIT(circle->end_time);
  return 0;
}
int circle_clear(PyObject* object) {
  auto* circle = reinterpret_cast<CircleObject*>(object);
  Py_CLEAR(circle->time);
  Py_CLEAR(circle->x);
  Py_CLEAR(circle->y);
  Py_CLEAR(circle->hitsound);
  Py_CLEAR(circle->type);
  Py_CLEAR(circle->new_combo);
  Py_CLEAR(circle->combo_skip);
  Py_CLEAR(circle->hit_sample);
  Py_CLEAR(circle->stacking);
  Py_CLEAR(circle->end_time);
  return 0;
}
void circle_dealloc(PyObject* object) {
  PyObject_GC_UnTrack(object);
  circle_clear(object);
  PyTypeObject* type = Py_TYPE(object);
  PyObject_GC_Del(object);
  Py_DECREF(type);
}
PyObject* circle_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
  static const char* names[] = {
      "time",       "x",          "y",        "hitsound", "type", "new_combo",
      "combo_skip", "hit_sample", "stacking", "end_time", nullptr};
  PyObject *time, *x, *y, *hitsound, *kind, *new_combo, *combo_skip,
      *hit_sample, *stacking, *end_time;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOOOOOOOO:Circle",
                                   const_cast<char**>(names), &time, &x, &y,
                                   &hitsound, &kind, &new_combo, &combo_skip,
                                   &hit_sample, &stacking, &end_time))
    return nullptr;
  auto* object = reinterpret_cast<CircleObject*>(PyType_GenericAlloc(type, 0));
  if (!object)
    return nullptr;
  object->time = Py_NewRef(time);
  object->x = Py_NewRef(x);
  object->y = Py_NewRef(y);
  object->hitsound = Py_NewRef(hitsound);
  object->type = Py_NewRef(kind);
  object->new_combo = Py_NewRef(new_combo);
  object->combo_skip = Py_NewRef(combo_skip);
  object->hit_sample = Py_NewRef(hit_sample);
  object->stacking = Py_NewRef(stacking);
  object->end_time = Py_NewRef(end_time);
  return reinterpret_cast<PyObject*>(object);
}
PyMemberDef circle_members[] = {
    {"time", T_OBJECT_EX, offsetof(CircleObject, time), READONLY, nullptr},
    {"x", T_OBJECT_EX, offsetof(CircleObject, x), READONLY, nullptr},
    {"y", T_OBJECT_EX, offsetof(CircleObject, y), READONLY, nullptr},
    {"hitsound", T_OBJECT_EX, offsetof(CircleObject, hitsound), READONLY,
     nullptr},
    {"type", T_OBJECT_EX, offsetof(CircleObject, type), READONLY, nullptr},
    {"new_combo", T_OBJECT_EX, offsetof(CircleObject, new_combo), READONLY,
     nullptr},
    {"combo_skip", T_OBJECT_EX, offsetof(CircleObject, combo_skip), READONLY,
     nullptr},
    {"hit_sample", T_OBJECT_EX, offsetof(CircleObject, hit_sample), READONLY,
     nullptr},
    {"stacking", T_OBJECT_EX, offsetof(CircleObject, stacking), READONLY,
     nullptr},
    {"end_time", T_OBJECT_EX, offsetof(CircleObject, end_time), READONLY,
     nullptr},
    {nullptr, 0, 0, 0, nullptr}};
PyObject* circle_repr(PyObject* object) {
  return record_repr(object, "Circle", circle_members);
}
PyObject* circle_equal(PyObject* a, PyObject* b, int op) {
  return record_equal(a, b, op, circle_members);
}
PyObject* circle_reduce(PyObject* object, PyObject*) {
  return record_reduce(object, circle_members);
}
PyMethodDef circle_methods[] = {
    {"__reduce__", circle_reduce, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyType_Slot circle_slots[] = {
    {Py_tp_new, reinterpret_cast<void*>(circle_new)},
    {Py_tp_repr, reinterpret_cast<void*>(circle_repr)},
    {Py_tp_richcompare, reinterpret_cast<void*>(circle_equal)},
    {Py_tp_hash, reinterpret_cast<void*>(PyObject_HashNotImplemented)},
    {Py_tp_methods, circle_methods},
    {Py_tp_dealloc, reinterpret_cast<void*>(circle_dealloc)},
    {Py_tp_traverse, reinterpret_cast<void*>(circle_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(circle_clear)},
    {Py_tp_members, circle_members},
    {0, nullptr}};
PyType_Spec circle_spec = {"fosu._core.Circle", sizeof(CircleObject), 0,
                           Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
                           circle_slots};

struct TimingPointObject {
  // clang-format off
  PyObject_HEAD
  PyObject* time;
  PyObject* beat_length;
  PyObject* meter;
  PyObject* sample_set;
  PyObject* sample_index;
  PyObject* volume;
  PyObject* uninherited;
  PyObject* effects;
  // clang-format on
};
int timing_point_traverse(PyObject* object, visitproc visit, void* arg) {
  auto* point = reinterpret_cast<TimingPointObject*>(object);
  Py_VISIT(Py_TYPE(object));
  Py_VISIT(point->time);
  Py_VISIT(point->beat_length);
  Py_VISIT(point->meter);
  Py_VISIT(point->sample_set);
  Py_VISIT(point->sample_index);
  Py_VISIT(point->volume);
  Py_VISIT(point->uninherited);
  Py_VISIT(point->effects);
  return 0;
}
int timing_point_clear(PyObject* object) {
  auto* point = reinterpret_cast<TimingPointObject*>(object);
  Py_CLEAR(point->time);
  Py_CLEAR(point->beat_length);
  Py_CLEAR(point->meter);
  Py_CLEAR(point->sample_set);
  Py_CLEAR(point->sample_index);
  Py_CLEAR(point->volume);
  Py_CLEAR(point->uninherited);
  Py_CLEAR(point->effects);
  return 0;
}
void timing_point_dealloc(PyObject* object) {
  PyObject_GC_UnTrack(object);
  timing_point_clear(object);
  PyTypeObject* type = Py_TYPE(object);
  PyObject_GC_Del(object);
  Py_DECREF(type);
}
PyObject* timing_point_new(PyTypeObject* type,
                           PyObject*     args,
                           PyObject*     kwargs) {
  static const char* names[] = {"time",        "beat_length",  "meter",
                                "sample_set",  "sample_index", "volume",
                                "uninherited", "effects",      nullptr};
  PyObject *time, *beat_length, *meter, *sample_set, *sample_index, *volume,
      *uninherited, *effects;
  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "OOOOOOOO:TimingPoint", const_cast<char**>(names),
          &time, &beat_length, &meter, &sample_set, &sample_index, &volume,
          &uninherited, &effects))
    return nullptr;
  auto* object =
      reinterpret_cast<TimingPointObject*>(PyType_GenericAlloc(type, 0));
  if (!object)
    return nullptr;
  object->time = Py_NewRef(time);
  object->beat_length = Py_NewRef(beat_length);
  object->meter = Py_NewRef(meter);
  object->sample_set = Py_NewRef(sample_set);
  object->sample_index = Py_NewRef(sample_index);
  object->volume = Py_NewRef(volume);
  object->uninherited = Py_NewRef(uninherited);
  object->effects = Py_NewRef(effects);
  return reinterpret_cast<PyObject*>(object);
}
PyMemberDef timing_point_members[] = {
    {"time", T_OBJECT_EX, offsetof(TimingPointObject, time), READONLY, nullptr},
    {"beat_length", T_OBJECT_EX, offsetof(TimingPointObject, beat_length),
     READONLY, nullptr},
    {"meter", T_OBJECT_EX, offsetof(TimingPointObject, meter), READONLY,
     nullptr},
    {"sample_set", T_OBJECT_EX, offsetof(TimingPointObject, sample_set),
     READONLY, nullptr},
    {"sample_index", T_OBJECT_EX, offsetof(TimingPointObject, sample_index),
     READONLY, nullptr},
    {"volume", T_OBJECT_EX, offsetof(TimingPointObject, volume), READONLY,
     nullptr},
    {"uninherited", T_OBJECT_EX, offsetof(TimingPointObject, uninherited),
     READONLY, nullptr},
    {"effects", T_OBJECT_EX, offsetof(TimingPointObject, effects), READONLY,
     nullptr},
    {nullptr, 0, 0, 0, nullptr}};
PyObject* timing_point_repr(PyObject* object) {
  return record_repr(object, "TimingPoint", timing_point_members);
}
PyObject* timing_point_equal(PyObject* a, PyObject* b, int op) {
  return record_equal(a, b, op, timing_point_members);
}
PyObject* timing_point_reduce(PyObject* object, PyObject*) {
  return record_reduce(object, timing_point_members);
}
PyMethodDef timing_point_methods[] = {
    {"__reduce__", timing_point_reduce, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyType_Slot timing_point_slots[] = {
    {Py_tp_new, reinterpret_cast<void*>(timing_point_new)},
    {Py_tp_repr, reinterpret_cast<void*>(timing_point_repr)},
    {Py_tp_richcompare, reinterpret_cast<void*>(timing_point_equal)},
    {Py_tp_hash, reinterpret_cast<void*>(PyObject_HashNotImplemented)},
    {Py_tp_methods, timing_point_methods},
    {Py_tp_dealloc, reinterpret_cast<void*>(timing_point_dealloc)},
    {Py_tp_traverse, reinterpret_cast<void*>(timing_point_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(timing_point_clear)},
    {Py_tp_members, timing_point_members},
    {0, nullptr}};
PyType_Spec timing_point_spec = {
    "fosu._core.TimingPoint", sizeof(TimingPointObject), 0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, timing_point_slots};
}  // namespace
