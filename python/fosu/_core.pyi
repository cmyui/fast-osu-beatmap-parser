from os import PathLike
from typing import Final, Literal

from ._model import Beatmap

backend: Final[Literal["avx2", "neon", "scalar"]]

def parse(data: bytes, sections: int, /) -> Beatmap: ...
def parse_file(path: str | bytes | PathLike[str] | PathLike[bytes], sections: int, /) -> Beatmap: ...
