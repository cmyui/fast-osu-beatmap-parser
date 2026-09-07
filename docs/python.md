# Python

`fosu` parses an original `.osu` file into an owned, read-only Python object:

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.artist, beatmap.ar)
print(beatmap.hit_objects[0].time)
```

The package builds the C++ parser into its extension modules. No separate
`libfosu` installation, manual CFFI compilation or handle management is needed.

## Install

Download the wheel for your platform from the **Python wheels** GitHub Actions
artifact, extract the artifact ZIP, then install the wheel:

```sh
python -m pip install ./fosu-0.1.0-*.whl
```

Wheels target standard CPython 3.10+ on Linux x86-64 (glibc 2.28+) and macOS
Apple Silicon (11+). They use Python's stable ABI. Windows and free-threaded
Python builds are not supported. The package is not published to PyPI by this
repository's workflow.

A source checkout is also installable with `python -m pip install .`; this
requires a C++20 compiler. Build a source archive and wheel with
`python -m pip install build` followed by `python -m build`. Linux builds use
GCC; macOS builds use Apple Clang. Build dependencies are installed by pip.

NumPy is optional: `python -m pip install numpy`. Importing `fosu` does not
import NumPy.

## Input and section selection

```python
beatmap = fosu.parse(osu_bytes)  # bytes or a contiguous buffer
beatmap = fosu.parse_file(path) # str, bytes or pathlib.Path

listing = fosu.parse_file(
    "map.osu",
    sections=fosu.Sections.METADATA | fosu.Sections.DIFFICULTY,
)
```

`parse` copies the bytes once into native padded storage; the caller's input
can be released immediately. `parse_file` reads directly into that storage.
Both return independent results, and CFFI releases the GIL during native calls.
`parse` also accepts `bytearray`, `memoryview`, `mmap` and other C-contiguous
buffers. Noncontiguous buffers raise `BufferError`. Do not mutate a writable
input concurrently while parsing it.
Unrequested sections retain defaults, with empty collections; `Sections.ALL`
is the default. Available flags are `GENERAL`, `EDITOR`, `METADATA`,
`DIFFICULTY`, `EVENTS`, `TIMING_POINTS`, `COLOURS`, and `HIT_OBJECTS`.

The parser assumes valid editor-emitted beatmaps, as the C++ library does;
it is not a strict validator for hostile input. See the root README for format
coverage. Files must be smaller than 4 GiB minus 134 bytes.

Wrong Python argument types raise `TypeError`; invalid section bits, embedded
NULs in paths and oversized input raise `ValueError`. I/O errors raise the
corresponding `OSError` subclass, such as `FileNotFoundError`,
`PermissionError` or `IsADirectoryError`. An unexpected early EOF raises
`OSError` with `errno.EIO`. Allocation failures raise `MemoryError`.

## Fields and records

The Python names follow the C++ fields. `title`, `artist`, `creator`, `version`,
`audio_filename`, etc. are Python strings; `ar`, `cs`, `od`, `hp` and other
numeric fields are ordinary Python numbers. IDE completion and type hints are
included. UTF-8 strings use `surrogateescape` so unexpected bytes can round-trip
with `.encode("utf-8", "surrogateescape")`. Metadata strings are decoded once
on first access.

```python
for note in beatmap.hit_objects:
    print(note.x, note.y, note.time, note.hit_sample)
    if note.slider is not None:
        slider = note.slider
        print(slider.curve_type, slider.length, slider.slides)
        for point in slider.points:
            print(point.x, point.y)
```

`time` and `end_time` are integer milliseconds. `is_circle`, `is_slider`,
`is_spinner`, `is_hold` and `is_new_combo` are boolean properties. `slider` is
a `Slider` or `None`; `slider_index` retains the raw array index or
`fosu.NO_SLIDER` (`0xFFFFFFFF`). Slider `points` exclude the head position,
matching the native parser. `edge_sounds`, `edge_sets` and `hit_sample` remain
raw strings rather than being split into additional objects.

`hit_objects`, `sliders`, `slider_points`, `timing_points`, `breaks` and
`combo_colours` are read-only sequences. They support `len`, iteration, negative
indices and lazy slicing, including reversed slices. Indexing outside the
sequence raises `IndexError`. Each accessed record has named attributes;
colours are integers in `0xRRGGBB` form. `beatmap.stats` exposes all four native
parser counters. `beatmap.source_size` is the original byte count.
Record equality identifies the same record within the same parsed result, so
sequence membership, `index` and `count` work. Separately parsed maps do not
compare equal merely because their values match.

## Automatic ownership

Results and their public fields are read-only. Parsing does not eagerly create
one Python object per note; a record wrapper is created when that note is
accessed. Each record, sequence and exported array retains the native storage:

```python
note = fosu.parse_file("map.osu").hit_objects[0]
print(note.time)  # safe even though the temporary Beatmap is gone
```

There is no `free`, `close` or reparse operation. Storage is released when its
last dependent object is collected. Keeping even one note or array slice keeps
the whole map's native storage alive. Copy the values you need if you want to
release a large map while keeping a small result.
Native-backed objects are not picklable or deep-copyable. Extract ordinary
Python values or copy arrays when an independent representation is needed.

## Optional NumPy access

```python
objects = beatmap.hit_objects.to_numpy()
times = objects["time"]
positions = objects[["x", "y"]]
```

`numpy.asarray(beatmap.hit_objects)` is equivalent. These are zero-copy,
read-only arrays; slices, field views, memoryviews and `numpy.asarray` continue
to retain the storage after the original beatmap is deleted. Use `.copy()` for
an independent, writable array. Converting a whole sequence to Python objects
or copying arrays naturally adds work beyond parsing.

The structured dtype retains native field offsets and record sizes: hitobjects
and sliders are 40 bytes, points 8, timing points 40. String fields in arrays
are nested `{offset, length}` records. Normal Python record attributes return
decoded strings; bulk callers can resolve the offsets against `beatmap.text`,
a read-only memoryview of the original `.osu` bytes. NumPy's `slider_index` column
contains the raw index/sentinel. Timing `uninherited` is a byte in arrays and a
boolean on Python records. `curve_type` is a one-byte NumPy string and a Python
`str` on records. Reserved fields are not exposed.

## CPU selection and development

On x86-64, the wheel includes scalar and AVX2 variants of the same C API. A
baseline module checks CPU/OS support before importing the optimized module;
there is no per-parse dispatch. The optimized variant requires AVX2, BMI1,
BMI2 and POPCNT, with Linux scheduling tuned for Zen 4. Apple Silicon uses the
scalar variant; the published Zen 4 timings do not describe its performance.
Set `FOSU_FORCE_SCALAR=1` before importing for a scalar check.
Linux extensions bundle a private C++ runtime to reduce first-import cost;
only their Python initialization symbols are exported. macOS uses the system
C++ runtime. Neither uses the standalone executable's custom runtime.
The ordinary C++ library and standalone C API keep their existing build policy.

```sh
python -m pip install -e '.[test]'
python -m pytest tests/test_python.py
FOSU_FORCE_SCALAR=1 python -m pytest tests/test_python.py
python python/generate_stubs.py  # after changing public C fields
```

The C declarations are read from `include/fosu/c_api.h` during wheel builds.
The generated Python property stubs are checked in and verified against the
compiled declarations. Native APIs are private to the extension modules;
applications should use `fosu`'s public objects. See the
[benchmark guide](performance.md) for measured call boundaries and reproduction.
