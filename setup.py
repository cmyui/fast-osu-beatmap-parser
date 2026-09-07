"""CFFI build hooks; package metadata lives in pyproject.toml."""
import platform
from setuptools import setup

builders = ["python/build_ffi.py:scalar"]
if platform.machine().lower() in ("x86_64", "amd64"):
    builders.append("python/build_ffi.py:avx2")
setup(cffi_modules=builders, options={"bdist_wheel": {"py_limited_api": "cp310"}})
