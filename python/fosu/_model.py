"""Detached beatmap values; all fields are populated during parsing."""

import sys
from enum import Enum, IntEnum, IntFlag
from typing import ClassVar, TypeAlias, TypeVar, cast, get_origin

if sys.version_info >= (3, 11):
    from typing import dataclass_transform
else:
    from typing_extensions import dataclass_transform

from . import _core

_T = TypeVar("_T")


def _restore(cls: type[_T]) -> _T:
    # Pickle/deepcopy memoize the empty record before restoring cyclic fields.
    return cast(_T, _core._restore_record(cls))


@dataclass_transform(frozen_default=True)
def _record(cls: type[_T]) -> type[_T]:
    """Keep the domain declaration here; give its eager fields native storage."""
    annotations = {}
    for base in reversed(cls.__mro__[:-1]):
        annotations.update(base.__annotations__)
    fields = tuple(
        name
        for name, hint in annotations.items()
        if cast(object, get_origin(hint)) is not ClassVar
    )
    result = _core._record(cls.__name__, fields)
    for name, value in vars(cls).items():
        if name not in {"__dict__", "__weakref__", "__annotations__"}:
            setattr(result, name, value)
    result.__annotations__ = annotations
    return cast(type[_T], result)


@_record
class Point:
    x: float
    y: float


class GameMode(IntEnum):
    OSU = 0
    TAIKO = 1
    CATCH = 2
    MANIA = 3


class CurveType(Enum):
    BEZIER = "B"
    CATMULL = "C"
    LINEAR = "L"
    PERFECT_CURVE = "P"


class SampleSet(IntEnum):
    NONE = 0
    NORMAL = 1
    SOFT = 2
    DRUM = 3


class HitSound(IntFlag):
    NORMAL = 1
    WHISTLE = 2
    FINISH = 4
    CLAP = 8


@_record
class CurveSegment:
    type: CurveType
    degree: int | None
    control_points: list[Point]


@_record
class PathPoint:
    x: float
    y: float


@_record
class SliderPath:
    """Retained polyline, relative to the head; distances are playfield pixels."""

    points: list[PathPoint]
    cumulative_lengths: list[float]

    def distance(self) -> float:
        return self.cumulative_lengths[-1] if self.cumulative_lengths else 0.0


@_record
class Stacking:
    stack_height: int
    stack_offset: PathPoint


@_record
class _HitObject:
    time: float
    x: float
    y: float
    hitsound: HitSound
    type: int
    new_combo: bool
    combo_skip: int
    hit_sample: str
    stacking: Stacking | None
    is_circle: ClassVar[bool] = False
    is_slider: ClassVar[bool] = False
    is_spinner: ClassVar[bool] = False
    is_hold: ClassVar[bool] = False

    def raw_position(self) -> tuple[float, float]:
        """Return the current position with any applied stacking offset removed."""
        if self.stacking is None:
            return self.x, self.y
        offset = self.stacking.stack_offset
        return self.x - offset.x, self.y - offset.y


@_record
class Circle(_HitObject):
    end_time: float
    is_circle: ClassVar[bool] = True


class SliderEventType(IntEnum):
    HEAD = 0
    TICK = 1
    REPEAT = 2
    LEGACY_LAST_TICK = 3
    TAIL = 4


@_record
class SliderEvent:
    type: SliderEventType
    time: float
    span_index: int
    span_start_time: float
    path_progress: float
    position: PathPoint


@_record
class Slider(_HitObject):
    end_time: float
    slides: int
    curve_type: CurveType
    curve_segments: list[CurveSegment]
    length: float
    edge_sounds: str
    edge_sets: str
    control_points: list[Point]
    path: SliderPath | None
    events: list[SliderEvent]
    is_slider: ClassVar[bool] = True


@_record
class Spinner(_HitObject):
    end_time: float
    is_spinner: ClassVar[bool] = True


@_record
class HoldNote(_HitObject):
    end_time: float
    is_hold: ClassVar[bool] = True


HitObject: TypeAlias = Circle | Slider | Spinner | HoldNote


@_record
class TimingPoint:
    time: float
    beat_length: float
    meter: int
    sample_set: SampleSet
    sample_index: int
    volume: int
    uninherited: bool
    effects: int


@_record
class Break:
    start: float
    end: float


@_record
class ParseStats:
    fast_path_lines: int
    slow_path_lines: int
    malformed_lines: int
    storyboard_lines: int


@_record
class Beatmap:
    format_version: int
    audio_filename: str
    audio_lead_in: int
    preview_time: int | None
    countdown: int
    sample_set: SampleSet
    sample_volume: int
    stack_leniency: float
    mode: GameMode
    letterbox_in_breaks: bool
    widescreen_storyboard: bool
    epilepsy_warning: bool
    special_style: bool
    use_skin_sprites: bool
    samples_match_playback_rate: bool
    countdown_offset: int
    overlay_position: str
    skin_preference: str
    bookmark_list: list[int]
    velocity_presets: list[float]
    distance_spacing: float
    beat_divisor: int
    grid_size: int
    timeline_zoom: float
    title: str
    title_unicode: str
    artist: str
    artist_unicode: str
    creator: str
    version: str
    source: str
    tag_list: list[str]
    beatmap_id: int | None
    beatmap_set_id: int | None
    hp: float
    cs: float
    od: float
    ar: float
    slider_multiplier: float
    slider_tick_rate: float
    background: str
    video: str
    tags: str
    bookmarks: str
    hit_objects: list[HitObject]
    timing_points: list[TimingPoint]
    breaks: list[Break]
    combo_colours: list[int]
    stats: ParseStats

    def __repr__(self) -> str:
        return (
            f"Beatmap(title={self.title!r}, artist={self.artist!r}, "
            f"version={self.version!r}, hit_objects={len(self.hit_objects)})"
        )
