# Building and checking fosu

The header-only C++ interface needs no build step: add `include` to your include
path and compile as C++20. CMake 3.26+ builds the C ABI, development tools, and
Python extensions. Python installations invoke CMake through scikit-build-core;
CFFI generates the wrapper source and does not compile it independently.

Public headers live directly under `include/fosu/`. The `internal/` directory
contains their numeric conversion, section parsing, SIMD, and storage
implementation; the public headers include these automatically.

```sh
cmake -S . -B build/native -G Ninja
cmake --build build/native -j4                       # C ABI shared library
cmake --build build/native --target check -j4        # build and run native tests
cmake --build build/native --target bench-build references -j4
```

Ninja is optional; omit `-G Ninja` to use CMake's default generator. CMake selects
the compiler from the environment. To choose explicitly, configure with
`-DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc`. Use a separate build directory
when changing compilers or target architectures.

CMake tracks compiler flags, source and header dependencies. Effective commands
are available in `build/native/compile_commands.json`, configuration in
`CMakeCache.txt`, and verbose output with `cmake --build build/native --verbose`.
Tests and benchmark programs are built only when their targets are requested.

## Configurations

| CMake option | Choices | Default |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release`, `Debug` | `Release` |
| `FOSU_ISA` | `auto`, `avx2`, `scalar` | `auto` |
| `FOSU_PORTABLE_VECTORS` | `ON`, `OFF` | `OFF` |
| `FOSU_SANITIZE` | `ON`, `OFF` | `OFF` |
| `FOSU_BUNDLE_RUNTIME` | `ON`, `OFF` | On for native Linux release builds and release wheels; off for local Python source builds, macOS and sanitizers |

Release C++ uses `-O3`; C benchmark launchers use `-O2`. Debug uses `-O0 -g` and
libstdc++ debug containers. Portable vectors use public vector operations.
Sanitizers use `-O1 -g`, ASan, UBSan, float-cast checks and vector annotations.
Tests keep assertions enabled. Standard `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, and
linker flag variables accept additional compiler options.

Compiled C API and Python products use runtime CPU selection with `FOSU_ISA=auto`:
x86-64 builds include scalar and AVX2 backends; Apple Silicon includes scalar.
`scalar` omits AVX2; `avx2` requires AVX2 at first use unless overridden by
`FOSU_BACKEND`. Unsupported forced requests fail instead of executing invalid
instructions. Both products require x86-64-v3 CPU features and OS XMM/YMM support
before selecting AVX2.

Native AVX2 backend code targets **x86-64-v3**, with Linux Zen 4 scheduling,
`-fno-plt` and `-fno-stack-protector`. Python AVX2 code retains
`-mavx2 -mbmi -mbmi2` and Linux Zen 4 scheduling. Python uses `-O3 -g0` and Linux
`-fno-plt`. Dispatch code and scalar backends target baseline x86-64.
Header-only tests, references and benchmarks remain compile-time selected:
`auto` uses AVX2 on x86-64 and scalar elsewhere. Run those AVX2 tools only on
supported CPUs, or configure `FOSU_ISA=scalar`.
A header-only consumer controls its own optimization and hardening flags.

```sh
cmake -S . -B build/scalar -DFOSU_ISA=scalar
cmake --build build/scalar --target check -j4
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --target test-binaries -j4
ctest --test-dir build/debug -L parser --output-on-failure
cmake -S . -B build/sanitize -DFOSU_SANITIZE=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/sanitize --target check fuzz-smoke -j4
```

`-DFOSU_FUZZ_SECONDS=60` controls the fuzz smoke duration; libFuzzer requires
Clang. Sanitizer builds skip Linux address-space-limit and exact export-list
checks; release builds cover those cases. Ordinary macOS checks use native ARM.
For Rosetta testing, configure a separate build with
`-DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_SYSTEM_NAME=Darwin
-DCMAKE_OSX_ARCHITECTURES=x86_64 -DFOSU_ISA=avx2
-DCMAKE_CROSSCOMPILING_EMULATOR="arch;-x86_64"`; AVX2 translation needs macOS 15+.

## Installed CMake consumers

```sh
cmake --install build/native --prefix "$HOME/.local"
python3 tests/test_build.py build/native
```

Downstream projects can use `find_package(fosu CONFIG REQUIRED)` with
`CMAKE_PREFIX_PATH` pointing at the installation. Link `fosu::headers` for the
header-only C++ API, or `fosu::fosu` for the compiled C ABI. The header-only target
sets the include directory and C++20 requirement without imposing CPU flags.

## Python packages

```sh
python3 -m pip install .
python3 -m build                 # source archive, then wheel from that archive
```

Build isolation supplies scikit-build-core, CFFI, and a suitable CMake/Ninja when
needed. An installed C/C++ compiler and Python development headers are required
for source builds. The wheel contains one extension with scalar and AVX2 backends on x86-64,
or scalar on Apple Silicon, using the CPython 3.10+ stable ABI.
`FOSU_BUNDLE_RUNTIME=1` bundles the Linux C++ runtime; cibuildwheel enables this
by default. `CMAKE_ARGS` or pip's `-Ccmake.define.NAME=VALUE` can configure CMake.

## One-shot process

This separate target requires GCC and Linux x86-64. Its `-O2`, fixed register,
custom entrypoint, syscall and runtime settings stay in `oneshot/build.sh`.
They are never applied to the library or Python extension.

```sh
cmake -S . -B build/native
cmake --build build/native --target check-oneshot -j4
build/native/fosu_oneshot map.osu > map.fosu
python3 oneshot/decode.py < map.fosu
```

The production target is Zen 4. For x86-64-v3 CI runners configure with
`-DFOSU_ONESHOT_FLAGS=-march=x86-64-v3`. `FOSU_ONESHOT_CXX` selects GCC and
`FOSU_ONESHOT_FLAGS` appends diagnostic or ISA overrides.

## Test responsibilities

- `test_build.py`: installed header-only and compiled CMake targets.
- `test_numeric.cpp`: bounded conversion, prefix and timing-shape equivalence.
- `test_sections.cpp`: metadata, object kinds, omitted sections and selection.
- `test_storage.cpp`: growth, lifetime, reuse and both record layouts.
- `test_hardening.cpp` and `fuzz_parser.cpp`: malformed input and scalar/SIMD parity.
- `test_dispatch.cpp`: CPU/OS feature requirements, concurrent first use, forced
  selection and unsupported requests; CI also exercises a CPU without AVX via QEMU.
- C ABI tests: field values, concurrency, failures, recycling and unload.
- Python tests: installed API, ownership, errors, array views and generated types.
- `test_oneshot*.py`: complete stream equality, I/O boundaries and limits.
- `test_official.py`: acceptance against the pinned official legacy decoder.

Each parser test defines its own input beside its assertions. The canonical
dump checks representation equality; the official decoder is the acceptance
reference. See [compatibility](compatibility.md).

CI exercises Linux and native Apple Silicon, including sanitizer builds.
Official checks install from the source archive. Wheels are tested on CPython
3.10 and 3.14 with both automatic selection and forced scalar imports. CI uploads
GitHub artifacts and does not publish packages to PyPI.
