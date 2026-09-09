# Building and checking FOSU

Requires C++20, CMake 3.26+, and Linux or macOS. Header-only consumers can
include `src/fosu/parser.h` without building a library.

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
Release Linux wheels bundle the C++ runtime; source installs can request this
with `FOSU_BUNDLE_RUNTIME=1`.

Use focused checks during iteration; see [AGENTS.md](../AGENTS.md) for scope.
[Compatibility](compatibility.md) documents official-reference checks, and
[performance](performance.md) describes measurement entry points.
