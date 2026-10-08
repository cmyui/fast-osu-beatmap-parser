# Building and checking FOSU

Requires C++20, CMake 3.26+, and Linux, macOS or Windows. Header-only consumers can
include `src/fosu/parser.h` without building a library.

## Checks

`make test` runs every check of parser behaviour that works on one machine:

| Target | Runs | Needs |
|---|---|---|
| `make test-native` | CTest (`check`): C++ parser tests on both engines, engine selection, and on Linux, exported-symbol and binary-hardening checks | CMake and a C++20 compiler |
| `make test-python` | `tests/test_python.py`, including mypy and stubtest, on both backends against the package reinstalled into `build/venv` | Python 3.10+ (`PYTHON=` selects the interpreter) |
| `make test-official` | Comparisons with osu!'s own decoder on both backends: synthetic field cases (`tests/test_official.py`), then acceptance and field-level values for the test beatmaps in the pinned osu! checkout | .NET 10 SDK as `dotnet`; the first run fetches the pinned osu! revision |

Each parsing or calculation rule has a named C++ test (`tests/test_*.cc`) next
to the code it covers: sections and fields, numbers, storage and parser reuse,
slider timing and events, slider paths, stacking and mods. Expected values that
follow osu! are checked against its decoder when the test is written.
`tests/test_python.py` covers only the Python interface: conversion to Python
objects, `None` for missing values, object lifetime, errors, buffers, and the
logic implemented in the binding (bookmark and tag lists, `control_points`,
`raw_position()` and `slider_position_at()`).

CI runs the same checks, plus checks that need other platforms or builds: macOS
and Windows; scalar, Debug, sanitizer and fuzzing builds; AArch64 and non-AVX
dispatch under emulation; installed C++ consumers (`tests/test_build.py`);
wheels on five platforms; and benchmark tools. Full-corpus comparisons with osu!
run only locally, because the corpus is not in the repository.

## Native

```sh
cmake -S . -B build/native
cmake --build build/native --target check -j4
```

Use `cmake --build build/native --target test_sections` followed by
`build/native/test_sections` for a focused section test. Other targets are
listed in [Development.cmake](../cmake/Development.cmake).

Useful configurations (use separate build directories):

- `-DFOSU_ISA=scalar`: scalar-only build. Default `auto` includes the host's
  optimized engine. `avx2` and `neon` select their respective architectures.
- `-DCMAKE_BUILD_TYPE=Debug`: debug checks.
- `-DFOSU_SANITIZE=ON -DCMAKE_CXX_COMPILER=clang++`: sanitizers.
  Build `check` and, for relevant memory-safety changes, `fuzz-smoke`.
- `-DCMAKE_CXX_COMPILER=g++`: choose a compiler explicitly.

Compiled products select an engine at runtime. Header-only tests and benchmark
tools select instructions at compile time; run AVX2 tools only on supported CPUs.
Build flags and platform protections live in [CMakeLists.txt](../CMakeLists.txt)
and [Hardening.cmake](../cmake/Hardening.cmake).

## Installed C++ consumers

```sh
cmake --build build/native -j4
cmake --install build/native --prefix /path/to/install
python3 tests/test_build.py build/native
```

Use `find_package(fosu CONFIG REQUIRED)` with the installation on
`CMAKE_PREFIX_PATH`. Link `fosu::headers` for header-only use or `fosu::fosu`
for runtime engine selection. Distribute the runtime and its adjacent engine
libraries together. See [C++ usage](library.md).

## Python and formatting

```sh
python -m pip install '.[test]'
python -m pytest tests/test_python.py
python -m mypy --config-file pyproject.toml
pre-commit install
pre-commit run --all-files
```

Reinstall after native changes before testing the installed Python API.
`FOSU_BACKEND=scalar` forces scalar selection before Python import.
`python -m build` builds the source archive and wheel when packaging needs checking.
Release Linux wheels are built with the packaged GCC 15 toolchain and bundle the
C++ runtime; source installs use the selected local compiler and can request
runtime bundling with `FOSU_BUNDLE_RUNTIME=1`.

Use focused checks during iteration; see [AGENTS.md](../AGENTS.md) for scope.
The [official reference audits](../tests/reference/official/README.md) document
the osu! comparisons, and [performance](performance.md) describes measurement
entry points.
