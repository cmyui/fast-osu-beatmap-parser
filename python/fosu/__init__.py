"""Fast native .osu parsing with owned, read-only Python results."""

from ._native import backend as backend

from ._beatmap import (
    NO_SLIDER,
    Beatmap,
    Break,
    HitObject,
    ParseStats,
    Point,
    RecordSequence,
    Sections,
    Slider,
    TimingPoint,
    parse,
    parse_file,
)

__all__ = [
    "backend",
    "NO_SLIDER",
    "Beatmap",
    "Break",
    "HitObject",
    "ParseStats",
    "Point",
    "RecordSequence",
    "Sections",
    "Slider",
    "TimingPoint",
    "parse",
    "parse_file",
]
