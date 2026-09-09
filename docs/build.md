# Building and checking fosu

The header-only C++ interface needs no build step: add `src` to your include
path and compile as C++20. CMake 3.26+ builds the C ABI, development tools, and
Python extensions. Python installations invoke CMake through scikit-build-core;
The Python extension constructs detached Python values directly from the C++ Beatmap.

Sources live under `src/fosu/`, grouped by responsibility. C++ headers use `.h`
and compiled C++ files use `.cc`:

```text
src/fosu/
    parser.h, beatmap.h, parse_options.h, result.h
    arena.h, os.h, io.h, beatmap_header.h
    engine/
      parsing_engine.h, parse_document.h
      sections/       # General, Editor, Metadata, Difficulty, Events, etc.
      hit_objects/    # Common fields, samples, object types, and sliders
      timing_points/  # Individual timing points and beat-length rules
      parsing/        # Key/value fields, numbers, lines, and string lookup
      primitives/     # Vector operations, byte scanning, and packed digits
      runtime/        # CPU detection and dynamic engine loading
      third_party/    # Unmodified fast_float dependency and attribution
    bindings/     # C ABI and detached Python value conversion
```

C++ callers include `<fosu/parser.h>`; C callers include
`<fosu/bindings/c_api.h>`. Engine and storage helpers are implementation details,
included automatically by the header-only interface. CMake explicitly lists
the headers needed by consumers and installs them under `include/fosu/`;
compiled-only loader headers and `.cc` files are not installed. Installed
consumers use the same include names as source-tree consumers.

The `Parser` owns input preparation, storage, and result lifetimes. Its selected
engine owns the complete document parse. Each section parser consumes its body
and returns the next section header or EOF; optimized sections can fuse line
scanning with record parsing. General, Editor, Metadata, and Difficulty own their
field tables and acceptance rules, sharing key/value iteration and typed field
assignment. Domain-specific SIMD algorithms stay beside the format rules they
implement; `primitives/` contains reusable operations, not all optimized code.
The shared document flow is compiled once for each ISA,
without a scalar/SIMD mode parameter. Tests compare it with a separately compiled
scalar engine whose implementation symbols are isolated from the SIMD build.

## Formatting and Python typing

Install `pre-commit` (CI uses version 4.6.0), then enable the Git hook for your
checkout:

```sh
pre-commit install
pre-commit run --all-files
```

The hook installs the pinned clang-format version and applies the repository's
`.clang-format` to first-party C/C++ files. The vendored `fast_float.h` is
excluded. Commits with formatting changes are stopped so you can review and
stage the fixes before committing again. CI runs the same configuration against
all tracked files and fails if formatting would change them.

The same hook configuration runs pinned mypy against the entire shipped Python
package and `tests/typing`, using strict settings from `pyproject.toml`. The
Python check does not build or import the native extension. Benchmark drivers
and reference utilities are outside this package-typing check.

## Native builds

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
| `FOSU_ISA` | `auto`, `avx2`, `neon`, `scalar` | `auto` |
| `FOSU_PORTABLE_VECTORS` | `ON`, `OFF` | `OFF` |
| `FOSU_SANITIZE` | `ON`, `OFF` | `OFF` |
| `FOSU_BUNDLE_RUNTIME` | `ON`, `OFF` | On for native Linux release builds and release wheels; off for local Python source builds, macOS and sanitizers |

Release C++ uses `-O3`; C benchmark launchers use `-O2`. Debug uses `-O0 -g` and
libstdc++ debug checks. Sanitizers use `-O1 -g`, ASan, UBSan and float-cast checks.
Tests keep assertions enabled. Standard `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, and
linker flag variables accept additional compiler options.

Compiled C API and Python products use runtime CPU selection with `FOSU_ISA=auto`:
x86-64 builds contain the scalar engine in the core and an adjacent AVX2 shared
library; AArch64 builds use the same layout with NEON. Only the selected
optimized library is loaded. Install/distribute both files together.
`scalar` omits optimized libraries; `avx2` and `neon` require their respective
backend at first use unless overridden by `FOSU_BACKEND`. Unsupported forced
requests fail instead of executing invalid instructions. Both products require x86-64-v3 CPU features and OS XMM/YMM support
before selecting AVX2.

Native AVX2 backend code targets **x86-64-v3**, with Linux Zen 4 scheduling
and `-fno-plt`. Python AVX2 code retains
`-mavx2 -mbmi -mbmi2` and Linux Zen 4 scheduling. Python uses `-O3 -g0` and Linux
`-fno-plt`. On x86-64, dispatch code and scalar backends target the baseline ISA.
Header-only tests, references and benchmarks remain compile-time selected:
`auto` uses AVX2 on x86-64, NEON on AArch64 and scalar elsewhere. Run those
AVX2 tools only on supported CPUs, or configure `FOSU_ISA=scalar`.
NEON uses baseline AArch64 instructions, including two-register table shuffles;
it does not require dot-product, SVE or SME extensions. Apple arm64 guarantees
NEON; Linux selection checks the OS-provided ASIMD capability. The scalar build
disables explicit parser fast paths; the compiler and system libraries can
still use vector instructions.
A header-only consumer controls its own optimization and hardening flags;
`FOSU_DISABLE_SIMD` disables its explicit parser SIMD paths.

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

## Hardening

Compiled C ABI and Python products, including every parser backend, use
`-fstack-protector-strong`. Native library benchmarks use the same protections.
Optimized builds enable `_FORTIFY_SOURCE=3` when the Linux compiler and libc
support it, otherwise level 2; macOS uses level 2. Debug and sanitizer builds
omit fosu's fortification flags. Linux also enables stack-clash protection,
full RELRO with eager binding, and a non-executable stack. Shared libraries
are position independent; native executables use PIE on Linux.

These are mitigations, not a guarantee of memory safety: canaries cover selected
stack frames, and fortification checks operations whose object bounds the
compiler can determine. The parser's validation, bounds checks and sanitizer
tests remain necessary. Header-only consumers choose their own hardening policy.
The freestanding one-shot has a separate, deliberately aggressive build.

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

Build isolation supplies scikit-build-core and a suitable CMake/Ninja when
needed. An installed C/C++ compiler and Python development headers are required
for source builds. The wheel contains a CPython 3.10+ stable-ABI extension with
the scalar engine, plus an adjacent AVX2 library on x86-64 or NEON library on
Apple Silicon.
`FOSU_BUNDLE_RUNTIME=1` bundles the Linux C++ runtime; cibuildwheel enables this
by default. `CMAKE_ARGS` or pip's `-Ccmake.define.NAME=VALUE` can configure CMake.

## One-shot process

This separate target requires GCC and Linux x86-64. Its `-O2`,
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
- `test_numeric.cc`: bounded conversion, prefix and timing-point equivalence.
- `test_sections.cc`: metadata, object kinds, omitted sections and selection.
- `test_storage.cc`: growth, lifetime, reuse and both record layouts.
- `test_hardening.cc` and `fuzz_parser.cc`: malformed input and scalar/SIMD parity.
- `test_dispatch.cc`: CPU/OS feature requirements, concurrent first use, forced
  selection and unsupported requests; CI also exercises a CPU without AVX via QEMU.
- C ABI tests: field values, concurrency, failures, recycling and unload.
- `test_binary_hardening.py`: Linux C ABI and installed-wheel ELF protections
  (RELRO, eager binding, non-executable stack, no writable executable load
  segments or text relocations, and emitted stack-canary support).
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
