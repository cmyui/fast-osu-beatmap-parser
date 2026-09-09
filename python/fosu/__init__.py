"""Fast .osu parsing into ordinary, mutable Python values."""

from __future__ import annotations

import sys
from enum import IntFlag
from os import PathLike

if sys.version_info >= (3, 12):
    from collections.abc import Buffer
else:
    from typing_extensions import Buffer

from . import _core
from ._core import backend as backend
from ._model import (
    NOT_CALCULATED,
    CalculationState,
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
    SampleSet,
    Slider,
    Spinner,
    TimingPoint,
)

__all__ = [
    "NOT_CALCULATED",
    "CalculationState",
    "Beatmap",
    "Break",
    "Circle",
    "CurveType",
    "GameMode",
    "HitObject",
    "HitSound",
    "HoldNote",
    "ParseStats",
    "Point",
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


def parse(
    data: Buffer,
    *,
    sections: Sections = Sections.ALL,
    calculate_slider_end_times: bool = True,
) -> Beatmap:
    """Parse selected sections into detached values, copying mutable buffers."""
    if not isinstance(data, bytes):
        with memoryview(data) as view:
            if view.nbytes > 64 * 1024 * 1024:
                raise ValueError("beatmap input exceeds the supported size")
            data = view.tobytes()
    return _core.parse(data, sections, calculate_slider_end_times)


def parse_file(
    path: str | bytes | PathLike[str] | PathLike[bytes],
    *,
    sections: Sections = Sections.ALL,
    calculate_slider_end_times: bool = True,
) -> Beatmap:
    """Read selected sections; raise OSError on file errors."""
    return _core.parse_file(path, sections, calculate_slider_end_times)
