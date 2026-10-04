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

Measured on 2026-10-04 from FOSU 0.6.0 (`20ccd67`). The performance profile
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
a cell's five runs differ by at most 4.2% on Intel and 3.1% on M3. This is a
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
| Decode | 31.2 | 93.1 | 159.3 | 222.2 |
| Hit objects only | 24.6 | 86.0 | 144.0 | 206.3 |
| End times | 33.3 | 95.4 | 161.4 | 224.7 |
| Paths | 49.0 | 110.9 | 253.3 | 315.7 |
| Geometry | 50.4 | 112.5 | 253.5 | 314.9 |
| Events | 57.0 | 119.6 | 317.4 | 379.6 |
| Stacking | 49.4 | 111.7 | 244.4 | 307.8 |
| Gameplay | 61.6 | 124.4 | 339.3 | 401.6 |
| Double time | 32.8 | 94.6 | 162.0 | 224.6 |

New parser per call:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 112.2 | 175.1 | 254.9 | 320.4 |
| Hit objects only | 100.8 | 164.2 | 232.8 | 298.5 |
| End times | 114.8 | 178.1 | 257.4 | 322.7 |
| Paths | 138.2 | 201.6 | 359.6 | 426.2 |
| Geometry | 140.1 | 203.5 | 358.9 | 425.0 |
| Events | 155.8 | 219.5 | 434.4 | 499.2 |
| Stacking | 137.0 | 200.5 | 347.6 | 414.2 |
| Gameplay | 161.4 | 225.6 | 458.4 | 522.9 |
| Double time | 113.9 | 176.9 | 257.7 | 323.4 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

Reused parser:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 20.7 | 57.6 | 108.8 | 146.6 |
| Hit objects only | 17.3 | 53.6 | 99.4 | 136.8 |
| End times | 21.8 | 58.7 | 109.9 | 147.8 |
| Paths | 30.8 | 67.8 | 176.9 | 214.0 |
| Geometry | 31.3 | 68.4 | 176.3 | 213.4 |
| Events | 33.3 | 70.3 | 219.2 | 255.5 |
| Stacking | 29.9 | 66.9 | 168.1 | 204.7 |
| Gameplay | 35.0 | 72.0 | 233.4 | 270.0 |
| Double time | 21.3 | 58.2 | 109.9 | 147.6 |

New parser per call:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 40.1 | 77.4 | 129.2 | 168.3 |
| Hit objects only | 35.7 | 72.3 | 119.0 | 157.5 |
| End times | 41.3 | 78.6 | 130.5 | 169.9 |
| Paths | 51.7 | 88.9 | 198.5 | 237.9 |
| Geometry | 52.4 | 89.6 | 198.2 | 237.4 |
| Events | 55.8 | 93.0 | 241.2 | 281.4 |
| Stacking | 50.5 | 87.7 | 189.2 | 228.2 |
| Gameplay | 57.7 | 94.9 | 256.4 | 296.3 |
| Double time | 40.7 | 77.9 | 130.4 | 169.9 |

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
disturbed run moves further: a cell's five runs differ by up to 14%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 25.6 | 60.4 | 99.0 | 133.9 | 18.2 | 39.6 | 69.1 | 91.6 |
| Hit objects only | 19.7 | 54.1 | 86.0 | 120.5 | 15.1 | 35.9 | 61.1 | 82.7 |
| End times | 25.9 | 60.7 | 99.6 | 134.3 | 18.4 | 39.8 | 69.5 | 91.8 |
| Paths | 27.8 | 62.6 | 110.2 | 145.8 | 19.5 | 40.9 | 77.2 | 99.7 |
| Geometry | 28.2 | 63.1 | 110.2 | 145.3 | 19.6 | 41.1 | 77.0 | 99.2 |
| Events | 29.1 | 63.9 | 118.4 | 153.9 | 19.9 | 41.3 | 82.5 | 105.1 |
| Stacking | 28.4 | 63.2 | 112.1 | 147.5 | 19.7 | 41.1 | 78.6 | 100.9 |
| Gameplay | 29.8 | 64.8 | 123.0 | 158.0 | 20.2 | 41.7 | 85.1 | 108.0 |
| Double time | 26.4 | 61.2 | 100.7 | 135.5 | 18.5 | 39.9 | 69.5 | 91.7 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 87.2 | 122.2 | 174.5 | 210.4 | 34.0 | 54.3 | 86.9 | 107.9 |
| Hit objects only | 75.9 | 111.1 | 155.4 | 191.7 | 29.8 | 49.9 | 78.2 | 98.4 |
| End times | 87.8 | 122.9 | 174.8 | 211.7 | 34.3 | 54.6 | 87.5 | 108.5 |
| Paths | 90.8 | 125.6 | 187.8 | 224.5 | 35.5 | 55.8 | 95.0 | 116.0 |
| Geometry | 91.3 | 126.4 | 186.5 | 223.1 | 35.7 | 56.1 | 95.2 | 116.2 |
| Events | 93.1 | 128.0 | 195.6 | 234.9 | 36.1 | 56.5 | 100.7 | 121.7 |
| Stacking | 91.2 | 126.2 | 189.7 | 225.7 | 35.8 | 56.0 | 96.9 | 117.3 |
| Gameplay | 94.1 | 129.3 | 202.6 | 238.2 | 36.5 | 56.9 | 103.5 | 124.5 |
| Double time | 88.3 | 123.0 | 175.8 | 211.4 | 34.3 | 54.7 | 87.8 | 108.8 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods. A cell's five
runs differ by up to 6.1%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 28.8 | 196.9 | 17.9 | 124.2 |
| DT | 29.1 | 194.3 | 18.3 | 123.9 |
| HR+DT | 29.8 | 193.6 | 18.6 | 123.7 |
| Full HR+DT | 116.2 | 668.9 | 60.1 | 440.9 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 108.5 | 291.1 | 37.3 | 145.6 |
| DT | 108.7 | 289.3 | 37.5 | 144.9 |
| HR+DT | 109.1 | 286.9 | 37.8 | 144.5 |
| Full HR+DT | 238.6 | 819.2 | 88.2 | 469.6 |

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
