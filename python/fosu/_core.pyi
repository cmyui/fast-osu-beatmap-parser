from os import PathLike
from typing import Final, Literal, final

from ._model import Beatmap

backend: Final[Literal["avx2", "neon", "scalar"]]

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
