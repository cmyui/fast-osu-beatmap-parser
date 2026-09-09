"""Mutable, detached beatmap values; all fields are populated during parsing."""

from dataclasses import dataclass
from enum import Enum, IntEnum, IntFlag
from typing import ClassVar, Final, Literal, TypeAlias


class CalculationState(Enum):
    NOT_CALCULATED = "NOT_CALCULATED"


NOT_CALCULATED: Final = CalculationState.NOT_CALCULATED


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


@dataclass(slots=True)
class Point:
    x: int
    y: int


@dataclass(slots=True, kw_only=True)
class _HitObject:
    time: float
    x: int
    y: int
    hitsound: HitSound
    type: int
    new_combo: bool
    combo_skip: int
    hit_sample: str
    is_circle: ClassVar[bool] = False
    is_slider: ClassVar[bool] = False
    is_spinner: ClassVar[bool] = False
    is_hold: ClassVar[bool] = False


@dataclass(slots=True, kw_only=True)
class Circle(_HitObject):
    end_time: float
    is_circle: ClassVar[bool] = True


@dataclass(slots=True, kw_only=True)
class Slider(_HitObject):
    end_time: float | Literal[CalculationState.NOT_CALCULATED]
    slides: int
    curve_type: CurveType
    length: float
    edge_sounds: str
    edge_sets: str
    control_points: list[Point]
    is_slider: ClassVar[bool] = True


@dataclass(slots=True, kw_only=True)
class Spinner(_HitObject):
    end_time: float
    is_spinner: ClassVar[bool] = True


@dataclass(slots=True, kw_only=True)
class HoldNote(_HitObject):
    end_time: float
    is_hold: ClassVar[bool] = True


HitObject: TypeAlias = Circle | Slider | Spinner | HoldNote


@dataclass(slots=True, kw_only=True)
class TimingPoint:
    time: float
    beat_length: float
    meter: int
    sample_set: SampleSet
    sample_index: int
    volume: int
    uninherited: bool
    effects: int


@dataclass(slots=True)
class Break:
    start: float
    end: float


@dataclass(slots=True, kw_only=True)
class ParseStats:
    fast_path_lines: int
    slow_path_lines: int
    malformed_lines: int
    storyboard_lines: int


@dataclass(slots=True, repr=False, kw_only=True)
class Beatmap:
    format_version: int
    audio_filename: str
    audio_lead_in: int
    preview_time: int | None
    countdown: int
    sample_set: SampleSet
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
