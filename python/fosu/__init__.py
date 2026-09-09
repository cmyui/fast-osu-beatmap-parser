"""Fast .osu parsing into ordinary, mutable Python values."""

from __future__ import annotations

import sys
from bisect import bisect_left
from enum import IntFlag
from os import PathLike

if sys.version_info >= (3, 12):
    from collections.abc import Buffer
else:
    from typing_extensions import Buffer

from . import _core
from ._core import backend as backend
from ._model import (
    Beatmap,
    Break,
    Circle,
    CurveType,
    GameMode,
    HitObject,
    HitSound,
    HoldNote,
    ParseStats,
    Point,
    PathPoint,
    SliderPath,
    SliderEvent,
    SliderEventType,
    Stacking,
    SampleSet,
    Slider,
    Spinner,
    TimingPoint,
)

__all__ = [
    "Beatmap",
    "Break",
    "Circle",
    "CurveType",
    "GameMode",
    "HitObject",
    "HitSound",
    "HoldNote",
    "Mods",
    "ParseStats",
    "Point",
    "PathPoint",
    "SliderPath",
    "SliderEvent",
    "SliderEventType",
    "Stacking",
    "slider_position_at",
    "SampleSet",
    "Sections",
    "Slider",
    "Spinner",
    "TimingPoint",
    "backend",
    "parse",
    "parse_file",
]


class Sections(IntFlag):
    """Sections to parse; combine members with ``|``."""

    GENERAL = 1 << 1
    EDITOR = 1 << 2
    METADATA = 1 << 3
    DIFFICULTY = 1 << 4
    EVENTS = 1 << 5
    TIMING_POINTS = 1 << 6
    COLOURS = 1 << 7
    HIT_OBJECTS = 1 << 8
    ALL = 0x1FE


class Mods(IntFlag):
    """Supported gameplay modifications; combine members with ``|``."""

    NONE = 0
    EASY = 1 << 1
    HARD_ROCK = 1 << 4
    DOUBLE_TIME = 1 << 6
    HALF_TIME = 1 << 8
    NIGHTCORE = 1 << 9


def parse(
    data: Buffer,
    *,
    sections: Sections = Sections.ALL,
    calculate_slider_end_times: bool = False,
    calculate_slider_paths: bool = False,
    calculate_slider_events: bool = False,
    apply_stacking: bool = False,
    mods: Mods = Mods.NONE,
) -> Beatmap:
    """Parse selected sections into detached values, copying mutable buffers."""
    if not isinstance(data, bytes):
        with memoryview(data) as view:
            if view.nbytes > 64 * 1024 * 1024:
                raise ValueError("beatmap input exceeds the supported size")
            data = view.tobytes()
    return _core.parse(
        data,
        sections,
        calculate_slider_end_times,
        calculate_slider_paths,
        calculate_slider_events,
        apply_stacking,
        int(mods),
    )


def parse_file(
    path: str | bytes | PathLike[str] | PathLike[bytes],
    *,
    sections: Sections = Sections.ALL,
    calculate_slider_end_times: bool = False,
    calculate_slider_paths: bool = False,
    calculate_slider_events: bool = False,
    apply_stacking: bool = False,
    mods: Mods = Mods.NONE,
) -> Beatmap:
    """Read selected sections; raise OSError on file errors."""
    return _core.parse_file(
        path,
        sections,
        calculate_slider_end_times,
        calculate_slider_paths,
        calculate_slider_events,
        apply_stacking,
        int(mods),
    )


def slider_position_at(path: SliderPath, progress: float) -> PathPoint:
    """Query a retained path at clamped [0, 1] progress, relative to its head."""
    if not path.points:
        return PathPoint(0.0, 0.0)
    distance = min(max(progress, 0.0), 1.0) * path.distance()
    i = bisect_left(path.cumulative_lengths, distance)
    if i == 0:
        point = path.points[0]
        return PathPoint(point.x, point.y)
    if i >= len(path.points):
        point = path.points[-1]
        return PathPoint(point.x, point.y)
    start = path.cumulative_lengths[i - 1]
    length = path.cumulative_lengths[i] - start
    a, b = path.points[i - 1], path.points[i]
    weight = (distance - start) / length if abs(length) >= 1e-7 else 0.0
    return PathPoint(a.x + (b.x - a.x) * weight, a.y + (b.y - a.y) * weight)
