"""Fast .osu parsing into ordinary, mutable Python values."""

from __future__ import annotations

import sys
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
    GameMode,
    HitObject,
    HitSound,
    HoldNote,
    ParseStats,
    Point,
    Slider,
    Spinner,
    TimingPoint,
)

__all__ = [
    "Beatmap",
    "Break",
    "Circle",
    "GameMode",
    "HitObject",
    "HitSound",
    "HoldNote",
    "ParseStats",
    "Point",
    "Slider",
    "Spinner",
    "TimingPoint",
    "backend",
    "parse",
    "parse_file",
]


def parse(data: Buffer) -> Beatmap:
    """Parse a complete beatmap into detached values, copying mutable buffers."""
    if not isinstance(data, bytes):
        with memoryview(data) as view:
            if view.nbytes > 64 * 1024 * 1024:
                raise ValueError("beatmap input exceeds the supported size")
            data = view.tobytes()
    return _core.parse(data)


def parse_file(path: str | bytes | PathLike[str] | PathLike[bytes]) -> Beatmap:
    """Read a complete beatmap; raise OSError on file errors."""
    return _core.parse_file(path)
