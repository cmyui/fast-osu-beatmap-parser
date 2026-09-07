# Measuring performance

Measure the boundary the application uses. A parser call with resident bytes,
a first Python call, and a complete process invocation include different work.
Only compare variants at the same boundary and with the same result fields.
Acceptance/rejection follows the [official legacy decoder](compatibility.md);
performance comparisons must also pass full-result regression checks.

## Workloads

| Driver | Timed work | Outside timing |
|---|---|---|
| `library_compare` / native module | C++ parse plus fresh result destruction, or reused result | File read/padding, module loading |
| `library_compare` / C API module | Input copy, parse, view acquisition, fresh handle/free or reused handle | Original file read, module loading |
| `python_compare.py` | `parse(bytes)` or `parse_file(path)`, result access and release | Imports; input bytes already available; file data resident |
| `library_first_compare.py` / `library_first` | First C++ parse and result allocation in a fresh process | Process startup, file read and result destruction |
| `library_first_compare.py` / `c_api_first` | `dlopen`, read, first parse, view, free and `dlclose` | Process startup |
| `python_first_compare.py` | Separately: import, first `parse_file`/release, whole Python process | Input page-cache misses |
| `oneshot_process` | Spawn, read original map, parse, write complete result, exit and reap | Input page-cache misses; reference verification |

The Python warm driver can also isolate raw CFFI bytes/file calls and NumPy view
creation. Normal Python results are lazy: parsing does not decode every string
or create a Python object for every note. Iterating all records or copying NumPy
arrays is additional work. The first-use driver launches a new interpreter for
every sample; the warm driver repeatedly calls an already loaded library.

A fresh result can still benefit from allocator state or the C API's bounded
spare arena within a process. A fresh process cannot. Neither path caches parsed
maps. One-shot stdout goes to `/dev/null`, so it measures serialization and the
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

## Current measurement snapshot

### Linux / Zen 4

On the target above, 2026-09-07, with resident input and one pinned core:

| Boundary | Mean per-map minimum |
|---|---:|
| C++ fresh result, resident bytes | 19.64–20.35 µs |
| C++ reused result | 18.58–19.25 µs |
| C ABI fresh handle, input copy included | 20.95–21.77 µs |
| C ABI reused handle | 19.94–20.61 µs |
| Complete one-shot process | 163.12 µs |

Library ranges are two full-corpus comparisons with reversed starting order,
seven repetitions per map for both C++ and the C ABI. The one-shot measurement
uses three repetitions per map. The AVX2 kernels use GCC
`-O3 -march=x86-64-v3 -mtune=znver4`, with baseline code selecting the C ABI
backend once per loaded library. Libraries and their benchmarks use the
[compiled-target hardening policy](build.md#hardening), including strong stack
protection and fortification. The separate one-shot measurement uses its
GCC `-O2 -march=znver4` build. No PGO is applied. Differences between runs are
one reason to retain all-run statistics and compare variants together.

Python 3.12.3, builds with the private C++ runtime, full 10k corpus
and seven repetitions per map in both starting orders:

| Python boundary | Mean per-map minimum |
|---|---:|
| Warm `parse(bytes)` and result release | 23.15–24.70 µs |
| Warm `parse_file(path)` and result release | 28.95–30.75 µs |

Python first-use measurements select 100 evenly spaced maps and launch a fresh
CPython process three times per map/variant, with identical dependencies and
precompiled bytecode. The first `parse_file`/result-release interval averages
127.07–132.18 µs of per-file minima across both starting orders; import
averages 4.74–4.83 ms and the complete Python process 15.36–15.63 ms. These are
separate timed boundaries, with separate minima; they should not be added together.

First C ABI use in a fresh C process averages 191.21–197.90 µs across 1,000
evenly spaced maps, three repetitions each in both starting orders. This includes
`dlopen`, CPU selection, file I/O, parsing, view acquisition, destruction and
`dlclose`, excluding the
C process's startup. CPU detection is paid once per library load, not once per map.

### Apple Silicon / NEON

Apple M3 Max, macOS 26, Apple Clang 17, `-O3` with strong stack protection
and fortification level 2, 2026-09-07. This measures
**The Unforgiving only**: 243,197 bytes and 6,503 hitobjects, all taking the
NEON prefix path. It is not a 10k-corpus average. File contents are resident.
Two rotating comparisons reverse the starting order, with 1,001 repetitions
per variant in each run:

| Boundary | Minimum | Median of all repetitions |
|---|---:|---:|
| C++ fresh result, resident bytes | 140–144 µs | 150–151 µs |
| C ABI fresh handle, input copy included | 143–148 µs | 157–158 µs |
| Python warm `parse(bytes)` and release | 139–141 µs | 155–157 µs |
| Python warm `parse_file(path)` and release | 150–154 µs | 168–169 µs |

Python uses CPython 3.14.6 and CFFI 2.1.1. Two separate 51-repetition rotating
comparisons with fresh interpreters and precompiled bytecode measured the first
`parse_file` and result release at a median 332–341 µs; import took 1.63–1.67 ms
and the whole process 23.35–24.79 ms. These intervals have separate statistics
and should not be added together. They do not include cold storage reads or establish
worst-case latency.

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

# Optional breakdown of the installed package's Python/CFFI/NumPy boundary.
python bench/python_compare.py "$corpus" --workloads bytes file raw-bytes raw-file numpy \
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
