"""Load the native library and its process-local backend selection."""

from ._core import ffi as ffi
from ._core import lib as lib

_name = lib.fosu_backend_name()
if _name == ffi.NULL:
    raise ImportError("FOSU_BACKEND requests an unknown or unavailable backend (use auto, scalar, avx2 or neon)")
backend = ffi.string(_name).decode("ascii")
SIMD_ENABLED = backend != "scalar"
