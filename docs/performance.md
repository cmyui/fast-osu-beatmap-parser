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
order rotate per entry. Each cell is the fastest of five measured runs after one
discarded warm-up run, reversing backend order between runs; interference only
adds time. Each parser lifetime is timed in its own process. On this corpus, the
slowest of a cell's five runs is at most 9.1% slower than its fastest on Intel
and 4.4% on M3. This is a lower-envelope feature-cost comparison, not a latency
percentile. Microseconds per map; lower is better.

### Intel Core i7-8700, Linux x86-64 under WSL2

GCC 15.2, `-O3`; native AVX2 uses `x86-64-v3`, while the Python AVX2 engine
uses AVX2, BMI and BMI2 without host-specific tuning. Python is CPython 3.12.14.
Runs used the WSL2 ext4 filesystem, were pinned to logical CPU 8 and used the
Windows High performance power plan.

Reused parser:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 28.0 | 83.5 | 154.7 | 209.6 |
| Hit objects only | 24.0 | 79.4 | 141.9 | 196.9 |
| End times | 30.3 | 86.0 | 157.0 | 211.8 |
| Paths | 42.0 | 97.9 | 245.0 | 297.0 |
| Geometry | 43.4 | 99.3 | 245.0 | 297.1 |
| Events | 48.4 | 104.6 | 306.4 | 359.1 |
| Stacking | 41.9 | 98.0 | 236.0 | 289.0 |
| Gameplay | 51.7 | 108.2 | 327.0 | 376.6 |
| Double time | 29.5 | 85.0 | 157.3 | 212.1 |

New parser per call:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 108.2 | 163.9 | 251.3 | 304.6 |
| Hit objects only | 99.1 | 155.6 | 232.9 | 286.2 |
| End times | 111.1 | 167.0 | 254.3 | 307.5 |
| Paths | 130.4 | 186.6 | 352.3 | 405.2 |
| Geometry | 132.3 | 188.7 | 353.0 | 404.4 |
| Events | 146.4 | 202.8 | 424.6 | 474.9 |
| Stacking | 128.8 | 185.3 | 341.7 | 392.5 |
| Gameplay | 150.7 | 207.6 | 446.6 | 498.0 |
| Double time | 109.9 | 165.6 | 254.8 | 307.3 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

Reused parser:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 18.7 | 53.5 | 106.1 | 141.0 |
| Hit objects only | 16.6 | 50.4 | 98.2 | 131.8 |
| End times | 19.7 | 54.5 | 107.2 | 142.1 |
| Paths | 25.6 | 60.4 | 170.3 | 204.5 |
| Geometry | 26.2 | 61.0 | 169.8 | 203.8 |
| Events | 27.9 | 62.7 | 211.4 | 244.9 |
| Stacking | 25.1 | 59.9 | 161.5 | 195.6 |
| Gameplay | 29.3 | 64.1 | 226.0 | 259.1 |
| Double time | 19.2 | 54.1 | 107.2 | 141.8 |

New parser per call:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 37.5 | 72.1 | 125.6 | 160.9 |
| Hit objects only | 34.4 | 68.1 | 116.8 | 151.3 |
| End times | 38.5 | 73.4 | 126.7 | 162.2 |
| Paths | 45.7 | 80.5 | 191.1 | 226.5 |
| Geometry | 46.4 | 81.1 | 190.7 | 226.2 |
| Events | 49.5 | 84.2 | 232.9 | 268.8 |
| Stacking | 44.9 | 79.6 | 181.8 | 217.5 |
| Gameplay | 51.2 | 85.8 | 247.2 | 283.5 |
| Double time | 38.0 | 72.6 | 126.7 | 162.1 |

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
that v128 parsing is faster than legacy parsing. A cell's slowest run
is up to 5% slower than its fastest.

Reused parser:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 23.8 | 54.7 | 95.5 | 124.8 | 18.0 | 39.2 | 68.8 | 89.8 |
| Hit objects only | 19.6 | 50.5 | 84.2 | 113.4 | 15.7 | 35.9 | 61.4 | 81.1 |
| End times | 24.2 | 55.1 | 96.5 | 125.5 | 18.2 | 39.3 | 69.3 | 89.8 |
| Paths | 25.7 | 56.7 | 106.7 | 135.4 | 19.0 | 40.1 | 76.7 | 97.0 |
| Geometry | 26.1 | 56.9 | 106.4 | 135.5 | 19.1 | 40.3 | 76.4 | 96.9 |
| Events | 26.7 | 57.7 | 113.8 | 142.1 | 19.4 | 40.5 | 81.7 | 102.2 |
| Stacking | 26.1 | 57.1 | 108.0 | 136.7 | 19.1 | 40.3 | 77.7 | 98.3 |
| Gameplay | 27.2 | 58.3 | 118.7 | 147.0 | 19.6 | 40.7 | 84.6 | 105.1 |
| Double time | 24.6 | 55.5 | 96.8 | 126.3 | 18.4 | 39.5 | 69.2 | 90.2 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 85.0 | 115.9 | 168.5 | 199.3 | 32.4 | 53.5 | 84.8 | 106.4 |
| Hit objects only | 75.5 | 106.8 | 151.7 | 181.8 | 29.1 | 49.2 | 76.7 | 97.1 |
| End times | 85.5 | 116.6 | 170.3 | 199.4 | 32.5 | 53.8 | 85.1 | 106.7 |
| Paths | 87.7 | 119.1 | 181.9 | 212.0 | 33.4 | 54.6 | 92.5 | 114.1 |
| Geometry | 88.4 | 119.6 | 181.3 | 211.0 | 33.5 | 54.9 | 92.6 | 114.2 |
| Events | 89.9 | 121.2 | 190.0 | 220.8 | 34.0 | 55.2 | 97.9 | 119.4 |
| Stacking | 88.2 | 119.7 | 183.7 | 213.4 | 33.5 | 54.9 | 93.9 | 115.2 |
| Gameplay | 90.9 | 122.2 | 195.2 | 226.3 | 34.5 | 55.5 | 100.7 | 122.6 |
| Double time | 85.3 | 116.9 | 170.7 | 200.6 | 32.6 | 53.8 | 85.6 | 106.8 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods. A cell's
slowest run is up to 3.6% slower than its fastest.

Reused parser:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 26.1 | 191.6 | 16.2 | 121.1 |
| DT | 26.6 | 189.7 | 16.5 | 120.9 |
| HR+DT | 27.2 | 187.1 | 16.9 | 120.9 |
| Full HR+DT | 93.8 | 638.0 | 46.8 | 419.7 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 104.2 | 283.6 | 34.7 | 143.7 |
| DT | 104.7 | 280.4 | 35.0 | 143.1 |
| HR+DT | 105.0 | 277.9 | 35.5 | 143.2 |
| Full HR+DT | 214.3 | 787.0 | 72.9 | 452.5 |

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
minimum of each workload's `mean_file_min_us`: the fastest complete run. Retain
all run values to report their spread.

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
