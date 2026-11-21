#pragma once

#include <Python.h>
#include <bit>
#include <cstdint>
#include <structmember.h>

namespace {
// Object-valued slots hold eager Python values, not C numbers boxed on read.
struct RecordObject {
  // clang-format off
  PyObject_HEAD
  Py_ssize_t field_count;
  std::uint64_t gc_field_mask;
  // clang-format on
};
PyObject** record_fields(PyObject* object) {
  return reinterpret_cast<PyObject**>(reinterpret_cast<char*>(object) +
                                      sizeof(RecordObject));
}
const PyMemberDef* record_members(PyTypeObject* type) {
  return static_cast<const PyMemberDef*>(PyType_GetSlot(type, Py_tp_members));
}
PyObject* allocate_record(PyTypeObject* type, Py_ssize_t count) {
  PyObject* object = PyType_GenericAlloc(type, 0);
  if (object) {
    reinterpret_cast<RecordObject*>(object)->field_count = count;
    reinterpret_cast<RecordObject*>(object)->gc_field_mask = 0;
  }
  return object;
}
int record_traverse(PyObject* object, visitproc visit, void* arg) {
  Py_VISIT(Py_TYPE(object));
  auto* fields = record_fields(object);
  auto* record = reinterpret_cast<RecordObject*>(object);
  auto  mask = record->gc_field_mask;
  while (mask) {
    Py_VISIT(fields[std::countr_zero(mask)]);
    mask &= mask - 1;
  }
  return 0;
}
void record_set_field(PyObject* object, Py_ssize_t index, PyObject* value) {
  record_fields(object)[index] = value;
  // Read-only fields can only enter a cycle when their type supports GC.
  if (PyType_HasFeature(Py_TYPE(value), Py_TPFLAGS_HAVE_GC)) {
    auto* record = reinterpret_cast<RecordObject*>(object);
    record->gc_field_mask |= std::uint64_t{1} << index;
  }
}
int record_clear(PyObject* object) {
  auto* fields = record_fields(object);
  for (Py_ssize_t i = 0;
       i < reinterpret_cast<RecordObject*>(object)->field_count; ++i) {
    Py_CLEAR(fields[i]);
  }
  return 0;
}
void record_dealloc(PyObject* object) {
  PyObject_GC_UnTrack(object);
  record_clear(object);
  PyTypeObject* type = Py_TYPE(object);
  PyObject_GC_Del(object);
  Py_DECREF(type);
}
PyObject* record_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
  if (PyType_GetSlot(type, Py_tp_dealloc) !=
      reinterpret_cast<void*>(record_dealloc)) {
    PyErr_SetString(PyExc_TypeError,
                    "FOSU record subclasses cannot be instantiated");
    return nullptr;
  }
  const auto* members = record_members(type);
  Py_ssize_t  count = 0;
  while (members[count].name)
    ++count;
  Py_ssize_t positional = PyTuple_Size(args);
  if (positional > count) {
    PyErr_SetString(PyExc_TypeError, "too many positional arguments");
    return nullptr;
  }
  PyObject* object = allocate_record(type, count);
  if (!object)
    return nullptr;
  Py_ssize_t keywords = 0;
  for (Py_ssize_t i = 0; i < count; ++i) {
    PyObject* keyword =
        kwargs ? PyDict_GetItemString(kwargs, members[i].name) : nullptr;
    if (i < positional && keyword) {
      PyErr_Format(PyExc_TypeError, "multiple values for %s", members[i].name);
      Py_DECREF(object);
      return nullptr;
    }
    PyObject* value = i < positional ? PyTuple_GetItem(args, i) : keyword;
    if (!value) {
      PyErr_Format(PyExc_TypeError, "missing required argument: %s",
                   members[i].name);
      Py_DECREF(object);
      return nullptr;
    }
    keywords += keyword != nullptr;
    record_set_field(object, i, Py_NewRef(value));
  }
  if (kwargs && keywords != PyDict_Size(kwargs)) {
    PyErr_SetString(PyExc_TypeError, "unexpected keyword argument");
    Py_DECREF(object);
    return nullptr;
  }
  return object;
}
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
PyObject* record_equal(PyObject* left, PyObject* right, int op) {
  const auto* members = record_members(Py_TYPE(left));
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
PyObject* record_reduce(PyObject* object, PyObject*) {
  const auto* members = record_members(Py_TYPE(object));
  PyObject*   values = record_values(object, members);
  if (!values)
    return nullptr;
  PyObject* type = reinterpret_cast<PyObject*>(Py_TYPE(object));
  PyObject* module_name = PyObject_GetAttrString(type, "__module__");
  PyObject* module = module_name ? PyImport_Import(module_name) : nullptr;
  PyObject* restore =
      module ? PyObject_GetAttrString(module, "_restore") : nullptr;
  PyObject* args = restore ? PyTuple_Pack(1, type) : nullptr;
  PyObject* result = args ? PyTuple_Pack(3, restore, args, values) : nullptr;
  Py_XDECREF(args);
  Py_XDECREF(restore);
  Py_XDECREF(module);
  Py_XDECREF(module_name);
  Py_DECREF(values);
  return result;
}
PyObject* record_setstate(PyObject* object, PyObject* values) {
  const auto count = reinterpret_cast<RecordObject*>(object)->field_count;
  if (!PyTuple_Check(values) || PyTuple_Size(values) != count) {
    PyErr_SetString(PyExc_TypeError, "record state must contain every field");
    return nullptr;
  }
  auto* fields = record_fields(object);
  for (Py_ssize_t i = 0; i < count; ++i) {
    if (fields[i]) {
      PyErr_SetString(PyExc_AttributeError, "record is already initialized");
      return nullptr;
    }
  }
  for (Py_ssize_t i = 0; i < count; ++i)
    record_set_field(object, i, Py_NewRef(PyTuple_GetItem(values, i)));
  Py_RETURN_NONE;
}
PyObject* record_repr(PyObject* object) {
  const auto* members = record_members(Py_TYPE(object));
  int         entered = Py_ReprEnter(object);
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
  if (joined) {
    PyObject* name = PyObject_GetAttrString(
        reinterpret_cast<PyObject*>(Py_TYPE(object)), "__name__");
    if (name) {
      result = PyUnicode_FromFormat("%U(%U)", name, joined);
      Py_DECREF(name);
    }
  }
done:
  Py_XDECREF(joined);
  Py_XDECREF(separator);
  Py_XDECREF(fields);
  Py_ReprLeave(object);
  return result;
}

PyMethodDef record_methods[] = {
    {"__reduce__", record_reduce, METH_NOARGS, nullptr},
    {"__setstate__", record_setstate, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}  // namespace
