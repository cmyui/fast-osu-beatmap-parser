"""CFFI build hooks; package metadata lives in pyproject.toml."""

import platform
from typing import Any

from setuptools import setup

builders = ["python/build_ffi.py:scalar"]
if platform.machine().lower() in ("x86_64", "amd64"):
    builders.append("python/build_ffi.py:avx2")
# The translation units include the parser and C API through headers. Distutils
# does not track that graph or environment-selected runtime link flags. Always
# rebuild so a wheel cannot contain stale native code from an earlier build.
options: dict[str, dict[str, Any]] = {
    "bdist_wheel": {"py_limited_api": "cp310"},
    "build_ext": {"force": True},
}
setup(cffi_modules=builders, options=options)
