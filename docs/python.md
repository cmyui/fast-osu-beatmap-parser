# Python API

fosu reads a complete `.osu` beatmap into ordinary, mutable Python dataclasses
and lists. Parsing and conversion run in compiled code. Every number, string,
record and list are ready when the call returns; no native
allocation is retained by the result.

## Install

Install a wheel from the repository's **Python wheels** workflow, or build from
a source checkout:

```sh
python -m pip install path/to/fosu-0.2.0-cp310-abi3-PLATFORM.whl
# From a checkout with a C++20 compiler:
python -m pip install .
```

Wheels support CPython 3.10 and later on Linux x86-64 and macOS arm64. Linux
wheels target glibc 2.28 or later; macOS wheels target macOS 11 or later.
The wheel selects AVX2, NEON or scalar automatically. Source builds use the
same dispatch policy. Python 3.10
and 3.11 use typing-extensions for buffer annotations.

## Parse and use

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.artist, beatmap.ar)

for note in beatmap.hit_objects:
    if isinstance(note, fosu.Slider):
        print(note.start_time, note.span_count, note.length)
        print(note.control_points[0])  # slider head included

note = beatmap.hit_objects[0]
note.x = 128
assert beatmap.hit_objects[0] is note
beatmap.tags.append("reviewed")
```

`parse_file(path)` accepts strings, bytes and `os.PathLike` paths. It reads,
parses and converts the entire map, and raises `OSError` on file errors.
`parse(data)` accepts objects supporting the buffer protocol, including bytes,
bytearray, memoryview and arrays. Non-bytes inputs are copied into an immutable
snapshot before the native parser releases the GIL; non-contiguous buffers are
copied in logical C order. Strings must be encoded explicitly.

```python
beatmap = fosu.parse(uploaded_bytes)
```

Inputs are limited to **64 MiB**; oversized input raises `ValueError` and
allocation failure raises `MemoryError`. Native parsing releases the GIL;
constructing Python objects holds it. Separate calls return independent results
and may be made from multiple threads. No parsed beatmaps are cached.

Both functions parse every supported section. Absent sections have the native
parser's defaults, including empty lists for absent record sections. The C++
and C APIs offer section selection for applications needing that boundary.
Malformed records are skipped and counted according to the
[parser contract](compatibility.md); success does not certify playability.

## Beatmap fields

`Beatmap` is a dataclass with explicit type annotations. Its main fields are:

| Group | Attributes |
|---|---|
| Source | `format_version` |
| General | `audio_filename`, `audio_lead_in`, `preview_time`, `countdown`, `sample_set`, `stack_leniency`, `mode`, `letterbox_in_breaks`, `widescreen_storyboard`, `epilepsy_warning`, `special_style`, `use_skin_sprites`, `samples_match_playback_rate`, `countdown_offset`, `overlay_position`, `skin_preference` |
| Editor | `bookmarks`, `raw_bookmarks`, `distance_spacing`, `beat_divisor`, `grid_size`, `timeline_zoom` |
| Metadata | `title`, `title_unicode`, `artist`, `artist_unicode`, `creator`, `version`, `source`, `tags`, `raw_tags`, `beatmap_id`, `beatmap_set_id` |
| Difficulty | `hp`, `cs`, `od`, `ar`, `slider_multiplier`, `slider_tick_rate` |
| Events | `background`, `video`, `breaks` |
| Collections | `hit_objects`, `timing_points`, `combo_colours` |
| Statistics | `stats` |

`mode` is `GameMode.OSU`, `TAIKO`, `CATCH` or `MANIA`. Difficulty values describe
the parsed file; gameplay normalization and mods are not applied. Missing
ApproachRate follows the native decoder's OverallDifficulty default.

`tags` is a list of whitespace-separated strings, retaining duplicates and order.
`bookmarks` contains valid signed 32-bit timestamps in file order. Following
osu!'s official legacy decoder, invalid bookmark tokens are skipped rather than
rejecting the field or map. `raw_tags` and `raw_bookmarks` retain the parser's
text values. The `-1` sentinel for IDs and preview time becomes `None`; zero and
other negative values remain values.

Strings decode as UTF-8 with `surrogateescape`, so undecodable input bytes can be
recovered with `value.encode("utf-8", "surrogateescape")`. Raw fields retain the
parser's text values, not the complete source file.

## Hitobjects and sliders

`hit_objects` is a `list[HitObject]` containing `Circle`, `Slider`, `Spinner` and
`HoldNote` instances in file order. `HitObject` is the union of these four types,
not a constructible base class. Use `isinstance` to narrow to a concrete type.
Common stored attributes are:

- `start_time`, `end_time`: milliseconds. A circle ends at its start time;
  spinner/hold endpoints are parsed from the file. A slider's endpoint is `None`
  because it requires gameplay timing calculation.
- `x`, `y`: coordinates in osu! pixels.
- `hit_sound`: a `HitSound` flag value. `NORMAL`, `WHISTLE`, `FINISH` and `CLAP`
  name the source bits; zero means default, and unknown bits are preserved.
- `is_new_combo`, `combo_skip`: combo flags decoded at construction.
- `raw_type`, `raw_end_time`, `raw_hit_sample`: native decoded fields. Empty
  sample text is valid and describes the parser's default sample representation.

Kinds also expose `is_circle`, `is_slider`, `is_spinner` and `is_hold`. Conflicting
source kind bits follow the decoder's precedence: circle, slider, spinner, hold.

A `Slider` additionally stores `span_count`, `curve_type`, `length`,
`raw_edge_sounds`, `raw_edge_sets`, and `control_points: list[Point]`. The control
points include the head followed by the path's remaining points in file order.
Native pool offsets and indices are not part of the Python model.
`span_count=2` means forward and back.
`curve_type` is a `CurveType` enum: `BEZIER`, `CATMULL`, `LINEAR`, or
`PERFECT_CURVE`. Its `.value` is the file's `B`, `C`, `L`, or `P` code.
No curve evaluation, slider duration, stacking, ruleset conversion or mod
adjustment is performed.

`Point` stores `x` and `y`. `TimingPoint` stores `time`, `beat_length`, `meter`,
`sample_set`, `sample_index`, `volume`, `uninherited` and `effects`. Inherited NaN
beat lengths are preserved. `Break` stores `start` and `end`. `combo_colours` is
a list of packed `0xRRGGBB` integers.

Both `Beatmap.sample_set` and `TimingPoint.sample_set` use `SampleSet`:
`NONE=0`, `NORMAL=1`, `SOFT=2`, and `DRUM=3`. `NONE` preserves the legacy
default selector: a timing point uses the beatmap default, and a beatmap's
`NONE` denotes normal. Unknown enum values are rejected during native parsing;
see the [malformed-input contract](compatibility.md).

`ParseStats` contains `fast_path_lines`, `slow_path_lines`, `malformed_lines` and
`storyboard_lines`, describing the original parse. Storyboard bodies are counted
and skipped. Invalid bookmark tokens do not count as rejected records.

## Mutation, copying and export

Beatmaps, hitobjects, timing points and parse statistics have keyword-only
constructors. `Point(x, y)` and `Break(start, end)` also accept positional values.
Annotations describe parsed values and guide type checking; these ordinary
mutable dataclasses do not validate assignments at runtime.
Narrow a `HitObject` with `isinstance` before changing type-specific fields:
mypy checks a concrete circle's `end_time` as `float`, but currently permits
`None` assignment through the unnarrowed union.

All result values are detached. Repeated list indexing returns the same object,
and mutations persist in that object. Mutating a result does not change another
parse result, the original input, or the file on disk. Stored duplicate values
are independent: changing `raw_type` does not recompute its decoded flags,
and moving a slider's `x`/`y` does not move its stored head point.
Editing a result is not gameplay preparation or `.osu` serialization.

Use standard Python tools:

```python
from copy import deepcopy
from dataclasses import asdict, replace

independent = deepcopy(beatmap)
renamed = replace(beatmap, title="New title")  # shallow copy: lists are shared
plain_dict = asdict(beatmap)                  # recursively copies dataclasses
```

Results support pickle. A reference to one record does not keep the whole native
beatmap alive: the parser has already been destroyed. The native parser may keep
a bounded spare arena for subsequent calls; no result depends on that storage.
Lists support ordinary
indexing, slicing, insertion and removal. If NumPy arrays are needed, construct
them explicitly from Python values; that conversion copies data.

## CPU selection and development

`fosu.backend` reports `"avx2"`, `"neon"` or `"scalar"`. Set `FOSU_BACKEND` before
import to request `auto`, `avx2`, `neon` or `scalar`. Unsupported requests raise
`ImportError`. `FOSU_FORCE_SCALAR=1` is a shorthand when `FOSU_BACKEND` is unset.
Selection stays fixed for that loaded extension. Linux AVX2 remains tuned for
Zen 4. C++ callers control header-only compilation separately.

The extension uses CPython's 3.10 stable ABI. CMake compiles the converter and
native parser together; no binding source or public dataclass definitions are
generated. The Python model is in `python/fosu/_model.py`; the conversion is in
`src/fosu/bindings/python.cc`. A small private extension stub types the compiled entry
points; public typing comes directly from the dataclasses and functions.

Linux release wheels bundle a private C++ runtime and export only the Python
initialization symbol. macOS uses its system C++ runtime. Both use the
[compiled-target hardening policy](build.md#hardening). Bundled-runtime updates
require rebuilding the wheel; `FOSU_BUNDLE_RUNTIME=0` disables bundling for custom
builds.

```sh
python -m pip install -e '.[test]'
python -m mypy --config-file pyproject.toml
python -m pytest tests/test_python.py
FOSU_FORCE_SCALAR=1 python -m pytest tests/test_python.py
```

Pre-commit and CI check the shipped package and consumer examples with the same
pinned mypy and strict `pyproject.toml` configuration, targeting Python 3.10.
Installed-wheel tests additionally check the consumer examples for the running
Python version and use `stubtest` to compare the private extension stub with the
loaded module. This checks exposed members/signatures, not every value produced
by native code; runtime conversion tests cover those values.

Tests run against installed wheels in CI, including Linux ELF protections.
The [performance guide](performance.md) separates native parsing,
full Python result construction, first use, and process startup.
