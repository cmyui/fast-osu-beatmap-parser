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

Measured on 2026-10-06 from FOSU commit `9620bc6` (after 0.7.0). The performance profile
contains 1,024 entries (986 unique beatmaps), 256 per mode and 46,029,610 bytes.
Repeated entries are deliberate products of the stratified selection. Input is
resident in memory. Python rows include the complete detached Python result and
its release.

The corpus SHA-256 is
`1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

Each run makes five passes per profile natively and three in Python. A pass
parses every corpus entry once with one profile, in a seeded shuffled order that
native and Python runs share, so no entry repeats until the next pass and the
CPU cannot learn a map's branches from the call before; profile order rotates
between passes. Each run reports its fastest pass: the arithmetic mean over every
entry in that one pass, so each value is a complete pass that actually ran. Each
cell is the fastest of five measured runs after one discarded warm-up run,
reversing backend order between runs; interference only adds time. Each parser
lifetime is timed in its own process. On this corpus, the slowest of a cell's
five runs is at most 3.6% slower than its fastest on Intel
and 11.9% on M3. This is a lower-envelope feature-cost comparison, not a latency
percentile. Microseconds per map; lower is better.

### Intel Core i7-8700, Linux x86-64 under WSL2

GCC 15.2, `-O3`; native AVX2 uses `x86-64-v3`, while the Python AVX2 engine
uses AVX2, BMI and BMI2 without host-specific tuning. Both align jumps for
Intel's JCC erratum. Python is CPython 3.12.14.
Runs used the WSL2 ext4 filesystem, were pinned to logical CPU 8 and used the
Windows High performance power plan.

Reused parser:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 32.6 | 96.6 | 170.0 | 233.8 |
| Hit objects only | 28.2 | 92.2 | 152.4 | 216.5 |
| End times | 35.9 | 99.0 | 172.8 | 235.9 |
| Paths | 49.3 | 114.5 | 267.6 | 330.8 |
| Geometry | 51.2 | 115.6 | 273.4 | 333.3 |
| Events | 56.7 | 121.2 | 337.7 | 396.7 |
| Stacking | 49.7 | 114.5 | 259.0 | 322.4 |
| Gameplay | 60.9 | 126.9 | 358.8 | 421.5 |
| Double time | 33.6 | 98.3 | 170.8 | 232.2 |

New parser per call:

| Profile | C++ AVX2 | C++ scalar | Python AVX2 | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 123.6 | 187.1 | 275.9 | 341.9 |
| Hit objects only | 112.7 | 177.7 | 252.7 | 318.3 |
| End times | 127.1 | 191.7 | 279.1 | 345.9 |
| Paths | 148.6 | 212.8 | 386.4 | 451.1 |
| Geometry | 151.1 | 215.3 | 388.6 | 457.0 |
| Events | 165.8 | 232.0 | 464.9 | 534.4 |
| Stacking | 147.1 | 211.8 | 374.4 | 440.8 |
| Gameplay | 172.3 | 237.0 | 495.0 | 564.0 |
| Double time | 124.8 | 187.5 | 278.5 | 345.2 |

### Apple M3 Max, macOS AArch64

Apple Clang 17, `-O3`; Python is CPython 3.12.9.

Reused parser:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 22.3 | 56.4 | 114.9 | 148.6 |
| Hit objects only | 19.8 | 52.5 | 105.3 | 137.2 |
| End times | 23.9 | 58.0 | 115.8 | 149.9 |
| Paths | 32.1 | 66.1 | 183.9 | 214.1 |
| Geometry | 33.3 | 67.2 | 184.1 | 215.9 |
| Events | 35.9 | 69.9 | 225.1 | 256.1 |
| Stacking | 32.5 | 66.5 | 174.1 | 206.9 |
| Gameplay | 38.6 | 72.4 | 240.4 | 273.9 |
| Double time | 22.9 | 56.8 | 115.5 | 148.1 |

New parser per call:

| Profile | C++ NEON | C++ scalar | Python NEON | Python scalar |
|---|---:|---:|---:|---:|
| Decode | 41.2 | 74.5 | 134.3 | 168.4 |
| Hit objects only | 37.2 | 69.9 | 125.6 | 158.1 |
| End times | 43.0 | 76.3 | 137.2 | 173.0 |
| Paths | 52.6 | 85.7 | 202.7 | 239.9 |
| Geometry | 53.7 | 87.3 | 204.9 | 237.2 |
| Events | 57.6 | 91.4 | 249.7 | 285.0 |
| Stacking | 52.5 | 86.1 | 196.3 | 233.1 |
| Gameplay | 60.7 | 94.3 | 265.6 | 299.0 |
| Double time | 41.5 | 75.6 | 135.4 | 169.3 |

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
is up to 11% slower than its fastest.

Reused parser:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 23.8 | 59.3 | 100.9 | 139.9 | 18.6 | 39.4 | 73.3 | 93.0 |
| Hit objects only | 18.5 | 54.6 | 87.2 | 124.0 | 16.0 | 35.9 | 64.3 | 83.0 |
| End times | 23.6 | 60.1 | 103.6 | 139.8 | 18.7 | 40.1 | 72.9 | 93.2 |
| Paths | 25.8 | 62.0 | 117.0 | 152.9 | 19.5 | 40.9 | 80.2 | 100.4 |
| Geometry | 26.5 | 62.8 | 113.1 | 153.2 | 19.6 | 41.3 | 80.3 | 101.1 |
| Events | 27.2 | 63.3 | 124.1 | 160.3 | 20.0 | 41.3 | 85.5 | 106.2 |
| Stacking | 26.0 | 63.7 | 118.6 | 154.5 | 19.7 | 40.9 | 81.6 | 102.7 |
| Gameplay | 27.7 | 64.6 | 133.5 | 164.7 | 20.3 | 41.3 | 90.0 | 109.4 |
| Double time | 24.5 | 60.5 | 103.9 | 141.3 | 18.8 | 39.7 | 73.0 | 93.1 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 C++ scalar | x86 Python AVX2 | x86 Python scalar | M3 C++ NEON | M3 C++ scalar | M3 Python NEON | M3 Python scalar |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Decode | 92.4 | 127.7 | 187.9 | 222.7 | 33.9 | 54.2 | 89.5 | 110.3 |
| Hit objects only | 81.6 | 119.0 | 162.8 | 199.9 | 30.4 | 50.3 | 78.3 | 99.4 |
| End times | 93.8 | 130.7 | 188.8 | 222.7 | 34.3 | 55.3 | 90.9 | 109.9 |
| Paths | 97.1 | 132.6 | 200.1 | 237.0 | 35.1 | 56.7 | 98.0 | 117.7 |
| Geometry | 97.4 | 133.8 | 198.0 | 236.0 | 35.8 | 56.2 | 96.6 | 118.3 |
| Events | 99.4 | 133.9 | 207.7 | 247.1 | 35.5 | 57.5 | 102.1 | 125.1 |
| Stacking | 97.8 | 134.6 | 200.5 | 239.7 | 35.7 | 56.2 | 97.3 | 120.4 |
| Gameplay | 102.1 | 136.9 | 218.8 | 253.8 | 36.2 | 57.0 | 107.3 | 128.3 |
| Double time | 95.8 | 130.1 | 187.4 | 224.1 | 33.8 | 54.7 | 90.5 | 111.5 |

## Standard gameplay and mods

HR is currently supported for standard and taiko, so the combined-mod profile is
reported on the 256-entry standard subset (255 unique beatmaps, 10,347,075 bytes).
`Full HR+DT` enables events and stacking in addition to both mods. A cell's
slowest run is up to 17.5% slower than its fastest. The widest spreads all come
from one slow Intel Python process timing new parsers; every other cell's
slowest run is within 8.6% of its fastest.

Reused parser:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 31.9 | 205.4 | 21.9 | 130.7 |
| DT | 32.0 | 206.6 | 21.9 | 130.3 |
| HR+DT | 32.8 | 207.2 | 22.4 | 130.9 |
| Full HR+DT | 108.2 | 716.7 | 68.7 | 439.9 |

New parser per call:

| Profile | x86 C++ AVX2 | x86 Python AVX2 | ARM C++ NEON | ARM Python NEON |
|---|---:|---:|---:|---:|
| Decode | 118.2 | 310.1 | 40.9 | 154.0 |
| DT | 120.2 | 311.0 | 40.5 | 152.5 |
| HR+DT | 119.4 | 312.8 | 41.2 | 152.4 |
| Full HR+DT | 241.2 | 864.3 | 94.5 | 474.1 |

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
minimum of each workload's `fastest_pass_mean_us`: the fastest complete pass.
Retain all run values to report their spread.

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
