# Measuring performance

Use the small representative all-mode corpus for routine experiments. Keep
pathological cases in correctness tests and the full compatibility corpus.
See [corpora](../bench/corpus/README.md) and [iteration guidance](../AGENTS.md).

## Public benchmark profiles

Named profiles keep option comparisons readable and reproducible. They are not
additional parser APIs; each row is an ordinary `ParseOptions` or Python keyword
configuration.

| Profile | Exact requested work |
|---|---|
| Decode | Defaults: all sections, no derived gameplay values, no mods |
| Hit objects only | `sections=HIT_OBJECTS` |
| End times | `calculate_slider_end_times=true` |
| Paths | `calculate_slider_paths=true` |
| Geometry | End times and paths enabled |
| Events | `calculate_slider_events=true`; this implies end times and paths |
| Stacking | `apply_stacking=true`; this implies end times and paths |
| Gameplay | Events and stacking enabled |
| Double time | `mods=DOUBLE_TIME` |

The geometry and gameplay names describe benchmark outcomes. They do not hide
new behavior or change the parser's explicit option interface.

## Current feature costs

Measured on 2026-09-11 from parser revision `feb0606`. The performance profile
contains 1,024 entries (986 unique beatmaps), 256 per mode and 46,029,610 bytes.
Repeated entries are deliberate products of the stratified selection. Input is
resident in memory. Native rows create a fresh `Parser`; Python rows include the
complete detached Python result and its release.

Each cell is the arithmetic mean of the fastest sample for every corpus entry.
Native uses five samples and Python three, with profile order rotated per entry.
This is a lower-envelope feature-cost comparison, not a latency percentile.
Microseconds per map; lower is better.

### AMD EPYC Genoa, Linux x86-64

GCC 13.3, `-O3`; AVX2 uses `x86-64-v3` and `znver4` tuning. Python is CPython
3.12.3. Runs were pinned to one vCPU on the eight-vCPU shared-tenancy host.

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 23.8 | 74.5 | 416.8 | 460.7 |
| Hit objects only | 20.5 | 67.1 | 391.3 | 433.3 |
| End times | 26.1 | 77.1 | 418.3 | 463.2 |
| Paths | 46.5 | 100.2 | 605.1 | 649.3 |
| Geometry | 46.9 | 100.9 | 602.0 | 648.9 |
| Events | 52.2 | 109.1 | 850.5 | 898.0 |
| Stacking | 42.7 | 97.0 | 582.3 | 630.1 |
| Gameplay | 54.8 | 112.6 | 896.8 | 948.0 |
| Double time | 25.7 | 77.0 | 420.1 | 465.6 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 21.5 | 65.7 | 303.5 | 351.8 |
| Hit objects only | 19.2 | 58.7 | 287.2 | 329.2 |
| End times | 22.6 | 67.3 | 304.7 | 352.6 |
| Paths | 34.0 | 78.6 | 430.9 | 483.1 |
| Geometry | 34.5 | 79.0 | 428.5 | 480.6 |
| Events | 36.8 | 81.4 | 608.6 | 662.5 |
| Stacking | 32.2 | 76.7 | 418.2 | 465.7 |
| Gameplay | 38.7 | 82.6 | 642.2 | 701.4 |
| Double time | 22.1 | 66.1 | 305.3 | 353.0 |

Small negative option costs in Python are measurement noise around eager result
construction. Paths and especially events dominate the optional work.

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods.

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 24.5 | 460.5 | 20.8 | 327.4 |
| DT | 25.0 | 451.3 | 21.4 | 328.3 |
| HR+DT | 25.4 | 452.7 | 21.7 | 326.0 |
| Full HR+DT | 113.7 | 1,676.5 | 71.7 | 1,161.3 |

## Reproduce FOSU measurements

Build the native matrix for the desired ISA, then use the same corpus and
manifest for every run:

```sh
cmake -S . -B build/native -DFOSU_ISA=avx2
cmake --build build/native --target feature_matrix -j4
build/native/feature_matrix /path/to/maps 5 avx2 all-modes > build/native.csv
python bench/summarize.py build/native.csv --corpus-manifest /path/to/manifest.csv

FOSU_BACKEND=avx2 python bench/python_feature_matrix.py /path/to/maps \
  --reps 3 --variant python-avx2 --suite all-modes > build/python.csv
python bench/summarize.py build/python.csv --corpus-manifest /path/to/manifest.csv
```

For standard-only HR profiles, pass a standard-only directory and replace
`all-modes` with `standard` or `--suite all-modes` with `--suite standard`.

For revision-to-revision work, `library_compare` measures fresh and reused
native parsers and `python_compare.py` measures the eager Python boundary:

```sh
build/native/library_compare /path/to/maps 3 \
  /path/to/baseline/library_native.so /path/to/candidate/library_native.so \
  > build/native-compare.csv
python bench/summarize.py build/native-compare.csv

python bench/python_compare.py /path/to/maps \
  /path/to/baseline-package /path/to/candidate-package --reps 3 \
  > build/python-compare.csv
```

Build before timing. Serialize runs per host, avoid competing work, and pin a
CPU with `taskset` on Linux. Screen cheaply; repeat promising small differences
with reversed variant order and a confirmation sample. Do not compare different
corpora, API boundaries, outputs, build settings or summary statistics.

`profile_parse` preloads a subset and repeats rounds for profiling:

```sh
perf stat -e cycles,instructions,branches,branch-misses -- \
  build/native/profile_parse /path/to/maps 1000 20 fresh
```

Generated artifacts belong under ignored `build/` directories. The
[competitor comparison](comparison.md) uses different timing protocols intended
to compare public APIs; its numbers must not be mixed with the feature tables.
