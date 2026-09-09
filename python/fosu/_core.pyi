from os import PathLike
from typing import Final, Literal

from ._model import Beatmap

backend: Final[Literal["avx2", "neon", "scalar"]]

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
