# Performance and verification

## Workload and boundaries

The evaluation set contains 10,000 of the most-played ranked/approved beatmaps
on a private server: 402,593,897 original input bytes. A manifest retained with
the corpus fixes its membership and provenance. The corpus and play data are
not distributed. Each file has equal weight in the latency mean; this is not
weighted by its historical playcount.

Measurements use a shared-tenancy AMD EPYC Genoa/Zen 4 VM, eight cores without
SMT, 32 MiB L3, 15 GB RAM, Ubuntu 24.04, Linux 6.8, glibc 2.39 and GCC 13.3.
Runs are pinned to one CPU. Other tenants can interrupt execution, so both
observed means and means of per-file minima are reported. Minima estimate an
uninterrupted run; they are not a latency guarantee or a replacement for the
observed distribution. The date and result boundary matter when comparing runs.

There are three separate measurements:

1. **Process lifetime:** `posix_spawn` through `wait4`, including input I/O,
   allocation, parsing, output writes and exit. Each repetition uses a fresh
   process. The harness rotates binaries within every file/repetition and sends
   stdout to `/dev/null`; consumer decoding and disk persistence are excluded.
   It reads each input before timing, making input resident in the kernel file
   cache. CPU caches/predictors are not explicitly flushed.
2. **In-process parsing:** modules are loaded once; padded input is acquired
   before timing. Each current file is parsed repeatedly, rotating modules and
   fresh/reused result modes. Fresh mode includes result destruction; reuse
   retains capacity. Input is generally cache-resident. DSO entry points keep
   the complete result observable to the compiler.
3. **First library use:** a fresh process reads a file and times its first C++
   parse, excluding startup, read and destruction. A separate C driver times
   `dlopen` through read/parse/view/free/`dlclose`; that broader interval includes
   library loading and I/O. Neither includes Python interpreter startup.

## Current measurements (kernel restructure and arena handle)

September 7, 2026; `a9d7693` (the previous section's "current" implementation)
versus this revision, GCC 13.3, no PGO unless stated. Every comparison pairs
the two builds on the same files and CPU 3, rotating variants within each
file/repetition or interleaving whole runs. Times are microseconds; means of
per-file minima are shown with means of all runs in parentheses. Output is
byte-identical to `a9d7693` on the whole corpus (SHA-256
`334598db4c426ce5e99ba49cb9c4e3c038f88cb5424586ecd718207ec482edad`), so
these compare identical-output implementations.

### In-process C++ (`library_compare`, 10,000 files, nine repetitions)

| Workload | `a9d7693` | Current | Change |
|---|---:|---:|---:|
| Fresh `Beatmap` per parse | 21.446 (23.977) | 20.308 (22.473) | −5.3% (−6.3%) |
| Reused `Beatmap` | 20.835 (22.813) | 19.664 (21.344) | −5.6% (−6.4%) |

A streaming subset driver (`bench/profile_parse.cpp`: 1,000 evenly spaced
files, fresh results, best of ten rounds, seven interleaved run pairs) puts
the medians at 28.2 versus 25.8 µs per file (−8.8%). Per round of 1,000
files, hardware counters attribute the change to the `[HitObjects]` section:
305.5M → 261.0M instructions (−14.6%), 88.1M → 80.1M cycles (−9.1%) and
553k → 441k branch mispredictions; `[TimingPoints]` moved from 44.4M to 41.0M
instructions and 16.1M to 15.4M cycles. Both regimes matter: the paired
run keeps each input cache-resident, the driver streams inputs from L3.

### C ABI

| Boundary | `a9d7693` | Current | Change |
|---|---:|---:|---:|
| Fresh handle per parse (`c_api_loop`, 1,000 files × 20, five interleaved runs, medians) | 22.80 (25.95) | 21.58 (23.88) | −5.4% (−8.0%) |
| First use: `dlopen` through parse, free, `dlclose` (1,000 files × 3) | 258.2 | 214.8 | −16.8% |

The first-use gain comes from the arena's huge-page advice: on a 500-file
subset the mean of file minima was 229.2 µs for `a9d7693`, 191.7 with the
arena, 217.6 without the advice and 218.5 with a `malloc`-backed arena, while
the four were within noise of each other in steady state. An in-process paired
run of two self-contained C API modules showed no significant change (fresh
21.93 → 22.15, reused 21.22 → 21.11); that harness loads both versions into
one process and is reported for completeness.

### Python package (CPython 3.12, bundled runtime, paired wheels)

| Call | `a9d7693` | Current | Change |
|---|---:|---:|---:|
| `fosu.parse(bytes)` + `len(hit_objects)` (10,000 files × 5) | 25.988 (29.678) | 24.687 (27.834) | −5.0% (−6.2%) |
| `fosu.parse_file(path)` + `len(hit_objects)` (10,000 files × 5) | 31.947 (35.323) | 30.738 (33.638) | −3.8% (−4.8%) |
| 300 different files round-robin, `parse(bytes)`, µs per call | 32.22 | 29.46 | −8.6% |
| First `parse_file` in a fresh interpreter (100 files × 3) | 161.4 (174.9) | 135.1 (148.4) | −16.3% |

Interleaved single-package runs agree (bytes 24.99 → 23.86, file 31.11 →
29.82 µs, medians of three runs). Interpreter start and `import fosu` are
unchanged (about 5.0 ms and 16.4 ms whole-process). Page faults per 3,000
round-robin parses fell from 6,901 to 6,612.

### First parse and process boundaries

| Boundary | `a9d7693` | Current | Change |
|---|---:|---:|---:|
| First C++ parse in a fresh process (`coldstart_x86`, 1,000 files × 3) | 73.98 (80.40) | 71.68 (77.87) | −3.1% |
| One-shot process lifetime (10,000 files × 3, best/file mean; all runs) | 168.56 (186.49) | 167.14 (185.13) | −0.8% |

The header-only interface keeps the caller's allocator, so its first-call
page faults are unchanged; the one-shot executable already had its own arena
and gains only the kernel's share of its 160 µs process time.

### Profile-guided builds (held-out)

`make bench-pgo CXX=g++` trains on sorted indices 0, 5, 10, … and evaluates
the other 8,000 files (five repetitions):

| Build | Fresh | Reused |
|---|---:|---:|
| `a9d7693` | 21.697 (24.133) | 21.007 (22.849) |
| `a9d7693` + PGO | 20.688 (23.136) | 19.997 (21.896) |
| Current | 20.226 (22.306) | 19.773 (21.318) |
| Current + PGO | 19.814 (22.158) | 19.246 (21.035) |

PGO adds less on the current code (about 2-3%) than on `a9d7693` (about 5%):
the restructured loops leave fewer data-dependent branches to specialize. No
PGO build is shipped.

### Branchy versus branchless slider fields (two paired runs, five repetitions)

| Module | Run 1 fresh / reused | Run 2 fresh / reused |
|---|---:|---:|
| `a9d7693` | 23.02 / 21.44 | 22.82 / 21.21 |
| Branchless length and edge scan | 23.72 / 22.20 | 23.41 / 21.97 |
| Branchy edge scan only | 23.38 / 21.88 | 23.42 / 21.96 |
| Branchy length only | 22.20 / 20.58 | 21.77 / 20.38 |
| Both branchy (kept) | 21.54 / 20.01 | 21.52 / 20.07 |

### Verification coverage (this revision)

- Exact canonical output equality with `a9d7693` on all 10,000 files for the
  hosted reference writer, the one-shot executable and the C API reference.
- Python file and bytes entry points match every public field on all 10,000
  files (float bits, raw strings, pool indices, counters); 18 package tests
  pass in AVX2 and scalar mode.
- The official legacy decoder agrees on all 1,395 synthetic fixtures in AVX2
  and scalar mode.
- Native, hardening, C ABI (including arena growth and recycling), export and
  I/O-failure tests pass; Clang ASan/UBSan/float-cast-overflow tests pass;
  a 61-second, 934,271-run fuzz smoke test passes; the sanitizer-built
  scalar/offset/numeric-oracle comparison verifies all 10,000 files
  (8,070,193 objects, six malformed lines).

## Compatibility measurements (0.2.0, `a9d7693`)

September 7, 2026; previous release `33aa2c0` versus `a9d7693`,
GCC 13.3, no PGO. Each paired benchmark rotates candidates within every
file/repetition on CPU 3. All 10,000 corpus files are included. Times are
microseconds; parentheses contain means of all runs.

| Workload | Previous release | Current |
|---|---:|---:|
| C++ fresh result, five repetitions | 19.634 (22.011) | 22.519 (25.639) |
| C++ reused result, five repetitions | 18.704 (20.354) | 21.112 (23.441) |

Official numeric and sample-field validation adds about 2.9 µs (15%) to fresh
parsing and 2.4 µs (13%) to reused parsing in this comparison. Common sample
spellings use parallel integer digit checks; unusual fields use the complete
bounded conversion. Both paths enforce the same acceptance policy. These
results compare different parsing behavior, not identical-output implementations.

This table covers C++ calls at `a9d7693`. Earlier process, C API and Python
results below predate this validation policy and do not establish its
performance at those boundaries; the section above measures every boundary
for the current revision.

### Python measurement procedure

To reproduce the paired Python comparisons, build each revision into its own
wheel directory with `FOSU_BUNDLE_RUNTIME=1`, extract into separate package
directories, then run with CFFI installed in the driver environment:

```sh
taskset -c 3 python bench/python_versions.py /path/to/corpus \
  /path/to/previous-wheel /path/to/current-wheel --reps 5
taskset -c 3 python bench/python_first_compare.py /path/to/corpus \
  /path/to/previous-wheel /path/to/current-wheel --limit 1000 --reps 3
```

### Verification coverage

- Exact canonical output matches between the hosted and one-shot writers on
  all 10,000 files. The reference SHA-256 is
  `334598db4c426ce5e99ba49cb9c4e3c038f88cb5424586ecd718207ec482edad`.
- Both Python file and bytes entry points match every public field on all
  10,000 files, including floating-point bits, raw strings, pool indices and
  counters. The tested extensions bundle private C++ runtimes; dependency
  inspection confirms no dynamic `libstdc++` or `libgcc_s` dependency.
- Scalar/SIMD record comparisons and the independent numeric-conversion
  reference pass on that set and on a broader 23,618-file cache: 792,921,673
  bytes and 14,995,892 hitobjects. The cache is not independently labeled as an
  Aspire/unranked evaluation set. Its host-local manifest SHA-256 is
  `e1acf6c79fe9c68cb5fe35ba21aa444fa289cd74d7e85ae564b3e56dc0390181`.
- The actual official legacy decoder at pinned revision `48c4800e` agrees on
  completion, rejected-line counts and retained object counts for all 10,000
  maps (six rejected lines) and the 23,618-file cache (70 rejected lines).
  The 1,395 synthetic fixtures also match in scalar and SIMD configurations.
  This checks acceptance, not complete gameplay-value equivalence. See the
  [official reference harness](compatibility.md#sources-of-truth).
- Native, C ABI, Python, I/O-failure, allocation-failure and one-shot boundary
  checks pass. Clang ASan/UBSan/float-cast-overflow checks and a 61-second,
  473,537-run mutation fuzz smoke test pass. Fuzz duration is a measured test
  budget, not a proof that all malformed inputs are safe.

After widening the previous stream's integer timestamp representation, exactly
32 maps differ from the previous release: 453 slider lengths across 25 maps
are corrected by one ULP, and seven inherited NaN timing points across three
maps are preserved instead of skipped. The latter also removes seven malformed
counts. Four additional maps change under the official acceptance policy:
three out-of-range timing points and three invalid sliders are rejected, and
whitespace-only lines no longer count as malformed timing points or storyboard
lines. Slider removal also changes the associated pools and indices. The
independent numeric reference and
[official decoding rules](compatibility.md#sources-of-truth) support those
corrections; previous-release equality is not the definition of correctness.

## Earlier 0.1.0 measurements

September 2026, complete corpus, three fresh processes per file, rotating
binaries within each repetition. Times are microseconds.

| Process | Mean of file minima | Mean of all runs | p50 / p99 of file minima | Minor faults at minima |
|---|---:|---:|---:|---:|
| One-shot, complete FOSUDMP4 output | **163.05** | 180.76 | 152.43 / 286.59 | 5.4 |
| One-shot without huge-page advice | 192.20 | 210.24 | 171.85 / 429.66 | 28.5 |
| Earlier Fable executable, FOSUDMP3 output | 161.68 | 178.07 | 151.05 / 289.11 | 5.4 |
| Master, dynamic runtime and reference serializer | 1168.84 | 1234.47 | 1123.18 / 1697.00 | 186.8 |
| Empty minimal executable | 108.06 | 122.28 | 109.29 / 138.25 | 3.6 |

The Fable comparison is a process baseline using its older stream format;
FOSUDMP4 additionally preserves explicit point-pool indices and a framing
footer. It is not an identical-output comparison. Exact verification uses the
master reference emitting FOSUDMP4. That executable is 61,680 bytes; these results precede the current numeric
corrections and timestamp representation.

For library parsing, the following paired run uses 8,000 evaluation files,
five repetitions per file, with modules and fresh/reused modes rotated.
PGO training used the other 2,000 files. Times are means of per-file minima;
parentheses contain means of all runs.

| In-process interface | Fresh result, µs | Reused result, µs |
|---|---:|---:|
| Master C++, GCC | 20.442 (22.729) | 19.051 (20.644) |
| Current C++, GCC | 20.401 (22.942) | 18.898 (20.674) |
| Current C++, GCC + PGO | **18.988 (21.499)** | **17.412 (19.276)** |
| Offset records, GCC | 20.444 (23.062) | 18.844 (20.770) |
| Current C++, Clang 18.1 | 21.756 (24.451) | 20.213 (22.233) |
| C API, bundled runtime; includes copy/view publication | 21.663 (24.517) | 19.972 (22.001) |

The unprofiled C++ path is effectively tied with master; offset records save
storage without a consistent latency benefit. The PGO build reduces parse time
versus master by about 7% fresh / 9% reused in this comparison. A separate
two-module PGO comparison measured 18.997 → 17.509 µs fresh and
18.495 → 16.983 µs reused. Module count and
allocator/cache state affect absolute times; compare rows within a run.

First-use measurements on an evenly spaced 1,000-file subset, five fresh
processes per file: master first C++ parse 62.950 µs; current first C++ parse
63.479 µs. This is effectively unchanged, not a cold-library speedup.
The system-runtime C driver's broader initial `dlopen`/file/parse/release interval is
756.445 µs (802.858 µs across all runs), including loading the shared C++
runtime. A long-running application pays library loading once. These numbers
exclude Python startup and must not be compared as the same timed workload.

The Linux C API defaults to a private bundled C++ runtime. In a paired
1,000-file first-use comparison, system-runtime loading took 756.490 µs
(802.183 across all runs), versus **181.849 µs** (197.270 across all runs)
with bundling. Steady-state C API time was unchanged: system/bundled fresh
19.970/20.034 µs and reuse 19.334/19.280 µs on 8,000 evaluation files.
Bundling raises loaded ELF sections from about 65 to 250 KB, hides the C++
copy and still uses the application's `malloc`/`free`. See
[runtime selection](c-api.md) for the operator-new distinction and opt-out.

The earlier Fable baseline was rebuilt from its latest reviewed revision
`b484fea`; its binary hash matched the preserved measurement binary exactly.

The executable has no supported PGO build target: profiles from a hosted
training runtime have different GCC control-flow counters from the
freestanding runtime. They cannot be treated as valid profiles for that binary.
PGO results above are for the hosted library, with matching training/use builds.

## Earlier Python package measurements

The locally built package uses the same unprofiled C API, compiled with GCC
13.3 for AVX2/BMI1/BMI2 and Zen 4 scheduling, with its private runtime bundled.
On CPython 3.12, a paired run over all
10,000 maps with five repetitions per file measured:

| Python call boundary | Mean of all runs, µs | Mean of file minima, µs |
|---|---:|---:|
| Raw CFFI: allocate, parse bytes, get count, free | 21.97 | 20.83 |
| `fosu.parse(bytes)` and `len(beatmap.hit_objects)` | **23.88** | 22.52 |
| Raw CFFI: allocate, read/parse file, get count, free | 27.54 | 26.02 |
| `fosu.parse_file(path)` and `len(beatmap.hit_objects)` | **29.61** | 27.99 |
| `fosu.parse(bytes)` and `beatmap.hit_objects.to_numpy()` | 27.08 | 25.31 |

Each timed call creates and releases a fresh result. Variants rotate within
each file/repetition; the input and file data are resident. Imports and NumPy
dtype construction happen before timing. The owned Python interface costs
about 2 µs over direct CFFI in this run. Accessing every field as Python objects
adds conversion and iteration work; these timings do not include that traversal.
The package adds convenience and ownership to the current parser, with no new
parser-kernel speedup over the C++ measurements above.

A separate run of the distributable manylinux wheel (built with GCC 14.2.1)
measured 24.41 µs from bytes and 30.26 µs from a file, against 22.45/28.08 µs
through raw CFFI in that same run. These are means of all runs at the same
boundary; both builds place the Python ownership overhead near 2 µs. The
10,000-file exact comparison also covers the distributable Linux wheel.

For cold Python use, a separate 100-file subset with three fresh interpreters
per file compared otherwise identical Linux wheels. The private bundled C++
runtime took 5.13 ms to import `fosu`, versus 6.00 ms with the system runtime;
the full interpreter/import/parse/release/exit interval was 16.38 versus
17.36 ms. These are means of all runs, including parent launch and timing-output
collection for the full interval. The first `parse_file` call itself averaged
155 µs bundled / 148 µs system. The bundled wheel was 259 KB versus 77 KB.
The package keeps the private runtime for its lower import and process time.
Already-loaded application dependencies can change that tradeoff. These cold
Python intervals are distinct from the warmed-call table and from the one-shot
native executable's process time.

```sh
FOSU_BUNDLE_RUNTIME=1 python -m pip install -e '.[test]'
taskset -c 5 python bench/python_compare.py /path/to/corpus --reps 5
python bench/python_verify.py build/oneshot_reference /path/to/corpus

# Extract each wheel into its own directory, with cffi installed in the driver.
taskset -c 5 python bench/python_first_compare.py /path/to/corpus \
  /path/to/extracted-bundled-wheel /path/to/extracted-system-wheel --limit 100 --reps 3
```

Build the runtime comparison wheels from clean source copies using the same
compiler, with `FOSU_BUNDLE_RUNTIME=1 python -m build` for the bundled variant
and `FOSU_BUNDLE_RUNTIME=0 python -m build` for the system-runtime variant.
The build always keeps symbol hiding enabled.
The Python verifier checks both bytes and file entry points against the
canonical reference: all fields, raw string bytes, floating-point bits, pool
indices and parser counters match on all 10,000 files. Package tests additionally
cover buffer inputs, lazy views, section selection, Unicode and embedded NULs,
errors, threading, scalar selection and ownership through NumPy view chains.
Installed wheels are tested on CPython 3.10 and 3.14 on Linux and Apple Silicon.

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

## Process benchmark

```sh
make oneshot CXX=g++
sh oneshot/build.sh build/fosu_oneshot_4k -DABLATE_NO_MADVISE
taskset -c 5 build/oneshot_process /path/to/corpus 0 3 \
  build/previous/build/fosu_oneshot build/fosu_oneshot build/fosu_oneshot_4k > build/process.csv
python3 bench/oneshot_summary.py build/process.csv
```

Arguments are `corpus limit reps binaries...`; limit 0 selects every file.
A nonzero limit selects evenly spaced files from sorted membership. All
candidates must accept a single input path and exit successfully. The `_4k`
variant omits the huge-page request; it does not modify host settings. See
[one-shot setup](../oneshot/README.md) for the optional multi-size THP policy.

The hosted reference emits the same complete stream, but its growing
`std::string` serializer is intentionally straightforward. Its output cost is
not a lower bound on native result delivery. Older parse-and-exit numbers
without output and older stream versions do different work and should be
labeled separately. An empty executable measures process-launch overhead, not
an attainable parsing result or a proof of optimality.

## In-process profiling and boundary harnesses

`bench/profile_parse.cpp` parses an evenly spaced corpus subset in one process
for `perf`, with fresh or reused results and an optional section mask, and
reports the best round plus minor faults:

```sh
make build/profile_parse CXX=g++
taskset -c 3 perf stat -e cycles:u,instructions:u,branch-misses:u -- \
  build/profile_parse /path/to/corpus 1000 20 fresh
taskset -c 3 build/profile_parse /path/to/corpus 1000 20 reuse 0x100  # [HitObjects] only
```

`bench/c_api_loop.c` measures fresh-handle C API parses (`fosu_new`, parse
from bytes, view, `fosu_free`) against a library path given on the command
line; run each library in its own process, because two libraries with the same
soname loaded into one process share a single copy:

```sh
make lib build/c_api_loop CXX=g++
taskset -c 3 build/c_api_loop build/libfosu.so /path/to/corpus 1000 20
```

Paired C API modules for `library_compare` must compile `src/c_api.cpp` into
each module (`-DFOSU_BENCH_CAPI bench/library_module.cpp src/c_api.cpp` with
`-Wl,-Bsymbolic`) for the same reason.

## Library benchmark and PGO

```sh
# GCC/Linux. The outer taskset also pins the training run.
taskset -c 5 make bench-pgo CXX=g++ BENCH_ARGS=/path/to/corpus
```

`bench/library_pgo.sh` compiles isolated DSOs, trains on sorted file indices
0, 5, 10, … and compares on the remaining 80%. It reports five repetitions per
file in rotating order. No evaluation file enters the training pass. Compiler
profiles live under `build/`; they describe branch/code frequencies, not cached
beatmap results. Train a real application at its own call sites and workload
before relying on its PGO benefit.

Additional module variants can be compiled from `bench/library_module.cpp`:
`FOSU_BENCH_OFFSET` uses offset records; `FOSU_BENCH_CAPI` links `libfosu` and
includes input copying, padding and view publication. Use hidden symbol
visibility so inline C++ functions from different revisions cannot interpose.
For example:

```sh
g++ -std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 -fno-plt \
  -fno-stack-protector -Iinclude -fPIC -fvisibility=hidden -shared \
  -DFOSU_BENCH_OFFSET bench/library_module.cpp -o build/library_offset.so
taskset -c 5 build/library_compare /path/to/corpus 5 \
  build/library_baseline.so build/library_offset.so > build/library.csv
python3 bench/library_summary.py build/library.csv
```

The older `make bench BENCH_ARGS=/path/to/corpus` preloads the entire corpus
and traverses it over repeated rounds. A 403 MB corpus exceeds this host's L3,
so those throughput numbers describe a different cache working set from the
per-file repeated comparisons above. `make bench` without a corpus uses
synthetic input and is useful for local smoke checks, not deciding production
performance.

```sh
make build/coldstart_x86 CXX=g++
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Iinclude \
  bench/c_api_first.c -ldl -o build/c_api_first
FOSU_LIBRARY=build/libfosu.so taskset -c 5 python3 bench/library_first_compare.py \
  /path/to/corpus build/coldstart_x86 build/c_api_first --limit 1000 --reps 5 \
  > build/first.csv
python3 bench/library_summary.py build/first.csv
```

The two rows deliberately time different interfaces; see the boundaries above.
`FOSU_PERF=1` enables optional hardware counters in `coldstart_x86` when the
host permits them. `FOSU_COLD_MODE` enables explicit decomposition experiments
inside that benchmark only; leave it unset for the default first-parse result.

## Choices tested

Retained from the hitobject-kernel restructure (all measured on the corpus
host with interleaved A/B runs; see the current measurements above):

- Packed-lane prefix validation and a multiply-derived table index replaced
  four range checks, four delimiter byte compares and a serial shift chain.
- Vector constants materialized once per section (opaque broadcasts) instead
  of being rebuilt per line whenever the loop body contains a call.
- Blank/comment/header detection only after the editor shape fails.
- Records written through raw cursors into reserved capacity and published
  once per section, for hitobjects, sliders, points and timing points.
- One 32-byte window converts the first two slider points speculatively and
  a shuffle table converts each further point; repeat counts of one or two
  digits take a direct path and slider fields are written straight into the
  reserved record.
- The C ABI handle allocates its input copy and arrays from one arena with
  huge-page advice, recycled through a single process-wide spare slot.

Rejected after measurement:

- A mask-indexed slider tail that located every field boundary up front so
  fields could convert in parallel: +12% hitobject instructions and +3%
  cycles on the corpus, because most sliders need two windows plus a
  sequential fallback and the field chain was not the limiting latency.
- Branchless slider length and edge-field handling (an unconditional divide
  and an unconditional edge scan): fewer mispredicts and no change when input
  streams from L3, but about 7% and 1-2% slower respectively when the input is
  cache-resident, which is the common single-parse case. The branchy forms
  were kept.
- Lambdas capturing the timing cursor by reference: the cursor moved to
  memory and the section slowed by ~8% until the loop was written out.
- Arena variants without huge-page advice or backed by `malloc`: equal in
  steady state, but they gave up most of the first-use improvement.


- Shared numeric kernels and compile-time record policies retained library
  throughput. Shared hitobject framing improved the paired hosted result by
  about 2%, while one-shot time was unchanged. Slider storage/rollback remains
  specific to each output representation.
- Shared metadata tables were effectively tied with native switch dispatch;
  they also provide one definition of keys, defaults and numeric behavior.
- `-O2` was about 10% slower than `-O3` for the hosted parser. The freestanding
  executable uses `-O2`, where code size and startup layout matter differently.
- A runtime function pointer for decimal fallback enlarged the executable by
  roughly 7 KiB and cost an additional fault. A compile-time fallback argument
  restored direct calls and the smaller binary.
- Timing storage grows only when its hint is exceeded. Overflow metadata is
  mapped lazily; normal corpus files have no orphaned points and fit the inline
  break/colour storage. An earlier empty-program experiment measured about
  3 µs of extra exec cost for 270 KB of `.bss`.
- In the one-shot design, a controlled input `mmap(MAP_POPULATE)` variant from
  the preceding attempt cost roughly 8 µs more than `read` over the full set.
  Parser user cycles were nearly unchanged. This supports the I/O choice;
  it does not establish an explanation for IPC differences between parsers.
- Previous audits found no durable benefit from instruction/table prefetches,
  wider ISA code generation alone, or a two-pass hitobject classification
  scheme. The present code keeps SIMD conversion and per-section shape reuse
  while concentrating process optimization on startup, output and page faults.

These measurements establish improvements under the stated boundary and host
conditions. They do not prove that no faster parser or process design can exist.
