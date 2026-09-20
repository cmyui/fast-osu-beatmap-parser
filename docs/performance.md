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

Measured on 2026-09-20 from FOSU 0.5.0 (`798b810`). The performance profile
contains 1,024 entries (986 unique beatmaps), 256 per mode and 46,029,610 bytes.
Repeated entries are deliberate products of the stratified selection. Input is
resident in memory. Native rows create a fresh `Parser`; Python rows include the
complete detached Python result and its release.

The corpus SHA-256 is
`1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

Each run reports the arithmetic mean of the fastest sample for every corpus
entry. Native uses five samples and Python three, with profile order rotated per
entry. Each cell is the median of five measured runs on Intel and three on M3,
reversing backend order between runs. Warm-up batches are excluded as whole
batches, not selected per cell. This is a lower-envelope feature-cost comparison,
not a latency percentile. Microseconds per map; lower is better.

### Intel Core i7-8700, Linux x86-64 under WSL2

GCC 15.2, `-O3`; native AVX2 uses `x86-64-v3`, while the Python AVX2 engine
uses AVX2, BMI and BMI2 without host-specific tuning. Python is CPython 3.12.14.
Runs used the WSL2 ext4 filesystem, were pinned to logical CPU 8 and used the
Windows High performance power plan.

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 29.2 | 90.9 | 155.8 | 209.8 |
| Hit objects only | 24.7 | 81.2 | 142.6 | 192.2 |
| End times | 31.7 | 93.5 | 158.4 | 212.5 |
| Paths | 54.2 | 116.3 | 257.4 | 310.8 |
| Geometry | 55.6 | 117.4 | 257.4 | 309.6 |
| Events | 62.4 | 124.6 | 320.6 | 373.0 |
| Stacking | 52.0 | 114.0 | 244.0 | 297.4 |
| Gameplay | 67.1 | 129.4 | 342.7 | 394.9 |
| Double time | 31.0 | 92.6 | 158.7 | 212.5 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 19.8 | 58.5 | 108.9 | 148.0 |
| Hit objects only | 17.4 | 51.4 | 100.8 | 135.1 |
| End times | 20.8 | 59.6 | 110.3 | 149.3 |
| Paths | 31.3 | 70.1 | 178.7 | 217.8 |
| Geometry | 31.8 | 70.7 | 177.9 | 216.9 |
| Events | 34.0 | 73.0 | 220.0 | 259.5 |
| Stacking | 29.6 | 68.5 | 168.6 | 208.3 |
| Gameplay | 35.7 | 74.7 | 235.0 | 273.9 |
| Double time | 20.3 | 59.0 | 110.2 | 149.2 |

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
| Decode | 23.7 | 59.9 | 96.9 | 128.1 | 18.5 | 41.4 | 69.5 | 92.7 |
| Hit objects only | 18.9 | 50.7 | 84.5 | 113.3 | 15.5 | 34.2 | 61.8 | 80.6 |
| End times | 24.2 | 60.4 | 97.5 | 129.4 | 18.7 | 41.7 | 69.7 | 93.2 |
| Paths | 26.7 | 62.9 | 109.2 | 140.7 | 19.9 | 42.8 | 77.5 | 101.7 |
| Geometry | 26.9 | 63.2 | 108.4 | 140.1 | 20.0 | 42.9 | 77.3 | 100.9 |
| Events | 28.0 | 64.2 | 116.7 | 148.3 | 20.3 | 43.2 | 82.5 | 106.0 |
| Stacking | 27.2 | 63.5 | 110.8 | 142.1 | 20.0 | 42.9 | 78.7 | 102.2 |
| Gameplay | 28.7 | 65.0 | 121.1 | 152.3 | 20.6 | 43.4 | 85.4 | 109.6 |
| Double time | 24.6 | 60.8 | 98.1 | 130.0 | 18.8 | 41.7 | 69.8 | 93.2 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods.

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 29.8 | 192.9 | 19.9 | 128.0 |
| DT | 29.0 | 189.0 | 20.3 | 126.8 |
| HR+DT | 29.3 | 188.3 | 20.5 | 126.4 |
| Full HR+DT | 132.7 | 685.1 | 65.4 | 449.0 |

## Run stability

Builds and benchmarks did not overlap. WSL load monitoring recorded no swapping
or CPU steal. One mixed-mode Python AVX2 process was slower: decode ranged from
155.0 to 176.7 µs/map across five runs, with the other four at 155.0–156.1.
That run remains in the median and range; its cause was not established. All
other Intel matrix cells had a full run range below 2.8% of their fastest run.

Cursor was closed before the final M3 passes, but macOS background services
remained active. No thermal warning was reported. The three final runs agreed
within 3.0% for every cell; these are repeatable desktop measurements, not a claim
of an idle machine. Earlier exploratory M3 passes are excluded as whole runs.

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

Repeat into separate CSV files, reversing backend order between runs, then take
the median of each profile's `mean_file_min_us`. Retain all run values to report
their spread; do not substitute the minimum of the complete runs.

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
