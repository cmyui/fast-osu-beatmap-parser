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

Measured on 2026-09-18 from parser revision `2d68f2a`. The performance profile
contains 1,024 entries (986 unique beatmaps), 256 per mode and 46,029,610 bytes.
Repeated entries are deliberate products of the stratified selection. Input is
resident in memory. Native rows create a fresh `Parser`; Python rows include the
complete detached Python result and its release.

The corpus SHA-256 is
`1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

Each cell is the arithmetic mean of the fastest sample for every corpus entry.
Native uses five samples and Python three, with profile order rotated per entry.
This is a lower-envelope feature-cost comparison, not a latency percentile.
Microseconds per map; lower is better.

### AMD EPYC Genoa, Linux x86-64

GCC 13.3, `-O3`; native AVX2 uses `x86-64-v3`, while the Python AVX2 engine
uses AVX2, BMI and BMI2 without host-specific tuning. Python is CPython 3.12.3.
Runs were pinned to one vCPU on the eight-vCPU shared-tenancy host.

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 19.8 | 68.2 | 416.5 | 474.0 |
| Hit objects only | 16.4 | 60.4 | 394.0 | 447.6 |
| End times | 22.0 | 70.3 | 419.2 | 476.6 |
| Paths | 41.7 | 92.6 | 592.7 | 651.8 |
| Geometry | 42.5 | 93.4 | 591.7 | 648.7 |
| Events | 49.4 | 101.5 | 843.5 | 905.8 |
| Stacking | 38.7 | 89.8 | 571.2 | 627.3 |
| Gameplay | 52.6 | 105.3 | 894.1 | 955.2 |
| Double time | 21.7 | 70.4 | 423.2 | 479.7 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 19.7 | 54.1 | 300.8 | 338.6 |
| Hit objects only | 17.5 | 47.5 | 284.7 | 318.6 |
| End times | 20.8 | 55.1 | 301.8 | 340.7 |
| Paths | 31.2 | 65.5 | 418.7 | 455.7 |
| Geometry | 31.7 | 66.0 | 416.2 | 454.6 |
| Events | 33.9 | 68.3 | 584.7 | 624.9 |
| Stacking | 29.5 | 63.9 | 404.2 | 443.0 |
| Gameplay | 35.5 | 69.9 | 620.7 | 663.5 |
| Double time | 20.2 | 54.4 | 302.6 | 341.5 |

Small negative option costs in Python are measurement noise around eager result
construction. Paths and especially events dominate the optional work.

## Real lazer v128 maps

The v128 profile contains 100 real maps: 8 osu!standard, 43 osu!taiko, 18
osu!catch and 31 osu!mania maps, totalling 2,634,547 bytes. Its SHA-256 is
`478a7c7753242f37a87093e919d25fced19833013578c47dbb6e184c4c8b65f2`.
These values use the same hosts, toolchains and timing boundary as the legacy
feature matrix.

The corpus averages substantially smaller files and has a different mode mix.
Compare feature costs within this table; do not use its absolute values to claim
that v128 parsing is faster than legacy parsing.

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 17.1 | 45.6 | 201.1 | 229.8 | 18.3 | 38.7 | 150.3 | 172.7 |
| Hit objects only | 13.4 | 38.1 | 182.2 | 207.6 | 15.3 | 31.9 | 136.3 | 156.0 |
| End times | 17.5 | 46.0 | 202.1 | 230.4 | 18.5 | 38.9 | 150.5 | 174.1 |
| Paths | 19.7 | 48.4 | 222.1 | 251.8 | 19.8 | 40.0 | 165.3 | 188.6 |
| Geometry | 19.9 | 48.6 | 222.0 | 251.2 | 19.7 | 40.0 | 165.0 | 188.0 |
| Events | 20.8 | 49.7 | 244.9 | 275.4 | 20.1 | 40.4 | 179.7 | 204.0 |
| Stacking | 20.0 | 48.8 | 225.8 | 256.0 | 19.9 | 40.0 | 167.8 | 191.2 |
| Gameplay | 21.3 | 50.3 | 252.4 | 283.2 | 20.5 | 40.6 | 186.2 | 209.6 |
| Double time | 18.2 | 46.6 | 202.3 | 231.0 | 18.6 | 38.9 | 151.2 | 173.2 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods.

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 20.1 | 470.7 | 19.4 | 336.0 |
| DT | 20.0 | 459.0 | 19.6 | 333.9 |
| HR+DT | 20.5 | 458.9 | 19.9 | 332.7 |
| Full HR+DT | 109.5 | 1,639.9 | 63.4 | 1,144.6 |

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
