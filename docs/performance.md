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

## Parser lifetime

Every profile is measured with two parser lifetimes:

- **Reused parser:** one C++ `Parser` or Python `fosu.Parser` parses every map.
  Its memory stays reserved and mapped between calls.
- **New parser per call:** each call constructs and destroys a C++ `Parser`, or
  calls Python's module-level `fosu.parse`, which does so internally. Each call
  also reserves the parser's memory and takes a page fault on the first write
  to each page.

Reuse a parser when parsing many maps. The extra cost of a new parser is
mostly operating-system work, and it is larger on Linux than on macOS.

## Current feature costs

Measured on 2026-10-04 from FOSU 0.6.0 (`cf9350e`). The performance profile
contains 1,024 entries (986 unique beatmaps), 256 per mode and 46,029,610 bytes.
Repeated entries are deliberate products of the stratified selection. Input is
resident in memory. Python rows include the complete detached Python result and
its release.

The corpus SHA-256 is
`1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

Each run reports the arithmetic mean of the fastest sample for every corpus
entry. Native uses five samples and Python three; profile and parser-lifetime
order rotate per entry. Each cell is the median of five measured runs after one
discarded warm-up run, reversing backend order between runs. Each parser
lifetime is timed in its own process. On this corpus, the slowest and fastest of
a cell's five runs differ by at most 4.5% on Intel and 3.3% on M3. This is a
lower-envelope feature-cost comparison, not a latency percentile. Microseconds
per map; lower is better.

### Intel Core i7-8700, Linux x86-64 under WSL2

GCC 15.2, `-O3`; native AVX2 uses `x86-64-v3`, while the Python AVX2 engine
uses AVX2, BMI and BMI2 without host-specific tuning. Python is CPython 3.12.14.
Runs used the WSL2 ext4 filesystem, were pinned to logical CPU 8 and used the
Windows High performance power plan.

Reused parser:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 28.1 | 84.0 | 157.5 | 210.4 |
| Hit objects only | 24.1 | 79.8 | 144.1 | 197.7 |
| End times | 30.3 | 86.3 | 159.7 | 212.7 |
| Paths | 45.9 | 101.8 | 249.6 | 304.0 |
| Geometry | 47.4 | 103.3 | 249.0 | 303.1 |
| Events | 53.9 | 110.5 | 314.0 | 372.8 |
| Stacking | 46.2 | 102.5 | 241.7 | 295.8 |
| Gameplay | 58.5 | 115.3 | 334.2 | 395.8 |
| Double time | 29.6 | 85.5 | 160.3 | 212.7 |

New parser per call:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 108.6 | 165.6 | 253.1 | 307.7 |
| Hit objects only | 99.7 | 156.9 | 234.2 | 288.3 |
| End times | 111.3 | 168.8 | 256.1 | 309.9 |
| Paths | 134.9 | 191.9 | 358.7 | 413.1 |
| Geometry | 136.9 | 193.8 | 358.4 | 412.5 |
| Events | 152.6 | 209.7 | 434.3 | 487.4 |
| Stacking | 133.6 | 190.4 | 346.3 | 400.9 |
| Gameplay | 158.3 | 215.8 | 457.5 | 511.7 |
| Double time | 110.4 | 167.2 | 256.3 | 310.3 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

Reused parser:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 18.7 | 53.6 | 106.5 | 141.3 |
| Hit objects only | 16.6 | 50.5 | 98.5 | 132.2 |
| End times | 19.7 | 54.6 | 107.5 | 142.5 |
| Paths | 28.7 | 63.7 | 173.6 | 207.9 |
| Geometry | 29.2 | 64.3 | 173.1 | 207.4 |
| Events | 31.2 | 66.3 | 215.4 | 249.1 |
| Stacking | 27.8 | 62.9 | 164.7 | 199.1 |
| Gameplay | 32.9 | 67.9 | 230.3 | 263.7 |
| Double time | 19.2 | 54.1 | 107.5 | 142.3 |

New parser per call:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 38.2 | 72.2 | 128.7 | 162.9 |
| Hit objects only | 35.2 | 68.3 | 119.7 | 153.2 |
| End times | 39.4 | 73.5 | 129.7 | 164.1 |
| Paths | 49.7 | 83.8 | 197.0 | 232.2 |
| Geometry | 50.4 | 84.4 | 196.7 | 231.3 |
| Events | 53.8 | 87.8 | 240.2 | 274.8 |
| Stacking | 48.6 | 82.6 | 188.1 | 222.7 |
| Gameplay | 55.7 | 89.8 | 254.7 | 289.6 |
| Double time | 38.7 | 72.7 | 129.5 | 164.0 |

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
that v128 parsing is faster than legacy parsing. Its runs are short, so a single
disturbed run moves further: a cell's five runs differ by up to 7%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 24.0 | 55.3 | 97.6 | 125.9 | 18.2 | 39.3 | 69.4 | 89.8 |
| Hit objects only | 19.7 | 50.9 | 86.4 | 115.1 | 15.8 | 36.2 | 61.9 | 81.2 |
| End times | 24.3 | 55.6 | 98.5 | 126.6 | 18.3 | 39.4 | 69.6 | 90.0 |
| Paths | 26.2 | 57.5 | 110.6 | 137.9 | 19.4 | 40.5 | 77.4 | 97.6 |
| Geometry | 26.5 | 57.9 | 109.4 | 137.1 | 19.6 | 40.7 | 77.1 | 97.5 |
| Events | 27.4 | 58.9 | 117.1 | 146.0 | 19.8 | 40.9 | 82.5 | 102.7 |
| Stacking | 26.7 | 58.2 | 111.3 | 139.3 | 19.6 | 40.7 | 78.7 | 99.0 |
| Gameplay | 28.1 | 59.8 | 122.1 | 150.7 | 20.1 | 41.2 | 85.5 | 105.5 |
| Double time | 24.7 | 56.0 | 99.0 | 127.1 | 18.5 | 39.6 | 69.7 | 90.2 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 85.9 | 116.5 | 171.8 | 201.4 | 33.1 | 54.3 | 85.3 | 106.5 |
| Hit objects only | 76.4 | 107.3 | 153.7 | 184.0 | 29.8 | 50.3 | 76.6 | 98.0 |
| End times | 86.5 | 117.1 | 173.6 | 201.9 | 33.2 | 54.4 | 85.4 | 107.0 |
| Paths | 89.1 | 119.9 | 184.6 | 214.1 | 34.3 | 55.6 | 92.6 | 115.1 |
| Geometry | 89.9 | 120.7 | 184.4 | 214.6 | 34.6 | 55.9 | 93.0 | 115.4 |
| Events | 91.4 | 122.2 | 193.8 | 224.0 | 35.1 | 56.4 | 98.5 | 120.8 |
| Stacking | 89.8 | 120.4 | 186.4 | 216.4 | 34.7 | 55.7 | 94.4 | 116.8 |
| Gameplay | 92.6 | 123.3 | 198.5 | 227.9 | 35.5 | 56.5 | 101.1 | 124.0 |
| Double time | 86.9 | 117.0 | 173.0 | 201.7 | 33.5 | 54.6 | 85.3 | 107.5 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods. A cell's five
runs differ by up to 7.3%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 26.1 | 192.3 | 16.2 | 122.8 |
| DT | 26.6 | 188.9 | 16.5 | 122.4 |
| HR+DT | 27.2 | 187.7 | 16.9 | 122.2 |
| Full HR+DT | 113.0 | 660.0 | 58.8 | 440.3 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 106.0 | 286.2 | 35.0 | 145.0 |
| DT | 106.1 | 285.0 | 35.1 | 144.9 |
| HR+DT | 106.8 | 281.3 | 35.5 | 144.6 |
| Full HR+DT | 236.5 | 816.9 | 85.2 | 468.6 |

## Reproduce FOSU measurements

Build the native matrix for the desired ISA, then use the same corpus and
manifest for every run:

```sh
cmake -S . -B build/native -DFOSU_ISA=avx2
cmake --build build/native --target feature_matrix -j4
build/native/feature_matrix /path/to/maps 5 avx2 all-modes reused > build/native.csv
python bench/summarize.py build/native.csv --corpus-manifest /path/to/manifest.csv

FOSU_BACKEND=avx2 python bench/python_feature_matrix.py /path/to/maps \
  --reps 3 --variant python-avx2 --suite all-modes --parser reused > build/python.csv
python bench/summarize.py build/python.csv --corpus-manifest /path/to/manifest.csv
```

Each run times one parser lifetime: `reused` shares one parser, and `fresh`
creates a new parser per call. Time them in separate processes; a fresh parser
maps and unmaps its memory every call, which also slows interleaved
reused-parser calls on Linux. Repeat into separate CSV files, discarding the
first run as a warm-up and reversing backend order between runs, then take the
median of each workload's `mean_file_min_us`. Retain all run values to report
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
