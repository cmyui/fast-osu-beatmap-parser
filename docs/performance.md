# Measuring performance

Measure the boundary the application uses. A parser call with resident bytes,
a first Python call, and a complete process invocation include different work.
Only compare variants at the same boundary and with the same result fields.
Acceptance/rejection follows the [official legacy decoder](compatibility.md);
performance comparisons must also pass full-result regression checks.

## Workloads

| Driver | Timed work | Outside timing |
|---|---|---|
| `library_compare` / native module | C++ input copy and parse with a fresh or reused `Parser` | Original file read, module loading |
| `library_compare` / C API module | Input copy, parse, view acquisition, fresh handle/free or reused handle | Original file read, module loading |
| `python_compare.py` | `parse(bytes)` or `parse_file(path)`, result access and release | Imports; input bytes already available; file data resident |
| `library_first_compare.py` / `library_first` | First C++ parse and result allocation in a fresh process | Process startup, file read and result destruction |
| `library_first_compare.py` / `c_api_first` | `dlopen`, read, first parse, view, free and `dlclose` | Process startup |
| `python_first_compare.py` | Separately: import, first `parse_file`/release, whole Python process | Input page-cache misses |
| `oneshot_process` | Spawn, read original map, parse, write complete result, exit and reap | Input page-cache misses; reference verification |

Python parsing constructs every supported field as a detached Python value before
returning. The warm driver can additionally sum timestamps or slider lengths;
these accesses do not perform deferred parsing or create native-backed wrappers.
The first-use driver launches a new interpreter for every sample; the warm
driver repeatedly calls an already loaded library.

A reused parser retains committed arena pages directly. Fresh parsers can reuse
the bounded arena pool. Neither path caches parsed maps. One-shot stdout
goes to `/dev/null`, so it measures serialization and the
write syscall but not another program decoding or consuming the stream.

## Method and target

The evaluation machine is an eight-core, shared-tenancy AMD EPYC Genoa (Zen 4)
VM with no SMT, 32 MiB L3, Ubuntu 24.04, kernel 6.8, GCC 13.3 and glibc 2.39.
The primary corpus contains the 10,000 most-played ranked/approved maps by
Akatsuki playcount: 402,593,897 input bytes and 8,070,193 parsed objects. Its
manifest fixes the selection and source keys; raw files remain on the corpus
host. Corpus conclusions do not establish performance on all maps or machines.

Pin a comparison to one core and serialize it with other benchmarks on the
host. Drivers rotate variants within each file and repetition. Reverse the
variant order in a second run when interpreting small differences. Avoid
compiling, running fuzzers or competing benchmarks while timing.

`bench/summarize.py` reports the **mean of each file's minimum time**, the median
of those minima, and all-run statistics. The mean minimum estimates an
uncontended lower envelope; it is not the median map, average user latency, or
a confidence interval. Shared-VM scheduling can move results. MB/s is total
input bytes divided by the sum of per-file minima, using decimal megabytes.

## Native measurement snapshot

Measured source: [`feaf08f`](https://github.com/cmyui/fast-osu-beatmap-parser/commit/feaf08f1f6f3ee3028322d80718e830e1577a308).
All runs below were serialized after builds and correctness checks completed.

### Linux / Zen 4

On the target above, 2026-09-08 UTC, with resident input and one pinned core:

| Boundary | Mean per-map minimum |
|---|---:|
| C++ fresh result, resident bytes | 20.59–20.75 µs |
| C++ reused result | 20.35–20.47 µs |
| C ABI fresh handle, input copy included | 22.91–23.21 µs |
| C ABI reused handle | 22.64–22.85 µs |
| Complete one-shot process | 279.51 µs |

Library ranges are two full-corpus comparisons with reversed starting order,
seven repetitions per map for both C++ and the C ABI. The one-shot measurement
uses six samples per map (the same executable in both rotating driver slots).
The AVX2 kernels use GCC
`-O3 -march=x86-64-v3 -mtune=znver4`, with baseline code selecting the C ABI
backend once per loaded library. Libraries and their benchmarks use the
[compiled-target hardening policy](build.md#hardening), including strong stack
protection and fortification. The separate one-shot measurement uses its
GCC `-O2 -march=znver4` build. No PGO is applied. Differences between runs are
one reason to retain all-run statistics and compare variants together.

First C ABI use in a fresh C process averages 334.28–337.96 µs across 1,000
evenly spaced maps, three repetitions each in two runs. This includes
`dlopen`, CPU selection, file I/O, parsing, view acquisition, destruction and
`dlclose`, excluding the
C process's startup. CPU detection is paid once per library load, not once per map.

### Apple Silicon / NEON

Apple M3 Max, macOS 26, Apple Clang 17, `-O3` with strong stack protection
and fortification level 2, 2026-09-08 UTC. This measures
**The Unforgiving only**: 243,197 bytes and 6,503 hitobjects, all taking the
NEON prefix path. It is not a 10k-corpus average. File contents are resident.
Two rotating comparisons reverse the starting order, with 1,001 repetitions
per variant in each run:

| Boundary | Minimum | Median of all repetitions |
|---|---:|---:|
| C++ fresh result, resident bytes | 132.5–133.0 µs | 139.8–142.0 µs |
| C ABI fresh handle, input copy included | 143.1–144.5 µs | 149.8–152.6 µs |

## Eager Python measurements

On the same Zen 4 host, CPython 3.12.3 with the bundled C++ runtime,
the eager binding on `aedfc51` was measured with the
[comparison batch driver](../bench/comparison/README.md). This uses the fixed
9,758-map common cohort and **means of two complete passes**, not the per-map
minimum statistic in the native tables:

| Boundary | AVX2 mean | Scalar mean |
|---|---:|---:|
| Resident bytes, complete Python result and release | 635.2 µs/map | 687.9 µs/map |
| Warm file, complete Python result and release | 647.7 µs/map | 705.7 µs/map |

Normal GC is enabled. Imports, input preload and warmup are outside the timed
loop. All supported Python values are constructed before returning. These
numbers do not measure native parsing alone; Python graph construction dominates.
The [batch evidence](../bench/comparison/results/hetzner-2026-09-08.eager-python-batch.json)
retains both pass times and the corpus fingerprint. First-use measurements must
also include the dataclass/model import costs; do not substitute native-view
Python timings for this API.

On Apple M3 Max / macOS 26 / CPython 3.14.0, the same eager implementation
was also measured over all 10,000 maps, with three repetitions per map and
bytes/file calls rotated within each repetition:

| NEON Python boundary | Mean per-map minimum | Mean of all calls |
|---|---:|---:|
| Resident bytes and complete result release | 326.4 µs | 353.0 µs |
| Warm file and complete result release | 344.8 µs | 375.1 µs |

These are per-map timings, not the Linux batch statistic, and cover the full
corpus rather than the single-map native Apple Silicon example above.

## Build and verify

These commands use GCC/Linux x86-64. See [build configurations](build.md) for
other architectures and sanitizer/portable builds.

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build/native --target bench-build references oneshot -j4
b=build/native
corpus=/path/to/maps
cmake --build "$b" --target check check-oneshot -j4
"$b/validate_corpus" "$corpus" "$b/numeric_oracle.so"
python3 tests/verify_stream.py "$b/reference_native" "$b/reference_c_api" "$corpus"
python3 tests/verify_stream.py "$b/reference_native" "$b/fosu_oneshot" "$corpus"
python3 tests/verify_python.py "$b/reference_native" "$corpus"
```

The canonical serializers compare every represented field, original string
bytes, double bits, all pool entries/indices and counters. Scalar/SIMD comparisons
ignore only the fast/slow path counters. The numeric reference replaces decimal
conversion with bounded libc conversion; it does not define acceptance policy.
The independent official acceptance fixtures have their own
[reference harness](compatibility.md#sources-of-truth).

For cross-revision comparisons, export or check out **whole trees** into separate
directories and compile each with its own includes. Benchmark modules use hidden
visibility to prevent C++ inline definitions interposing across loaded DSOs.
Each module must preserve the same timed contract and output semantics. Never
compile an old source file against current headers and label it an old parser.

For a public smoke corpus, generate synthetic files; these exercise the tools
but are not representative performance evidence:

```sh
"$b/generate_corpus" build/smoke-corpus  # requires a new output directory
"$b/library_compare" build/smoke-corpus 2 "$b/library_native.so" "$b/library_c_api.so" > "$b/library.csv"
python3 bench/summarize.py "$b/library.csv"
```

## Library and Python comparisons

```sh
# Fresh and reused C++/C ABI results, rotated within each map.
# run.py saves raw CSV plus command/build/corpus metadata in .csv.json.
taskset -c 3 python3 bench/run.py --output build/library.csv --corpus "$corpus" \
  --config "$b/compile_commands.json" --config "$b/CMakeCache.txt" -- \
  "$b/library_compare" "$corpus" 9 "$b/library_native.so" "$b/library_c_api.so"
python3 bench/summarize.py build/library.csv

# Build/extract each wheel into its own directory, with identical dependencies.
python -m compileall -q /path/to/baseline/fosu /path/to/candidate/fosu
taskset -c 3 python bench/python_compare.py "$corpus" \
  /path/to/baseline /path/to/candidate --reps 7 > build/python.csv
python3 bench/summarize.py build/python.csv
taskset -c 3 python bench/python_first_compare.py "$corpus" \
  /path/to/baseline /path/to/candidate --limit 500 --reps 3 > build/python-first.csv
python3 bench/summarize.py build/python-first.csv

# Complete eager results, with optional timestamp and slider-length traversal.
python bench/python_compare.py "$corpus" --workloads bytes file iterate slider-lengths \
  --reps 7 > build/python-boundary.csv
```

Use the same interpreter, dependencies, bytecode-cache state and wheel runtime
policy for both packages. Release wheels bundle the private Linux C++ runtime;
ordinary source installs use the system runtime unless requested otherwise.
`bench/run.py` can wrap any driver, and refuses to overwrite existing evidence.
Its metadata includes local commands/paths; inspect it before publishing.

## First use, one-shot and profiling

```sh
taskset -c 3 python3 bench/library_first_compare.py "$corpus" \
  "$b/library_first" "$b/c_api_first" --limit 500 --reps 3 > build/first.csv
python3 bench/summarize.py build/first.csv

cmake --build "$b" --target oneshot_process
taskset -c 3 "$b/oneshot_process" "$corpus" 0 3 \
  /path/to/baseline/fosu_oneshot "$b/fosu_oneshot" > build/process.csv
python3 bench/summarize.py build/process.csv

perf stat -r 3 -e cycles,instructions,branches,branch-misses,page-faults -- \
  taskset -c 3 "$b/profile_parse" "$corpus" 1000 20 fresh
# Section mask is optional; 0x100 selects hitobjects.
taskset -c 3 "$b/profile_parse" "$corpus" 1000 20 reuse 0x100
"$b/prefix"  # isolated hitobject-prefix microbenchmark, not a full parser
```

The process driver arguments are corpus, file limit (`0` means all), repetitions,
then executables. Process failures invalidate the run. It records user/system
time and page faults alongside wall time. The profiling driver preloads an
evenly spaced subset and repeats whole rounds for `perf` sampling.

Anonymous page provisioning affects first-use and process results. The tuned
host enables 64/128/256 KiB folios for `MADV_HUGEPAGE`; see the
[one-shot memory policy](../oneshot/README.md#optional-host-configuration).
The parser never changes host policy. To measure without its advice, compile the
C ABI with `-DFOSU_ARENA_NO_HUGEPAGE` or the executable with
`ONESHOT_FLAGS=-DABLATE_NO_MADVISE`, into a separate `BUILD_DIR`.

GCC profile-guided optimization is an explicit experiment:

```sh
CXX=g++ taskset -c 3 sh bench/library_pgo.sh "$corpus"
```

It trains on sorted file indices 0,5,10,... and evaluates on the other 80%.
Keep the split when quoting held-out results. Profiles encode code-generation
feedback, not cached parsed maps. PGO for a header-only application must be
trained with that application's workload and build settings.
