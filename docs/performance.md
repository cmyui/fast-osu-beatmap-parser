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

## Measurement snapshots

Native and one-shot source: [`d31cddf`](https://github.com/cmyui/fast-osu-beatmap-parser/commit/d31cddf90f6c7c5d35f2c5f6a4c0ccc39e716866).
Python source: [`2d010a1`](https://github.com/cmyui/fast-osu-beatmap-parser/commit/2d010a190deda8df71d2607c0b8c8dfdbac8df44).
Measured 2026-09-08 UTC. Both machines use the same full **10,000-map corpus** and
validate all 8,070,193 objects before timing. Builds and tests finish before
measurements start; each host runs only one benchmark at a time.

- **Hetzner / Zen 4:** the Linux VM described above, pinned to CPU 3, GCC 13.3,
  CPython 3.12.3. Native AVX2 uses `-O3 -march=x86-64-v3 -mtune=znver4`;
  scalar uses the baseline ISA. Linux Python bundles its private C++ runtime.
- **M3 Max:** macOS 26.2, Apple Clang 17, CPython 3.14.0, `-O3`, NEON or
  scalar. No CPU affinity control. Different CPUs, operating systems and Python
  versions mean the cross-machine difference is not an ISA-only comparison.

Both retain the [compiled-target hardening policy](build.md#hardening), including
strong stack protection and fortification. No PGO. Numbers below are µs/map
unless marked ms. Ranges are the two reversed-order runs, not confidence
intervals. The [Linux summary](../bench/results/2026-09-08-hetzner.json) and
[Mac summary](../bench/results/2026-09-08-m3-max.json) retain each run's minima,
all-sample means/medians, repetitions, raw CSV hashes and corpus fingerprint.
The [Python conversion evidence](../bench/results/2026-09-08-python-conversion.json)
contains the updated Python measurements, their `b8af09c` controls, screening
experiments and first-use comparisons. Python package variants rotate within
each map, as well as the four workloads; the second run reverses both orders.

### Warm native calls

Seven repetitions per map, rotating C++/C and fresh/reused parsers within each
map; a second run reverses module order. Input copies and result release are
included. Neither path caches parsed beatmaps.

| Machine / backend / API | Fresh: mean minimum | Reused: mean minimum | Fresh: all-call mean | Reused: all-call mean |
|---|---:|---:|---:|---:|
| Hetzner / Zen 4 / AVX2 / C++ | 21.19–21.20 | 19.92–20.06 | 23.79–23.87 | 22.16–22.18 |
| Hetzner / Zen 4 / AVX2 / C | 24.03–24.08 | 22.56–22.85 | 27.27–27.33 | 25.54–25.60 |
| Hetzner / Zen 4 / SCALAR / C++ | 59.19–59.56 | 57.66–57.79 | 64.92–65.03 | 63.49–63.74 |
| Hetzner / Zen 4 / SCALAR / C | 61.57–61.62 | 59.90–59.94 | 67.17–67.39 | 65.16–65.90 |
| M3 Max / NEON / C++ | 17.75–17.90 | 17.52–17.70 | 20.23–20.67 | 19.23–19.66 |
| M3 Max / NEON / C | 19.56–19.64 | 19.21–19.36 | 22.31–22.92 | 21.17–21.65 |
| M3 Max / SCALAR / C++ | 55.84–56.38 | 55.50–56.16 | 59.74–60.85 | 59.01–60.07 |
| M3 Max / SCALAR / C | 57.65–58.07 | 57.30–57.79 | 61.59–62.90 | 60.66–61.99 |

### Warm eager Python calls

Three repetitions per map; bytes, file, timestamp traversal and slider-length
traversal rotate within each map. The second run reverses workload order.
Every workload constructs and releases the complete Python result. Normal GC
remains enabled. Traversal timings include parsing, not just the extra loop.

| Machine / backend | Workload | Mean minimum | All-call mean |
|---|---|---:|---:|
| Hetzner / Zen 4 / AVX2 | Resident bytes | 385.1–400.0 | 427.1–440.4 |
| Hetzner / Zen 4 / AVX2 | Warm file | 398.5–403.0 | 442.5–446.3 |
| Hetzner / Zen 4 / AVX2 | Bytes + timestamps | 414.3–416.1 | 457.7–458.9 |
| Hetzner / Zen 4 / AVX2 | Bytes + slider lengths | 420.4–432.2 | 468.5–480.9 |
| M3 Max / NEON | Resident bytes | 236.6–238.3 | 270.0–270.5 |
| M3 Max / NEON | Warm file | 253.0–256.4 | 290.1–296.0 |
| M3 Max / NEON | Bytes + timestamps | 251.0–255.9 | 281.8–292.4 |
| M3 Max / NEON | Bytes + slider lengths | 254.0–263.4 | 285.3–300.0 |

These are full-corpus, per-call statistics. The README's Python comparison
instead uses isolated complete-pass means on the fixed **9,758-map** common
cohort. See [the comparison](comparison.md) for those batch measurements; do not
use these minima against competitors' all-call means. Python graph construction
dominates the eager API, so the native SIMD speedup does not translate directly
into an equally large Python speedup.

### First use

Native: 1,000 evenly spaced maps, three fresh child processes per map per
executable, two reversed-order runs. The table shows means of per-map minima.
The C++ timer covers first parse/result allocation, excluding file read and
destruction. The C timer includes `dlopen`, file read, parse/view/free and
`dlclose`. Neither includes process startup. **These columns time different
work, not just C-versus-C++ binding overhead.**

| Machine / backend | C++ first parse (µs) | C first library use (µs) |
|---|---:|---:|
| Hetzner / Zen 4 / AVX2 | 85.34–86.48 | 339.10–341.27 |
| Hetzner / Zen 4 / SCALAR | 124.20–125.49 | 279.22–281.21 |
| M3 Max / NEON | 52.23–52.76 | 623.72–627.22 |
| M3 Max / SCALAR | 92.43–92.56 | 402.10–404.38 |

Python: 100 evenly spaced maps, three fresh interpreters per map, two runs
with reversed package order. Python bytecode is precompiled. File pages are
resident. Import, first file parse/result release, and the whole subprocess are
timed separately. Each column is a mean of its own per-map minima, so columns
are not additive. The whole-process column includes startup and shutdown.

| Machine / backend | Import (ms) | First file (µs) | Whole Python process (ms) |
|---|---:|---:|---:|
| Hetzner / Zen 4 / AVX2 | 20.91–20.92 | 724.45–724.79 | 34.46–34.53 |
| M3 Max / NEON | 12.06–12.09 | 381.42–383.13 | 31.94–32.00 |

### Linux one-shot process

Full 10k corpus, six samples per map, GCC `-O2 -march=znver4`:
**279.38 µs** mean per-map minimum;
**306.46 µs** mean of all samples. This includes spawn,
file read, parse, complete-result serialization to `/dev/null`, exit and reap.
The same executable occupies both rotating driver slots; this is a current
measurement, not a baseline-versus-candidate speedup claim.

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
