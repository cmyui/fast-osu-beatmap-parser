"""Mutable, detached beatmap values; all fields are populated during parsing."""

from dataclasses import dataclass
from enum import IntEnum, IntFlag
from typing import ClassVar


class GameMode(IntEnum):
    OSU = 0
    TAIKO = 1
    CATCH = 2
    MANIA = 3


class HitSound(IntFlag):
    NORMAL = 1
    WHISTLE = 2
    FINISH = 4
    CLAP = 8


@dataclass(slots=True)
class Point:
    x: int
    y: int


@dataclass(slots=True)
class HitObject:
    start_time: float
    end_time: float | None
    x: int
    y: int
    hit_sound: HitSound
    raw_type: int
    raw_end_time: float
    is_new_combo: bool
    combo_skip: int
    raw_hit_sample: str
    is_circle: ClassVar[bool] = False
    is_slider: ClassVar[bool] = False
    is_spinner: ClassVar[bool] = False
    is_hold: ClassVar[bool] = False


@dataclass(slots=True)
class Circle(HitObject):
    end_time: float
    is_circle: ClassVar[bool] = True


@dataclass(slots=True)
class Slider(HitObject):
    end_time: None
    span_count: int
    curve_type: str
    length: float
    raw_edge_sounds: str
    raw_edge_sets: str
    control_points: list[Point]
    is_slider: ClassVar[bool] = True


@dataclass(slots=True)
class Spinner(HitObject):
    end_time: float
    is_spinner: ClassVar[bool] = True


@dataclass(slots=True)
class HoldNote(HitObject):
    end_time: float
    is_hold: ClassVar[bool] = True


@dataclass(slots=True)
class TimingPoint:
    time: float
    beat_length: float
    meter: int
    sample_set: int
    sample_index: int
    volume: int
    uninherited: bool
    effects: int


@dataclass(slots=True)
class Break:
    start: float
    end: float


@dataclass(slots=True)
class ParseStats:
    fast_path_lines: int
    slow_path_lines: int
    malformed_lines: int
    storyboard_lines: int


@dataclass(slots=True, repr=False)
class Beatmap:
    format_version: int
    audio_filename: str
    audio_lead_in: int
    preview_time: int | None
    countdown: int
    sample_set: str
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
    bookmarks: list[int]
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
    tags: list[str]
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
    raw_tags: str
    raw_bookmarks: str
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
