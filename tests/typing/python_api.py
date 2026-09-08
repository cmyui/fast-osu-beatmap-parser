"""Static public API contract; checked by mypy, not executed."""

from dataclasses import asdict, replace
from typing import Literal

import fosu
from typing_extensions import assert_never, assert_type

b = fosu.parse(b"")
assert_type(fosu.backend, Literal["avx2", "neon", "scalar"])
assert_type(b, fosu.Beatmap)
assert_type(b.tags, list[str])
assert_type(b.mode, fosu.GameMode)
assert_type(b.sample_set, fosu.SampleSet)
assert_type(b.preview_time, int | None)
assert_type(b.hit_objects, list[fosu.HitObject])
b.title = "Edited"
b.tags.append("new")
for note in b.hit_objects:
    note.x = 100
    assert_type(note.start_time, float)
    if isinstance(note, fosu.Slider):
        assert_type(note.end_time, None)
        assert_type(note.curve_type, fosu.CurveType)
        assert_type(note.control_points, list[fosu.Point])
        note.control_points.append(fosu.Point(1, 2))
    elif isinstance(note, fosu.Circle):
        assert_type(note.end_time, float)
    elif isinstance(note, (fosu.Spinner, fosu.HoldNote)):
        assert_type(note.end_time, float)
    else:
        assert_never(note)
assert_type(replace(b, title="Copy"), fosu.Beatmap)
asdict(b)
