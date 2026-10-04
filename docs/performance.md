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

Measured on 2026-10-04 from FOSU commit `7ea3961` (after 0.6.0). The performance profile
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
a cell's five runs differ by at most 9.1% on Intel and 4.4% on M3. This is a
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
| Decode | 28.1 | 83.5 | 155.9 | 212.0 |
| Hit objects only | 24.1 | 79.4 | 142.5 | 198.6 |
| End times | 30.4 | 86.0 | 158.0 | 214.4 |
| Paths | 42.1 | 97.9 | 246.3 | 301.9 |
| Geometry | 43.6 | 99.4 | 246.4 | 301.2 |
| Events | 48.6 | 104.6 | 307.5 | 364.0 |
| Stacking | 42.1 | 98.1 | 238.2 | 293.5 |
| Gameplay | 51.9 | 108.3 | 327.8 | 385.4 |
| Double time | 29.5 | 85.0 | 158.6 | 214.1 |

New parser per call:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 108.5 | 164.2 | 253.3 | 306.9 |
| Hit objects only | 99.5 | 155.8 | 233.4 | 288.1 |
| End times | 111.3 | 167.3 | 255.2 | 309.8 |
| Paths | 131.2 | 186.8 | 354.2 | 407.3 |
| Geometry | 133.0 | 189.0 | 354.1 | 406.9 |
| Events | 147.2 | 203.2 | 426.4 | 477.9 |
| Stacking | 129.2 | 185.4 | 342.3 | 395.0 |
| Gameplay | 151.8 | 207.9 | 447.6 | 500.6 |
| Double time | 110.2 | 166.0 | 255.6 | 310.0 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

Reused parser:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 18.7 | 53.6 | 106.6 | 141.5 |
| Hit objects only | 16.6 | 50.6 | 98.6 | 132.5 |
| End times | 19.7 | 54.6 | 107.6 | 142.8 |
| Paths | 25.7 | 60.5 | 170.4 | 204.7 |
| Geometry | 26.2 | 61.1 | 170.0 | 204.3 |
| Events | 27.9 | 62.7 | 211.6 | 245.3 |
| Stacking | 25.1 | 60.0 | 161.7 | 196.2 |
| Gameplay | 29.4 | 64.2 | 226.1 | 259.7 |
| Double time | 19.3 | 54.1 | 107.6 | 142.4 |

New parser per call:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 37.8 | 72.6 | 127.5 | 161.9 |
| Hit objects only | 34.7 | 68.8 | 118.7 | 152.5 |
| End times | 38.8 | 73.7 | 128.9 | 163.1 |
| Paths | 46.0 | 80.9 | 192.8 | 227.5 |
| Geometry | 46.7 | 81.5 | 192.4 | 226.8 |
| Events | 49.8 | 84.6 | 235.2 | 269.5 |
| Stacking | 45.2 | 80.0 | 183.7 | 218.3 |
| Gameplay | 51.5 | 86.2 | 249.4 | 284.3 |
| Double time | 38.4 | 73.2 | 128.7 | 163.0 |

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
that v128 parsing is faster than legacy parsing. A cell's five runs
differ by up to 5%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 24.0 | 54.8 | 96.0 | 126.4 | 18.2 | 39.2 | 69.5 | 90.2 |
| Hit objects only | 19.8 | 50.5 | 84.6 | 115.1 | 15.8 | 36.0 | 61.8 | 81.6 |
| End times | 24.4 | 55.3 | 96.8 | 127.4 | 18.4 | 39.4 | 69.6 | 90.4 |
| Paths | 25.9 | 56.8 | 107.4 | 138.8 | 19.1 | 40.1 | 76.8 | 97.7 |
| Geometry | 26.2 | 57.1 | 106.8 | 137.6 | 19.3 | 40.3 | 76.8 | 97.6 |
| Events | 26.9 | 57.8 | 114.4 | 144.4 | 19.5 | 40.6 | 81.9 | 102.9 |
| Stacking | 26.4 | 57.3 | 109.0 | 139.8 | 19.3 | 40.4 | 78.2 | 98.9 |
| Gameplay | 27.4 | 58.5 | 119.1 | 149.5 | 19.8 | 40.9 | 85.0 | 105.9 |
| Double time | 24.8 | 55.6 | 97.4 | 127.2 | 18.5 | 39.6 | 69.8 | 90.7 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 85.4 | 116.6 | 171.4 | 202.3 | 32.7 | 53.9 | 86.3 | 108.3 |
| Hit objects only | 75.8 | 107.3 | 153.5 | 186.3 | 29.5 | 49.7 | 78.2 | 98.9 |
| End times | 85.7 | 117.2 | 171.8 | 203.6 | 32.9 | 54.2 | 86.6 | 108.6 |
| Paths | 88.2 | 119.5 | 184.1 | 214.7 | 33.8 | 55.1 | 94.5 | 116.2 |
| Geometry | 88.9 | 120.1 | 184.2 | 215.2 | 34.1 | 55.3 | 94.5 | 115.9 |
| Events | 90.3 | 121.4 | 192.7 | 225.4 | 34.4 | 55.8 | 99.8 | 121.6 |
| Stacking | 88.8 | 119.9 | 185.9 | 217.2 | 34.1 | 55.3 | 95.4 | 117.2 |
| Gameplay | 91.0 | 122.7 | 197.0 | 229.5 | 34.8 | 56.0 | 102.4 | 124.6 |
| Double time | 86.2 | 117.1 | 172.2 | 204.5 | 33.1 | 54.3 | 86.9 | 109.0 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods. A cell's five
runs differ by up to 3.5%.

Reused parser:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 26.2 | 191.9 | 16.3 | 123.6 |
| DT | 26.7 | 190.1 | 16.6 | 123.3 |
| HR+DT | 27.3 | 187.6 | 17.0 | 122.9 |
| Full HR+DT | 94.3 | 641.6 | 46.8 | 426.8 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 105.2 | 286.7 | 35.5 | 144.6 |
| DT | 104.8 | 283.8 | 35.7 | 144.2 |
| HR+DT | 105.6 | 281.9 | 36.1 | 143.6 |
| Full HR+DT | 215.3 | 794.8 | 73.9 | 456.8 |

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
