"""Static public API contract; checked by mypy, not executed."""

from dataclasses import asdict, replace
from typing import Literal

import fosu
from typing_extensions import assert_never, assert_type

b = fosu.parse(b"")
assert_type(fosu.parse(b"", sections=fosu.Sections.METADATA | fosu.Sections.DIFFICULTY), fosu.Beatmap)
assert_type(fosu.parse_file("map.osu", sections=fosu.Sections.HIT_OBJECTS), fosu.Beatmap)
assert_type(fosu.backend, Literal["avx2", "neon", "scalar"])
assert_type(b, fosu.Beatmap)
assert_type(b.tag_list, list[str])
assert_type(b.mode, fosu.GameMode)
assert_type(b.sample_set, fosu.SampleSet)
assert_type(b.preview_time, int | None)
assert_type(b.hit_objects, list[fosu.HitObject])
b.title = "Edited"
b.tag_list.append("new")
for note in b.hit_objects:
    assert_type(note.time, float)
    assert_type(note.x, float)
    assert_type(note.y, float)
    assert_type(note.raw_position(), tuple[float, float])
    note.x = 100
    assert_type(note.stacking, fosu.Stacking | None)
    if note.stacking is not None:
        assert_type(note.stacking.stack_height, int)
        assert_type(note.stacking.stack_offset, fosu.PathPoint)
    if isinstance(note, fosu.Slider):
        assert_type(note.end_time, float)
        assert_type(note.curve_type, fosu.CurveType)
        assert_type(note.control_points, list[fosu.Point])
        assert_type(note.path, fosu.SliderPath | None)
        assert_type(note.events, list[fosu.SliderEvent])
        if note.path is not None:
            assert_type(fosu.slider_position_at(note.path, 0.5), fosu.PathPoint)
        for event in note.events:
            assert_type(event.type, fosu.SliderEventType)
            assert_type(event.position, fosu.PathPoint)
        note.control_points.append(fosu.Point(1, 2))
    elif isinstance(note, fosu.Circle):
        assert_type(note.end_time, float)
    elif isinstance(note, (fosu.Spinner, fosu.HoldNote)):
        assert_type(note.end_time, float)
    else:
        assert_never(note)
assert_type(replace(b, title="Copy"), fosu.Beatmap)
asdict(b)
