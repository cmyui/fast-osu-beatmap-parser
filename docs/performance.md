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

Measured on 2026-09-12 from parser revision `164d691`. The performance profile
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

GCC 13.3, `-O3`; AVX2 uses `x86-64-v3` and `znver4` tuning. Python is CPython
3.12.3. Runs were pinned to one vCPU on the eight-vCPU shared-tenancy host.

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 24.5 | 81.3 | 421.9 | 478.2 |
| Hit objects only | 21.1 | 73.4 | 398.6 | 451.6 |
| End times | 26.7 | 83.7 | 425.4 | 480.8 |
| Paths | 47.3 | 106.9 | 599.1 | 654.0 |
| Geometry | 47.7 | 107.6 | 598.9 | 654.0 |
| Events | 52.9 | 115.8 | 854.8 | 908.6 |
| Stacking | 44.0 | 104.0 | 580.1 | 636.5 |
| Gameplay | 56.0 | 119.7 | 907.4 | 962.0 |
| Double time | 26.3 | 83.5 | 428.2 | 484.1 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 20.3 | 62.0 | 311.5 | 354.5 |
| Hit objects only | 18.1 | 55.3 | 294.6 | 334.4 |
| End times | 21.4 | 63.2 | 313.0 | 355.6 |
| Paths | 32.4 | 74.2 | 432.5 | 476.3 |
| Geometry | 33.0 | 74.9 | 431.9 | 473.8 |
| Events | 35.3 | 77.1 | 605.3 | 647.3 |
| Stacking | 30.8 | 72.6 | 419.2 | 462.9 |
| Gameplay | 37.0 | 78.8 | 644.1 | 689.0 |
| Double time | 20.8 | 62.7 | 312.8 | 357.1 |

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
| Decode | 18.8 | 51.3 | 204.7 | 237.7 | 19.3 | 44.5 | 156.7 | 188.2 |
| Hit objects only | 15.1 | 43.8 | 185.2 | 215.3 | 16.3 | 37.3 | 143.5 | 167.9 |
| End times | 19.1 | 52.0 | 205.5 | 238.2 | 19.6 | 44.6 | 157.9 | 186.8 |
| Paths | 21.4 | 54.5 | 227.8 | 263.3 | 20.8 | 46.0 | 174.2 | 202.1 |
| Geometry | 21.6 | 54.8 | 227.6 | 260.6 | 21.0 | 46.3 | 173.1 | 201.8 |
| Events | 22.3 | 55.7 | 250.5 | 290.0 | 21.3 | 46.6 | 188.2 | 217.4 |
| Stacking | 21.6 | 54.8 | 231.4 | 268.2 | 21.0 | 46.1 | 176.0 | 205.9 |
| Gameplay | 22.8 | 56.4 | 256.8 | 298.7 | 21.6 | 46.7 | 194.9 | 223.6 |
| Double time | 19.9 | 52.5 | 206.2 | 241.5 | 19.7 | 44.9 | 157.3 | 187.1 |

## Revision cost

An interleaved comparison against the previous `feb0606` benchmark revision used
the same legacy corpus and pinned x86 core. Each direction was measured separately
to control for variant order. Current `164d691` was slower by the following average
of the forward and reverse changes:

| Interface | AVX2 | Scalar |
|---|---:|---:|
| Native C++ | 4.40% | 4.42% |
| Python | 2.17% | 2.31% |

The matching native AVX2 and scalar changes indicate added parser work rather than
a scalar-only compiler regression. Python exposes a smaller relative change because
detached model construction is a larger part of that timing boundary.

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods.

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 24.8 | 487.6 | 20.7 | 353.2 |
| DT | 25.3 | 479.3 | 21.0 | 347.0 |
| HR+DT | 25.8 | 474.4 | 21.3 | 345.6 |
| Full HR+DT | 112.6 | 1,672.3 | 69.1 | 1,167.1 |

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
