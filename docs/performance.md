# Performance and verification

## Scope and measurement boundaries

The primary products are the C++ library, C ABI and Python package. The
freestanding executable measures the additional cost of creating a process,
reading one original beatmap, writing its complete result and exiting.

The evaluation set contains 10,000 ranked/approved beatmaps selected by
playcount: 402,593,897 original bytes and 8,070,193 retained hitobjects. Its
manifest fixes membership and provenance; maps and play data stay private.
Each file has equal weight in the latency mean, regardless of playcount.

Measurements below were taken on September 7, 2026 on a shared-tenancy AMD
EPYC Genoa/Zen 4 VM: eight cores without SMT, 32 MiB L3, 15 GB RAM, Ubuntu
24.04, Linux 6.8, glibc 2.39 and GCC 13.3. Runs use CPU 3 and are serialized
with a shared benchmark lock. The host enables 64/128/256 KiB anonymous
multi-size transparent huge pages in `madvise` mode. fosu requests huge pages
for its C ABI arena and one-shot executable; it does not change host policy.
The header-only interface uses the caller's allocator.

Tables report microseconds as **mean of per-file minima (mean of all runs)**.
Minima reduce interruption noise on the shared VM; observed means retain it.
Neither is a latency guarantee. Compare variants within the same table/run:
load, compiler layout and the number of loaded modules affect absolute times.

- **C++:** preloaded padded bytes; parse every selected section into a complete
  result. Fresh includes construction and destruction; reuse retains capacity.
  Variants and modes rotate within every file/repetition. A compiler barrier
  makes the complete result observable.
- **C ABI:** also includes copying/padding input and publishing the view.
  Fresh creates and frees a handle each call. Freed storage may be recycled,
  but no parsed result is reused. Independently loaded modules own separate
  spare arenas.
- **Warm Python:** package already imported, fresh owned `Beatmap` each call,
  `len(hit_objects)` and release included. `parse_file` includes the file read;
  `parse(bytes)` receives existing bytes. Lazy conversion of every field to
  Python objects is excluded.
- **First Python call:** a new interpreter imports fosu, calls `parse_file`
  once, reads the object count and releases the result. Import and first-call
  times are separate. Whole-process time also includes launch, interpreter
  startup, timing-output collection and exit. Package bytecode is precompiled
  with the measured interpreter, matching a normal installed package.
- **One-shot:** parent `posix_spawn` through `wait4`, including original input
  I/O, parsing, complete output writes to `/dev/null` and exit. Consumer
  decoding and disk persistence are excluded.

All file benchmarks warm the kernel file cache before timing. A fresh process
is not a cold disk read; CPU caches and predictors are not explicitly flushed.

## Kernel and arena measurements

The baseline is the compatible parser at `a9d7693`. The two independent
optimization attempts are `9676109` and `a10ac66`; the combined implementation
at `164d554` uses their best measured pieces plus storage-lifetime fixes.
All four preserve the baseline's complete output on this corpus. Release
`33aa2c0` predates correctness fixes and is not an identical-work baseline.

### C++: 10,000 files, nine repetitions, no PGO

| Result | Compatible baseline | Attempt A | Attempt B | Combined |
|---|---:|---:|---:|---:|
| Fresh | 23.532 (26.597) | 22.208 (25.187) | 21.635 (24.240) | **20.711 (23.245)** |
| Reused | 22.243 (25.046) | 20.881 (23.449) | 20.591 (22.789) | **19.661 (21.845)** |

The combined fresh-result time is 12.0% below the compatible baseline, 6.7%
below attempt A and 4.3% below attempt B in this run. GCC flags are `-O3
-march=x86-64-v3 -mtune=znver4 -fno-plt -fno-stack-protector`, with hidden
symbols in isolated benchmark modules.

### C ABI

Isolated implementation modules, all 10,000 files, nine repetitions:

| Result | Compatible baseline | Attempt A | Attempt B | Combined |
|---|---:|---:|---:|---:|
| Fresh handle | 23.392 (26.957) | 22.159 (25.213) | 22.312 (25.818) | **21.134 (24.350)** |
| Reused handle | 22.033 (24.682) | 21.050 (23.356) | 21.318 (23.408) | **20.181 (22.193)** |

The combined fresh call saves 9.7% against the compatible baseline, 4.6%
against attempt A and 5.3% against attempt B.

A separate fresh C process driver measures `dlopen` through read, parse, view,
free and `dlclose` on 1,000 evenly spaced files, three repetitions:

| Compatible baseline | Attempt A | Attempt B | Combined |
|---:|---:|---:|---:|
| 209.441 (232.707) | 207.619 (232.398) | 175.554 (199.087) | 191.906 (216.846) |

The combined result includes the corrected arena unload cleanup. Attempt B
leaves its spare mapping allocated after `dlclose`, so its faster teardown
figure does less resource cleanup. First Python-call timing ends before
library unload and must be considered separately. The corrected first C use
is 8.4% faster than the compatible baseline at this broader boundary.

### Python: CPython 3.11.15, bundled runtime, no PGO

Warm calls, all 10,000 files, five repetitions, five package variants rotated
within each file (the fifth was an experimental VBMI build):

| Call | Compatible baseline | Attempt A | Attempt B | Combined |
|---|---:|---:|---:|---:|
| `parse(bytes)` | 30.398 (34.750) | 27.804 (31.880) | 27.375 (31.958) | **26.759 (30.990)** |
| `parse_file(path)` | 35.369 (39.941) | 33.106 (37.230) | 32.751 (37.723) | **32.133 (36.191)** |

The combined bytes call saves 12.0% against the compatible baseline and 2.2%
against attempt B. File reads and Python wrapper work reduce the relative
benefit in `parse_file`. These are fresh logical results, even though the
loaded library can recycle storage between calls.

Fresh interpreters, 500 evenly spaced files, three repetitions, four rotating
packages after matching CPython bytecode-cache state:

| Boundary | Compatible baseline | Attempt A | Attempt B | Combined |
|---|---:|---:|---:|---:|
| First `parse_file` | 166.476 (180.390) | 163.701 (178.645) | 138.365 (154.062) | **138.411 (153.559)** |
| `import fosu`, ms | 5.146 (5.354) | 5.137 (5.365) | 5.130 (5.357) | 5.137 (5.346) |
| Whole process, ms | 15.406 (15.918) | 15.403 (15.959) | 15.363 (15.928) | 15.390 (15.911) |

The first call improves by 16.9% versus the compatible baseline; the combined
and attempt B results are effectively tied. Interpreter startup and imports
dominate the whole process, so the parser improvement barely moves its total.
Each interval's minimum is selected independently within a file; columns are
not components from one chosen execution and must not be added together.

### Profile-guided compilation

GCC PGO trains on 2,000 sorted corpus files and evaluates only the other
8,000, with five rotating repetitions. This is a separate paired run of the
combined implementation:

| Build | Fresh | Reused |
|---|---:|---:|
| Default | 19.140 (21.275) | 18.645 (20.272) |
| PGO | **18.506 (20.825)** | **18.018 (19.805)** |

PGO reduces the mean minima by about 3.3% here. Published wheels do not carry
this profile. The reproducible experiment below lets C++ applications measure
profiles for their own call sites and workload.

### One-shot process lifetime

All 10,000 files, three fresh processes per file, identical complete FOSUDMP5
output contract, GCC `-O2 -march=znver4`:

| Compatible baseline | Attempt A | Attempt B | Combined |
|---:|---:|---:|---:|
| 170.24 (191.73) | 167.15 (187.07) | 168.74 (189.02) | 168.15 (188.75) |

The combined executable is 81,296 bytes and records 6.5 minor faults at the
selected minima on average. Its mean minimum is 1.2% below the compatible
baseline; attempt A is 0.6% faster at this boundary. The shared implementation
is selected for its stronger primary library results, not a claim that it
wins every sub-microsecond process comparison.

The hosted reference serializer is a verification tool; its growing
`std::string` is not an optimized delivery path. Its runtime does not establish
the minimum cost of returning a native result. Older parse-and-exit figures
without output perform different work.

## Measured implementation choices

The shared hitobject loop packs prefix lengths for validation and table
selection, keeps vector constants at section scope, and tests uncommon line
shapes after the common editor format. Sliders convert their first two points
speculatively. Compile-time storage policies share parsing semantics between
native vectors, C ABI arena arrays and streamed one-shot records.

Independent two-way ablations, all 10,000 files with nine repetitions, measure
the incremental pieces. Each row is a separate run:

| Removed optimization | Combined fresh / reused | Ablated fresh / reused |
|---|---:|---:|
| SSE sample and edge-set classifiers | 19.486 / 18.942 | 20.361 / 19.802 |
| Numeric whitespace/bounds shortcuts | 19.714 / 19.207 | 20.252 / 19.707 |
| Direct vector writes (`FOSU_PORTABLE_VECTORS`) | 19.392 / 18.812 | 21.162 / 20.340 |

Direct vector writes depend on tested libstdc++/libc++ release layouts.
Explicit placement construction starts record lifetimes without redundant
zeroing. Debug containers, AddressSanitizer and unsupported layouts use normal
vector operations; callers can force that path with `FOSU_PORTABLE_VECTORS`.
The arena vectors have their own direct-size interface and remain exercised
under sanitizers. The C ABI caches at most one spare arena of up to 8 MiB per
loaded library image, releases it at unload, and preserves independent live
results. See [C++ storage](library.md#performance) and [C ABI storage](c-api.md#storage).

The optional AVX-512 VBMI prefix uses 256-bit byte permutations and a smaller
table. It was measured, then omitted. Dedicated Python A/B runs rotate AVX2
and VBMI within each map and repeat with the starting order reversed:

| Boundary | AVX2, order A/B | VBMI, order A/B | AVX2, order B/A | VBMI, order B/A |
|---|---:|---:|---:|---:|
| Warm bytes, 10k × 7 | 24.400 (28.005) | 24.802 (28.043) | 23.498 (27.641) | 23.731 (27.072) |
| Warm file, 10k × 7 | 31.451 (34.882) | 31.820 (35.419) | 30.574 (33.776) | 30.818 (33.799) |
| First file call, 500 × 3 | 135.271 (150.744) | 135.873 (150.128) | 135.204 (151.620) | 135.766 (150.850) |

AVX2 wins the warm minima in both orders. Observed means have interruption
noise; the first-call sub-microsecond differences disagree in direction
between minima and means. Whole-process Python time has no consistent winner.
A C++ paired run was effectively tied (fresh 19.181 vs 19.287 µs); streaming
favored AVX2 (24.713 vs 25.192). One-shot minima differed by less than 1%
(168.67 AVX2 vs 168.04 VBMI), while observed means favored AVX2. No measured
boundary establishes a durable VBMI benefit on this host.

Additional measured alternatives were rejected: AVX-512 VBMI2 delimiter
compression slowed fresh parsing from 19.121 to 20.112 µs; software prefetch
512 bytes ahead did not improve streaming (24.713 vs 24.804 µs/file, median
best round across seven rotated runs of 1,000 files × ten rounds). Wider
`-march=znver4` code generation alone did not beat the AVX2 library target.
These conclusions apply to this host and workload, not every CPU or map mix.

## Verification coverage

The current implementation matches `a9d7693` exactly across all 10,000 maps
for native, portable-vector, C ABI and one-shot output. Canonical SHA-256:
`334598db4c426ce5e99ba49cb9c4e3c038f88cb5424586ecd718207ec482edad`.
The comparison includes raw strings, float bits, all pools and indices,
orphaned points and all four parser counters.

Both Python entry points match every public field on all 10,000 maps. The
18 package tests pass in scalar and SIMD modes. Release, forced-portable,
debug-vector and ASan container-annotation builds pass, as do C ABI growth,
recycling, concurrent ownership, actual unload/reload and late host exit
callback checks. The one-shot boundary and resource-limit tests pass.

Clang ASan/UBSan/float-cast-overflow checks pass. A 61-second mutation fuzz
smoke completed 855,444 runs; the sanitizer-built scalar/SIMD/offset comparison
with an independently converted numeric reference passed all 10,000 files
(six malformed lines). Fuzz duration and corpus coverage are measured test
budgets, not proofs that every malformed input is safe.

Official acceptance is checked against ppy/osu revision
`48c4800e3ae4ee752452cdff83bd3787ccf3105f`. The 1,395 synthetic fixtures
agree in scalar and SIMD modes (969 rejected lines). This is acceptance
coverage, not proof of complete gameplay-value equivalence. The compatibility
baseline also passed an official-decoder audit of the 10k corpus and a broader
23,618-file cache; that cache is not independently labeled as an Aspire set.
[Compatibility](compatibility.md) explains the authority, supported semantics
and intentional differences from older releases.

## Verification and regression comparison

`oneshot/dump.hpp` serializes every logical field and pool index, raw string
bytes and float bits, including unreferenced points and all counters. The
standalone writer is independent of that serializer. Its Python decoder also
checks framing, counts and non-overlapping point-pool coverage. Same-version
representations must agree exactly; an old release is a regression baseline
whose intentional corrections require separate accounting.

```sh
make test test-c-api oneshot build/c_api_reference CXX=g++
make test-sanitize fuzz-smoke CXX=clang++
python3 tests/test_oneshot.py build/oneshot_reference \
  build/fosu_oneshot build/c_api_reference
python3 tests/test_oneshot_limits.py build/fosu_oneshot
for candidate in build/fosu_oneshot build/c_api_reference; do
  python3 bench/oneshot_verify.py build/oneshot_reference "$candidate" \
    /path/to/corpus --expected-files 10000
done
python3 bench/python_verify.py build/oneshot_reference /path/to/corpus
```

For a previous release, export its **whole tree** into a separate directory
and build its own reference/serializer there. Do not compile today's serializer
against old record declarations. The cross-format comparison widens v4 integer
timestamps to v5 doubles, but still compares all other float bits and fields:

```sh
mkdir -p build/previous
# Replace the revision with the release being evaluated.
git archive 33aa2c0 | tar -x -C build/previous
make -C build/previous oneshot CXX=g++
python3 bench/oneshot_verify.py build/previous/build/oneshot_reference \
  build/oneshot_reference /path/to/corpus --previous-format \
  --report build/regression-differences.json
```

The command returns failure if differences exist, and the optional report
collects all affected files. Do not erase discrepancies by rounding floats or
omitting fields. [Compatibility](compatibility.md) documents the independent
references and intentional numeric behavior. Keep detailed corpus reports on
the corpus host; publish aggregate results without private maps or play data.

To compare scalar and SIMD fields under sanitizers on a broader local corpus:

```sh
clang++ -std=c++20 -O1 -g -Iinclude -mavx2 -mbmi -mbmi2 \
  -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
  tests/validate_corpus.cpp -ldl -o build/validate_corpus
build/validate_corpus /path/to/maps
```

An optional independently converted numeric reference uses libc `strtod` over
bounded copies. It retains the framing/storage code, so it isolates numeric
conversion correctness rather than validating the entire format independently:

```sh
clang++ -std=c++20 -O1 -g -Iinclude -fPIC -fvisibility=hidden -shared \
  -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
  bench/numeric_oracle.cpp -o build/numeric_oracle.so
build/validate_corpus /path/to/maps ./build/numeric_oracle.so
```

This compares every serialized field except fast/slow path counters, which
necessarily differ. It checks two output representations and reports malformed
record counts. `fuzz-smoke` starts with synthetic numeric/format seeds and
mutates full files plus forced timing/hitobject sections. The native CI job
runs these checks independently of wheel installation tests.

## Reproduce paired library and Python measurements

Export each complete revision into a separate source directory. Compile each
variant against exactly its own include root; mixing roots silently measures
the wrong implementation. Give benchmark modules hidden symbols and compile
C API implementation code into each module, rather than linking both wrappers
to one loader-resolved dependency.

```sh
g++ -std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt \
  -fno-stack-protector -Iinclude -fPIC -fvisibility=hidden -shared \
  bench/library_module.cpp -o build/library_native.so
g++ -std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt \
  -fno-stack-protector -Iinclude -fPIC -fvisibility=hidden -shared \
  -DFOSU_BENCH_CAPI bench/library_module.cpp src/c_api.cpp \
  -Wl,-Bsymbolic -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL \
  -o build/library_capi.so
g++ -std=c++20 -O2 -Iinclude bench/library_compare.cpp -ldl -o build/library_compare
taskset -c 3 build/library_compare /path/to/corpus 9 \
  /path/to/previous/build/library_native.so build/library_native.so > build/library.csv
python3 bench/library_summary.py build/library.csv
```

Use `FOSU_BENCH_OFFSET` for the compact C++ representation. The C API modules
include allocation, input copying, padding and view publication. A single
library can also be profiled with `build/c_api_loop`; its separate process
runs are not a substitute for paired variant rotation.

Build Python wheels from separate clean source copies with the same compiler
and `FOSU_BUNDLE_RUNTIME=1`. Extract each wheel into a separate package
directory; install CFFI in the driver's environment. Use the same interpreter
to precompile bytecode in every package before first-use comparisons:

```sh
python -m compileall -q /path/to/previous/fosu /path/to/current/fosu
taskset -c 3 python bench/python_versions.py /path/to/corpus \
  /path/to/previous /path/to/current --reps 7
taskset -c 3 python bench/python_first_compare.py /path/to/corpus \
  /path/to/previous /path/to/current --limit 500 --reps 3
```

Both harnesses rotate variants within each file/repetition. Repeat with package
arguments reversed to check starting-order effects. Keep compiler, Python,
bytecode-cache state, CPU affinity and package-loading conditions identical.

```sh
# GCC/Linux; the outer taskset also pins training.
taskset -c 3 make bench-pgo CXX=g++ BENCH_ARGS=/path/to/corpus
```

PGO trains on sorted indices 0, 5, 10, … and evaluates the remaining 80%, with
five paired repetitions per evaluation file. Training and evaluation files are
disjoint. Profiles contain code-generation feedback, not cached map results.
Train an application at its own call sites before assuming this benefit.

## Streaming, first-use and process harnesses

`profile_parse` reads an evenly spaced subset before timing, then visits every
file in each round. This exposes a different working set from repeatedly
parsing one file. Complete results remain observable to the compiler.

```sh
make build/profile_parse CXX=g++
taskset -c 3 perf stat -e cycles:u,instructions:u,branch-misses:u -- \
  build/profile_parse /path/to/corpus 1000 20 fresh
taskset -c 3 build/profile_parse /path/to/corpus 1000 20 reuse 0x100
```

The optional section mask above selects `[HitObjects]`. `make bench` with a
corpus preloads the complete set (403 MB here, larger than L3); without a
corpus it uses synthetic input and is only a smoke check.

```sh
make build/coldstart_x86 lib CXX=g++
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Iinclude \
  bench/c_api_first.c -ldl -o build/c_api_first
FOSU_LIBRARY=build/libfosu.so taskset -c 3 python3 bench/library_first_compare.py \
  /path/to/corpus build/coldstart_x86 build/c_api_first --limit 1000 --reps 3 \
  > build/first.csv
python3 bench/library_summary.py build/first.csv
```

These two rows have deliberately different boundaries: `coldstart_x86` times
only the first C++ parse after reading; `c_api_first` times `dlopen`, file I/O,
parse, view, free and `dlclose`. Neither includes process startup itself.
For multiple C ABI revisions, compile a driver per library with
`FOSU_DEFAULT_LIBRARY` set to its absolute path. `FOSU_PERF=1` enables optional
hardware counters in the C++ driver when the host permits them.

```sh
make oneshot CXX=g++
sh oneshot/build.sh build/fosu_oneshot_4k -DABLATE_NO_MADVISE
taskset -c 3 build/oneshot_process /path/to/corpus 0 3 \
  /path/to/previous/build/fosu_oneshot build/fosu_oneshot build/fosu_oneshot_4k \
  > build/process.csv
python3 bench/oneshot_summary.py build/process.csv
```

Arguments are `corpus limit reps binaries...`; zero selects every file, and a
positive limit selects evenly spaced sorted files. The `_4k` variant omits
huge-page advice without changing host policy. See [one-shot setup](../oneshot/README.md).
An empty executable measures launch overhead, not an attainable parse result.
These measurements do not prove that no faster design can exist.
