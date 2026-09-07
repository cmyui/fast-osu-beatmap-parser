"""Select instructions once at import; parser calls need no runtime dispatch."""

from os import environ

from ._native_scalar import ffi as ffi
from ._native_scalar import lib as lib

SIMD_ENABLED = False
if environ.get("FOSU_FORCE_SCALAR") != "1" and lib.fosu_python_has_avx2():
    from ._native_avx2 import ffi as ffi
    from ._native_avx2 import lib as lib

    SIMD_ENABLED = True
