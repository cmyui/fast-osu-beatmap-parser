"""Select instructions once at import; parser calls need no runtime dispatch."""
from os import environ
from ._native_scalar import ffi, lib

SIMD_ENABLED = False
if environ.get("FOSU_FORCE_SCALAR") != "1" and lib.fosu_python_has_avx2():
    from ._native_avx2 import ffi, lib
    SIMD_ENABLED = True
