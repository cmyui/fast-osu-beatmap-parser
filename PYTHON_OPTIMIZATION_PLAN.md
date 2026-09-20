# Python object-construction optimization plan

## Goal

Optimize the complete eager-consumption path:

```python
beatmap = fosu.parse(data)
for obj in beatmap.hit_objects:
    use(obj.x, obj.y, obj.time)
for point in beatmap.timing_points:
    use(point.time, point.beat_length, point.meter, ...)
```

All supported fields should already be available as ordinary, correctly typed Python values when `parse()` returns. No lazy getters or deferred conversion. Writes to record attributes are not required, but Python consumers must be able to release objects safely, including cycles they can create through mutable containers.

## Where we are

The C++ binding already constructs slotted Python records directly with `PyType_GenericAlloc` and cached slot-descriptor setters; it does not call their generated dataclass constructors. Lists are allocated at their final size and filled as values are produced. On CPython 3.14, reads of real hit-object `x`, `y`, and `time` specialize to `LOAD_ATTR_SLOT`. Native record types with object-valued members get the same read specialization. The main opportunity is therefore cheaper eager materialization, not replacing Python attribute reads.

Do not store frequently read numbers as C `double` members: CPython's member getter boxes a fresh Python float on every read of a `double`, while an object-valued slot returns the eagerly created float. That is the wrong tradeoff for this workload.

An exploratory three-pass run on the 1,024-map default-decode corpus, replacing `print` with a checksum, took about **394 µs/map** for parse, scan, and release. The Python loops reading all hit-object `x/y/time` and all timing-point fields plus checksum arithmetic took about **99 µs/map**. The remainder includes parsing, Python object construction, destruction, and GC; it is not an isolated construction measurement. These are orientation numbers, not a controlled before/after benchmark. Literal printing would dominate them.

The same corpus contained 1,215,093 hit objects (986,263 circles) and 98,760 timing points. All 2,430,186 exposed hit-object `x/y` values in this default-decode run were integers in `[0, 512]`. Other inputs and options, including mods and stacking, can produce different coordinates.

## Experiments, in order

1. **Share common boxed coordinate floats.** Keep a per-module cache of Python floats for integral coordinates `0..512`; return a new float for everything else. Preserve distinctions such as negative zero. The cache should change neither the attribute type nor its eager-read behavior. Benchmark the extra range/integrality check against the saved `PyFloat_FromDouble` allocations. This is promising from the corpus distribution, but no speedup has been measured yet.
2. **Pilot native read-only records for `Circle` and `TimingPoint`.** Give each field an eagerly populated `PyObject*` slot, exposed through read-only `PyMemberDef` object members. Construct each instance and transfer field references directly, avoiding the current descriptor-setter call per field. These two types exercise the most common hit object and the timing-point workload. Keep every field, type, and simple attribute read available. Use the Stable ABI first; do not assume a private CPython API is necessary. Track any native type that can participate in a Python reference cycle and implement the required GC traversal/clearing behavior.
3. **Expand only if the pilot wins end to end.** Apply the same representation to other record types if the measured benefit justifies its C++ code and Python protocol surface. As a separate experiment, runtime-validated direct writes into the existing dataclass slot offsets could avoid setter calls without replacing classes, but this requires the non-limited CPython ABI and version-specific wheels. Prefer it only if it offers a substantial additional measured win.

Do not start with private float-object allocation, a custom Python-object arena, or `PyList_SET_ITEM`. `PyFloat_FromDouble` already uses CPython's float freelist, and exact-sized lists are already in place. `PyList_SET_ITEM` removes checks but leaves the dominant object-creation work; losing the current ABI compatibility for it alone is unlikely to pay off.

## How to decide

Compare the same maps, options, Python version, CPU, and resulting Python values. Time native parsing, eager conversion, a complete attribute scan, and release/GC separately, as well as the full `parse → scan → release` path. Exclude I/O from the timed scan. Screen each candidate on a small representative all-mode corpus, then use interleaved/reversed-order confirmation on the full corpus and a second machine for promising results. Include paths with mods, stacking, and slider geometry so a default-decode win does not hide a regression elsewhere. Check installed-package behavior across supported Python versions, typing, and consumer-created reference cycles before adopting a new record representation.

## CPython references

- [Slot-read specialization](https://github.com/python/cpython/blob/v3.14.0/Python/specialize.c#L867-L884) and [specialized `LOAD_ATTR_SLOT` implementation](https://github.com/python/cpython/blob/v3.14.0/Python/bytecodes.c#L2451-L2472)
- [Member access: object slots versus `double`](https://github.com/python/cpython/blob/v3.14.0/Python/structmember.c#L23-L116)
- [`PyFloat_FromDouble` allocation/freelist path](https://github.com/python/cpython/blob/v3.14.0/Objects/floatobject.c#L123-L136)
- [Stable-ABI member definitions](https://docs.python.org/3/c-api/structures.html#accessing-attributes-of-extension-types) and [GC requirements for extension types](https://docs.python.org/3/c-api/gcsupport.html)
