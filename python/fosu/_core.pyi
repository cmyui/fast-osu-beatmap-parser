from os import PathLike
from typing import Final, Literal, final

from ._model import Beatmap, HitSound, SampleSet, Stacking

backend: Final[Literal["avx2", "neon", "scalar"]]

@final
class Circle:
    is_circle: Final[Literal[True]]
    is_slider: Final[Literal[False]]
    is_spinner: Final[Literal[False]]
    is_hold: Final[Literal[False]]
    @property
    def time(self) -> float: ...
    @property
    def x(self) -> float: ...
    @property
    def y(self) -> float: ...
    @property
    def hitsound(self) -> HitSound: ...
    @property
    def type(self) -> int: ...
    @property
    def new_combo(self) -> bool: ...
    @property
    def combo_skip(self) -> int: ...
    @property
    def hit_sample(self) -> str: ...
    @property
    def stacking(self) -> Stacking | None: ...
    @property
    def end_time(self) -> float: ...
    def __new__(
        cls,
        time: float,
        x: float,
        y: float,
        hitsound: HitSound,
        type: int,
        new_combo: bool,
        combo_skip: int,
        hit_sample: str,
        stacking: Stacking | None,
        end_time: float,
    ) -> Circle: ...
    def raw_position(self) -> tuple[float, float]: ...

@final
class TimingPoint:
    @property
    def time(self) -> float: ...
    @property
    def beat_length(self) -> float: ...
    @property
    def meter(self) -> int: ...
    @property
    def sample_set(self) -> SampleSet: ...
    @property
    def sample_index(self) -> int: ...
    @property
    def volume(self) -> int: ...
    @property
    def uninherited(self) -> bool: ...
    @property
    def effects(self) -> int: ...
    def __new__(
        cls,
        time: float,
        beat_length: float,
        meter: int,
        sample_set: SampleSet,
        sample_index: int,
        volume: int,
        uninherited: bool,
        effects: int,
    ) -> TimingPoint: ...

@final
class Point:
    @property
    def x(self) -> float: ...
    @property
    def y(self) -> float: ...
    def __new__(cls, x: float, y: float) -> Point: ...

def parse(
    data: bytes,
    sections: int,
    calculate_slider_end_times: bool,
    calculate_slider_paths: bool,
    calculate_slider_events: bool,
    apply_stacking: bool,
    mods: int,
    /,
) -> Beatmap: ...
def parse_file(
    path: str | bytes | PathLike[str] | PathLike[bytes],
    sections: int,
    calculate_slider_end_times: bool,
    calculate_slider_paths: bool,
    calculate_slider_events: bool,
    apply_stacking: bool,
    mods: int,
    /,
) -> Beatmap: ...
