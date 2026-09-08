from os import PathLike

from ._model import Beatmap

backend: str

def parse(data: bytes, /) -> Beatmap: ...
def parse_file(path: str | bytes | PathLike[str] | PathLike[bytes], /) -> Beatmap: ...
