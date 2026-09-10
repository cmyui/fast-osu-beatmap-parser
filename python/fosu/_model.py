"""Mutable, detached beatmap values; all fields are populated during parsing."""

from dataclasses import dataclass
from enum import Enum, IntEnum, IntFlag
from typing import ClassVar, TypeAlias


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


class StoryboardElementType(IntEnum):
    VIDEO = 0
    SPRITE = 1
    ANIMATION = 2
    SAMPLE = 3


class StoryboardLayer(IntEnum):
    BACKGROUND = 0
    FAIL = 1
    PASS = 2
    FOREGROUND = 3
    OVERLAY = 4
    VIDEO = 5


class StoryboardOrigin(IntEnum):
    TOP_LEFT = 0
    CENTRE = 1
    CENTRE_LEFT = 2
    TOP_RIGHT = 3
    BOTTOM_CENTRE = 4
    TOP_CENTRE = 5
    CUSTOM = 6
    CENTRE_RIGHT = 7
    BOTTOM_LEFT = 8
    BOTTOM_RIGHT = 9


class AnimationLoopType(IntEnum):
    LOOP_FOREVER = 0
    LOOP_ONCE = 1


class StoryboardCommandType(IntEnum):
    FADE = 0
    SCALE = 1
    VECTOR_SCALE = 2
    ROTATE = 3
    MOVE_X = 4
    MOVE_Y = 5
    COLOUR = 6
    PARAMETER = 7
    LOOP = 8
    TRIGGER = 9


class StoryboardParameter(IntEnum):
    NONE = 0
    ADDITIVE = 1
    FLIP_HORIZONTAL = 2
    FLIP_VERTICAL = 3


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
    x: float
    y: float


@dataclass(slots=True, kw_only=True)
class StoryboardCommand:
    type: StoryboardCommandType
    depth: int
    easing: int
    start_time: float
    end_time: float
    start_value: tuple[float, float, float]
    end_value: tuple[float, float, float]
    parameter: StoryboardParameter
    repeat_count: int
    group_number: int
    trigger_name: str


@dataclass(slots=True, kw_only=True)
class StoryboardElement:
    type: StoryboardElementType
    layer: StoryboardLayer
    origin: StoryboardOrigin
    loop_type: AnimationLoopType
    filename: str
    x: float
    y: float
    time: float
    volume: int
    frame_count: int
    frame_delay: float
    commands: list[StoryboardCommand]


@dataclass(slots=True)
class PathPoint:
    x: float
    y: float


@dataclass(slots=True)
class SliderPath:
    """Retained polyline, relative to the head; distances are playfield pixels."""

    points: list[PathPoint]
    cumulative_lengths: list[float]

    def distance(self) -> float:
        return self.cumulative_lengths[-1] if self.cumulative_lengths else 0.0


@dataclass(slots=True)
class Stacking:
    stack_height: int
    stack_offset: PathPoint


@dataclass(slots=True, kw_only=True)
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


class SliderEventType(IntEnum):
    HEAD = 0
    TICK = 1
    REPEAT = 2
    LEGACY_LAST_TICK = 3
    TAIL = 4


@dataclass(slots=True)
class SliderEvent:
    type: SliderEventType
    time: float
    span_index: int
    span_start_time: float
    path_progress: float
    position: PathPoint


@dataclass(slots=True, kw_only=True)
class Circle(_HitObject):
    end_time: float
    is_circle: ClassVar[bool] = True


@dataclass(slots=True, kw_only=True)
class Slider(_HitObject):
    end_time: float
    slides: int
    curve_type: CurveType
    length: float
    edge_sounds: str
    edge_sets: str
    control_points: list[Point]
    path: SliderPath | None
    events: list[SliderEvent]
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
    video_offset: float
    storyboard_background_offset_x: float
    storyboard_background_offset_y: float
    tags: str
    bookmarks: str
    hit_objects: list[HitObject]
    timing_points: list[TimingPoint]
    breaks: list[Break]
    combo_colours: list[int]
    storyboard_elements: list[StoryboardElement]
    stats: ParseStats

    def __repr__(self) -> str:
        return (
            f"Beatmap(title={self.title!r}, artist={self.artist!r}, "
            f"version={self.version!r}, hit_objects={len(self.hit_objects)})"
        )
