# Building and checking fosu

The header-only C++ interface needs no build step: add `include` to your include
path and compile as C++20. The Makefile builds the C ABI and development tools.
The Python package uses its own setuptools/CFFI build (see [Python](python.md)).

Public headers live directly under `include/fosu/`. The `internal/` directory
contains their numeric conversion, section parsing, SIMD, and storage
implementation; the public headers include these automatically.

```sh
make                     # C ABI shared library only
make -j4 test            # native parser, C ABI, ownership and failure checks
make -j4 bench-build     # benchmark programs; does not run measurements
make -j4 references      # independent canonical writers and corpus checks
```

Clang is the default compiler. Set `CXX=g++ CC=gcc` to use GCC. Compiler
commands, flags and compiler versions are recorded in `config.json` alongside
the binaries; each successful output records that configuration in `.build.json`.
Changes rebuild that output even after a partial build or within a single
filesystem timestamp tick.
Changing headers also rebuilds their dependents.

## Configurations

Outputs live under `build/<profile>-<isa>-<runtime>/`. `BUILD_DIR=...` selects
another directory, which still tracks its effective commands. `make clean`
removes only the selected configuration.

| Option | Choices | Default |
|---|---|---|
| `PROFILE` | `release`, `portable`, `debug`, `sanitize` | `release` |
| `ISA` | `avx2`, `scalar` | AVX2 on x86-64, scalar elsewhere |
| `LIB_RUNTIME` | `bundled`, `shared` | Bundled on Linux; shared on macOS and sanitizer builds |

Release uses `-O3`. Portable keeps release optimization but uses public vector
operations. Debug uses `-O0 -g` and libstdc++ debug containers. Sanitize uses
`-O1 -g`, ASan, UBSan, float-cast checks and vector annotations. Tests keep
assertions enabled even if user flags contain `-DNDEBUG`.

AVX2 builds target **x86-64-v3**, with Zen 4 scheduling, `-fno-plt` and
`-fno-stack-protector` on Linux. Scalar x86 builds target baseline x86-64;
Apple Silicon builds use the native scalar path. These are compile-time choices
for C/C++; only the Python wheels select an ISA at runtime. A header-only
consumer controls its own optimization and hardening flags.

```sh
make CXX=g++ ISA=scalar test
make CXX=g++ PROFILE=portable test-parser
make CXX=g++ PROFILE=debug test-parser
make CXX=clang++ PROFILE=sanitize test
make CXX=clang++ PROFILE=sanitize fuzz-smoke FUZZ_SECONDS=60
make LIB_RUNTIME=shared test-c-api
make test-rosetta         # optional x86-64 build/run on Apple Silicon
```

Rosetta testing requires AVX2 translation support (macOS 15+). Ordinary macOS
checks use the native architecture. The sanitizer profile skips the C ABI
address-space-limit test because ASan reserves a large virtual address range,
and the exact export-list check because instrumentation adds symbols;
release checks cover that failure path. Use Clang for libFuzzer.

`CXXFLAGS`, `CFLAGS`, `CPPFLAGS` and `LDFLAGS` accept normal compiler overrides.
The selected ISA/profile flags are appended to `CXXFLAGS`. `CPPFLAGS` supplied
on the command line must include `-Iinclude`.

## One-shot process

This separate target requires GCC and Linux x86-64. Its `-O2`, fixed register,
custom entrypoint, syscall and runtime settings stay in `oneshot/build.sh`.
They are never applied to the library or Python extension.

```sh
make CXX=g++ oneshot test-oneshot
build/release-avx2-bundled/fosu_oneshot map.osu > map.fosu
python3 oneshot/decode.py < map.fosu
```

The production target is Zen 4. To test on an x86-64-v3 CI runner:

```sh
make CXX=g++ ONESHOT_FLAGS=-march=x86-64-v3 test-oneshot
```

`ONESHOT_CXX` selects its GCC executable; `ONESHOT_FLAGS` appends diagnostic or
ISA overrides. The syscall runtime is intentionally specific to this target.

## Test responsibilities

- `test_build.py`: compiler-flag changes, partial builds and idle reuse.
- `test_numeric.cpp`: bounded conversion, prefix and timing-shape equivalence.
- `test_sections.cpp`: metadata, object kinds, omitted-section defaults and section selection.
- `test_storage.cpp`: growth, lifetime, shrinking/growing result reuse, stale-state reset and both record layouts.
- `test_hardening.cpp` and `fuzz_parser.cpp`: malformed input and scalar/SIMD parity.
- C ABI tests: all field values, independent/concurrent handles, failures,
  recycling, unload, and late host exit callbacks.
- Python tests: installed API, ownership, errors, array views and generated types.
- `test_oneshot*.py`: complete stream equality, I/O boundaries and explicit limits.
- `test_official.py`: acceptance against the pinned official legacy decoder.

Each parser test defines its own input beside its assertions. Section omission,
selection, and reuse are separate cases, so their expected behavior can be read
without following shared beatmap fixtures.

`tests/support/canonical_dump.hpp` independently serializes all logical fields,
float bits, counters and pool indices. It is a test oracle for equality between
representations, not the authority on valid osu! input. The official decoder
harness lives in `tests/reference/official`; see [compatibility](compatibility.md).

CI exercises these profiles on Linux and native Apple Silicon. Official-reference
checks install the Python package from its source archive. Wheel builds
run the installed suite on CPython 3.10; downloaded wheels are tested again on
CPython 3.14. Both automatic CPU selection and forced scalar imports are tested.
CI produces GitHub artifacts and does not publish packages to PyPI.
