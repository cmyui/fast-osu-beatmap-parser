# Python API

FOSU is in active development; its API may change without compatibility shims.
Parsing returns eager, mutable dataclasses and lists, detached from native memory.

## Install and use

Requires CPython 3.10+. Wheels target Linux x86-64 and macOS arm64; source
installation requires a C++20 compiler.

```sh
python -m pip install .
```

```python
import fosu

beatmap = fosu.parse_file("map.osu")
for note in beatmap.hit_objects:
    if isinstance(note, fosu.Slider):
        print(note.time, note.length, note.control_points)
```

`parse_file(path)` accepts strings, bytes, and `os.PathLike`.
`parse(data)` accepts buffer-protocol objects, including bytes, bytearray,
memoryview, and arrays. Non-bytes buffers are copied to an immutable snapshot;
encode text explicitly.

Inputs are limited to 64 MiB. Oversized inputs raise `ValueError`, allocation
failures raise `MemoryError`, and file failures raise `OSError`. Native parsing
releases the GIL; Python value construction holds it. Concurrent calls return
independent results. Malformed records are skipped and counted in
`beatmap.stats.malformed_lines`; success does not certify playability.

## Section selection

```python
listing = fosu.parse_file(
    "map.osu",
    sections=fosu.Sections.METADATA | fosu.Sections.DIFFICULTY,
)
```

Both entry points accept keyword-only `sections`, defaulting to `Sections.ALL`.
Members are `GENERAL`, `EDITOR`, `METADATA`, `DIFFICULTY`, `EVENTS`,
`TIMING_POINTS`, `COLOURS`, and `HIT_OBJECTS`. Combine them with `|`.
Skipped sections retain defaults and empty lists; `Sections(0)` selects none.
Unsupported mask bits are rejected.

Include `GENERAL` for mode-dependent difficulty rules (such as mania CircleSize)
and `EVENTS` for break-dependent combo rules. Only selected sections contribute
malformed-line counts.

## Results and important distinctions

The typed dataclasses in [_model.py](../python/fosu/_model.py) define the fields.
Shared fields use C++ names. Important Python-specific behavior:

- `hit_objects` contains `Circle`, `Slider`, `Spinner`, or `HoldNote`, in
  stable timestamp order. Narrow the union with `isinstance`.
- Times are milliseconds. Slider endpoints are `0` by
  default. Pass `calculate_slider_end_times=True` to `parse` or `parse_file` to
  calculate them using curve distance, timing and repeats.
  Do not use slider endpoints unless calculation was requested.
  Accessing the field never computes or caches anything.
  Other object types still have numeric endpoints. Omitted sections use their default settings.
  Hit samples and slider edge fields remain text.
- Slider `control_points` includes the head position, unlike the native point
  range. `slides=2` means forward and back.
- `tag_list` and `bookmark_list` are parsed conveniences alongside the
  `tags` and `bookmarks` text fields.
- IDs and preview time map the `-1` sentinel to `None`.
- Strings decode with UTF-8 `surrogateescape`, preserving undecodable bytes.
- Inherited timing-point NaN beat lengths are preserved; consumers must not
  treat them as ordinary slider velocities.

Values can be edited, copied with `deepcopy`, exported with `dataclasses.asdict`,
or pickled. They do not validate assignments or recompute related fields:
changing a slider's position does not move its stored head point, and changing
`type` does not recalculate combo flags. `dataclasses.replace` is a shallow copy.
Mutation does not change the input or another parse result.

Pass `calculate_slider_paths=True` to retain each slider's `path` (otherwise
`None`). `fosu.slider_position_at(slider.path, progress)` is a pure query over
that polyline, with progress clamped to [0, 1]. Returned `PathPoint` coordinates
are relative to the head; add the slider's x/y for playfield coordinates.
Paths alone do not calculate end times. Requesting both reuses their distance.
Native code exposes the same query and `Beatmap.slider_paths`, indexed by slider.

`calculate_slider_events=True` additionally populates `slider.events` with
`HEAD`, `TICK`, `REPEAT`, `LEGACY_LAST_TICK`, and `TAIL` records in official
generator order, grouped by slider-span traversal. This is not necessarily
timestamp order: a legacy last tick may be timed before a late tick or the
repeat beginning its final span. The legacy event is the effective historical
tail judgement, not an additional score or combo event; the real `TAIL` and
slider `end_time` remain unchanged. This option includes path and end-time
calculation. Each record has time, span index/start time, path progress, and a
position relative to the head. Native code exposes `Beatmap.slider_events`,
indexed by slider. Without the option, events are empty. These are path events
using the decoded slider's timing, not a converted ruleset's nested hitobjects:
no samples or catch conversion.
Expansion beyond 1,048,576 events per map raises `MemoryError` rather than
silently dropping events.

`apply_stacking=True` applies unmodded osu!standard stacking after parsing,
including the pre-v6 algorithm. Hit-object x/y and absolute slider control points
are adjusted before returning. `obj.raw_position()` subtracts the stacking offset
from the current x/y, or returns x/y when stacking is absent. It can have small
floating-point rounding differences. Coordinates are floats in both interfaces,
including when stacking is disabled. Each object also retains `stack_height` and `stack_offset`
in its `Stacking` value for inspection; do not add the offset again.
Relative path/event positions remain unchanged: add them to the adjusted head.
It includes path and end-time calculation, but not events. Native results use
`Beatmap.stacking`, indexed by hit object. Other modes are unchanged; Python
`stacking` is `None` when not calculated. Include GENERAL, DIFFICULTY,
TIMING_POINTS and HIT_OBJECTS when selecting sections for meaningful results.

`mods=` accepts combined `Mods` values. EZ and HR adjust difficulty settings for
osu!standard and osu!taiko; HR also reflects standard hit objects and slider
control points vertically. EZ/HR require GENERAL and DIFFICULTY in `sections`.
They are not yet supported for osu!catch or osu!mania because those modes require
converted fruit offsets or resolved hit windows; support is planned. DT,
NIGHTCORE, and HT support every mode and divide gameplay timeline values by
1.5, 1.5, and 0.75 respectively. This includes hit objects, calculated slider
events, timing-point offsets and uninherited beat lengths, and breaks. General
and Editor timestamp metadata remains in source-map time.

See [compatibility](compatibility.md) for supported behavior and limitations;
there is no ruleset conversion.

## CPU selection

`fosu.backend` reports `"avx2"`, `"neon"`, or `"scalar"`.
Set `FOSU_BACKEND=auto|scalar|avx2|neon` before importing to select an engine.
Unsupported explicit requests raise `ImportError`; selection stays fixed for
the loaded extension. See [development checks](build.md).
