# Measuring performance

Measure the boundary the application uses. Native parsing and eager Python
conversion include different work.
Only compare variants at the same boundary and with the same result fields.
Acceptance/rejection follows the [official legacy decoder](compatibility.md);
performance comparisons must also pass full-result regression checks.

## Workloads

| Driver | Timed work | Outside timing |
|---|---|---|
| `library_compare` / native module | C++ input copy and parse with a fresh or reused `Parser` | Original file read, module loading |
| `python_compare.py` | `parse(bytes)` or `parse_file(path)`, result access and release | Imports; input bytes already available; file data resident |

Python parsing constructs every supported field as a detached Python value before
returning. The warm driver can additionally sum timestamps or slider lengths;
these accesses do not perform deferred parsing or create native-backed wrappers.
A reused parser retains committed arena pages directly. Fresh parsers can reuse
the bounded arena pool. Neither path caches parsed maps.

## Method and target

The evaluation machine is an eight-core, shared-tenancy AMD EPYC Genoa (Zen 4)
VM with no SMT, 32 MiB L3, Ubuntu 24.04, kernel 6.8, GCC 13.3 and glibc 2.39.
The snapshots below used 10,000 ranked/approved maps selected by playcount:
402,593,897 input bytes and 8,070,193 parsed objects. Routine measurements use
a smaller all-mode sample; full validation uses the retained compatibility
corpus. See [corpora](../bench/corpus/README.md). Results from different corpora
are not directly comparable.

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

Native source: [`d31cddf`](https://github.com/cmyui/fast-osu-beatmap-parser/commit/d31cddf90f6c7c5d35f2c5f6a4c0ccc39e716866).
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
intervals. These are historical measurements, not a claim about the current revision.

### Warm native calls

Seven repetitions per map, rotating C++ and fresh/reused parsers within each
map; a second run reverses module order. Input copies and result release are
included. Neither path caches parsed beatmaps.

| Machine / backend / API | Fresh: mean minimum | Reused: mean minimum | Fresh: all-call mean | Reused: all-call mean |
|---|---:|---:|---:|---:|
| Hetzner / Zen 4 / AVX2 / C++ | 21.19–21.20 | 19.92–20.06 | 23.79–23.87 | 22.16–22.18 |
| Hetzner / Zen 4 / SCALAR / C++ | 59.19–59.56 | 57.66–57.79 | 64.92–65.03 | 63.49–63.74 |
| M3 Max / NEON / C++ | 17.75–17.90 | 17.52–17.70 | 20.23–20.67 | 19.23–19.66 |
| M3 Max / SCALAR / C++ | 55.84–56.38 | 55.50–56.16 | 59.74–60.85 | 59.01–60.07 |


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

## Build and verify

These commands use GCC/Linux x86-64. See [build configurations](build.md) for
other architectures and sanitizer/portable builds.

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build/native --target bench-build references -j4
b=build/native
corpus=/path/to/maps
cmake --build "$b" --target check -j4
"$b/validate_corpus" "$corpus" "$b/numeric_oracle.so"
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
"$b/library_compare" build/smoke-corpus 2 "$b/library_native.so" "$b/library_native.so" > "$b/library.csv"
python3 bench/summarize.py "$b/library.csv"
```

## Library and Python comparisons

```sh
# Fresh and reused C++ results, rotated within each map.
# run.py saves raw CSV plus command/build/corpus metadata in .csv.json.
taskset -c 3 python3 bench/run.py --output build/library.csv --corpus "$corpus" \
  --config "$b/compile_commands.json" --config "$b/CMakeCache.txt" -- \
  "$b/library_compare" "$corpus" 9 "$b/library_native.so" /path/to/candidate/library_native.so
python3 bench/summarize.py build/library.csv

# Build/extract each wheel into its own directory, with identical dependencies.
python -m compileall -q /path/to/baseline/fosu /path/to/candidate/fosu
taskset -c 3 python bench/python_compare.py "$corpus" \
  /path/to/baseline /path/to/candidate --reps 7 > build/python.csv
python3 bench/summarize.py build/python.csv

# Complete eager results, with optional timestamp and slider-length traversal.
python bench/python_compare.py "$corpus" --workloads bytes file iterate slider-lengths \
  --reps 7 > build/python-boundary.csv
```

Use the same interpreter, dependencies, bytecode-cache state and wheel runtime
policy for both packages. Release wheels bundle the private Linux C++ runtime;
ordinary source installs use the system runtime unless requested otherwise.
`bench/run.py` can wrap any driver, and refuses to overwrite existing evidence.
Its metadata includes local commands/paths; inspect it before publishing.

## Profiling

```sh
perf stat -r 3 -e cycles,instructions,branches,branch-misses,page-faults -- \
  taskset -c 3 "$b/profile_parse" "$corpus" 1000 20 fresh
# Optional section mask: 0x100 selects hitobjects.
taskset -c 3 "$b/profile_parse" "$corpus" 1000 20 reuse 0x100
```

The driver preloads a subset and repeats whole rounds for sampling.
See [the competitor harness](../bench/comparison/README.md) to measure other parsers.
Store generated results under `build/`, not in the source tree.
