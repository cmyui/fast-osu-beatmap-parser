# fosu — fast osu! beatmap parsing

> **Active development:** This repo is experimental and may break without notice.
> APIs, behavior, and build interfaces are not stable. It is not production-ready;
> contributors and coding agents should follow [AGENTS.md](AGENTS.md).

Parse `.osu` beatmaps into named fields and records from Python or C++.
fosu combines SIMD parsing with the official osu! legacy decoder's acceptance
rules, including unusual numeric forms and malformed records.

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.ar, beatmap.hit_objects[0].time)
```

Pass `mods=fosu.Mods.HARD_ROCK | fosu.Mods.DOUBLE_TIME` to return gameplay
positions, difficulty settings, and timeline values with supported mods applied.
EZ and HR currently support osu!standard and osu!taiko; osu!catch and osu!mania
support is planned. DT, NC, and HT support every mode.

Python returns fully populated, mutable dataclasses and lists. All supported
fields are eager; returned values do not retain native memory or depend on a parser.
Install a prebuilt wheel with `python -m pip install fosu`, or run
`python -m pip install .` in a source checkout.
See the [Python guide](docs/python.md) for installation and the complete API.

The C++20 interface is header-only:

```cpp
#include <fosu/parser.h>

fosu::Parser parser;
auto parsed = parser.parse_file("map.osu");
if (!parsed) return 1;
fosu::Beatmap& map = *parsed.value();
// map.title, map.ar, map.hit_objects, map.sliders, map.slider_points, ...
// map remains valid until parser parses another beatmap or is destroyed.
```

## Performance

Benchmarks use public APIs and include result construction and release. Lower is
better. Current FOSU measurements use parser source `164d691` and a representative
1,024-entry corpus: 256 entries per game mode and 46,029,610 bytes total.

### Python: structural decode

This scenario requests a normal decoded beatmap without optional gameplay
calculations. Every row uses the same 1,004 mutually accepted all-mode entries.

| Python interface | Result contract | Resident bytes (µs/map) | Warm file (µs/map) |
|---|---|---:|---:|
| FOSU AVX2 | Full supported document | 438.1 | 460.3 |
| FOSU scalar | Full supported document | 497.5 | 522.2 |
| OsuPyParser 1.0.7 | Different eager model and derived statistics | Unsupported | 4,817.2 |

Packages like rosu-pp and its Python bindings are intentionally excluded. They construct
a significantly reduced PP-oriented model, not a general-purpose beatmap document.

### Python: slider geometry

This standard-mode scenario requires slider end times and queryable paths. FOSU
enables `calculate_slider_end_times` and `calculate_slider_paths`; slider is the
only comparable parser that supports this functionality in a Python API. It performs
its end-time and curve construction during ordinary parsing. Stacking is disabled
for both parsers. All 256 entries are accepted by both parsers.

| Python interface | Resident bytes (µs/map) | Warm file (µs/map) |
|---|---:|---:|
| FOSU AVX2 | 991.6 | 976.2 |
| FOSU scalar | 1,001.4 | 1,060.8 |
| slider 0.8.4 | 13,855.4 | 14,411.9 |

### Native and other languages

Resident-input public API latency on the same Zen 4 host. These two interleaved
passes use 1,023 common all-mode entries and are not directly comparable to the
isolated Python batch measurements above.

| Library / interface | Result scope | Mean µs/map |
|---|---|---:|
| FOSU C++ AVX2 | Full supported document | 46.1 |
| FOSU C++ scalar | Full supported document | 103.5 |
| rosu-map 0.2.1 (Rust) | General-purpose document | 576.4 |
| Coosu 2.5.1 (C#) | Typed document plus normal post-processing | 723.9 |
| OsuParsers 1.7.2 (C#) | Rich document and storyboard decoding | 860.5 |
| Official osu!lazer decoder (C#) | Rich ruleset model and processing | 2,957.1 |
| osu-parsers 4.1.7 (TypeScript) | Rich document model | 3,009.8 |
| osu-parser 0.3.3 (JavaScript) | Automatically derives slider/gameplay values | 13,190.3 |

The official osu!lazer completely and rosu-map largely support lazer-specific
v128 beatmap features. FOSU supports the v128 fields represented by its public
model; other parsers in this table have more limited or no v128 coverage.

### FOSU on real lazer v128 maps

The separate v128 profile contains 100 real maps across all four modes. These
files are smaller and have a different mode distribution, so compare option
costs within this profile rather than its absolute latency against the legacy
corpus.

| Profile | x86 C++ AVX2 | x86 Python AVX2 | M3 C++ NEON | M3 Python NEON |
|---|---:|---:|---:|---:|
| Decode | 18.8 | 204.7 | 19.3 | 156.7 |
| Gameplay | 22.8 | 256.8 | 21.6 | 194.9 |

The [full comparison](docs/comparison.md) defines the result contracts, execution
models, versions, per-pass variation and coverage. [Performance details](docs/performance.md)
show the cost of each FOSU option on x86-64 and AArch64. See the
[reproducible harness](bench/comparison/README.md) for exact timing boundaries.

## Interfaces

| Interface | Result | Use |
|---|---|---|
| [C++ library](docs/library.md) | Parser-owned `Beatmap` view | Direct parsing in a C++ application |
| [Python package](docs/python.md) | Detached `Beatmap` dataclass and lists | Ordinary mutable Python values |

Native representations are checked against a fixed all-mode compatibility
corpus. Comparisons cover strings, raw float
bits, every pool entry/index and parser counters. Prior releases are regression
baselines; intentional correctness fixes are accounted for separately. See
[compatibility](docs/compatibility.md) for the parsing contract and independent
references.

```sh
cmake -S . -B build/native
cmake --build build/native --target check -j4  # library and native checks
```

See [builds and checks](docs/build.md) for compiler/ISA profiles, sanitizers,
and benchmarks.

## Coverage and assumptions

General, Editor, Metadata, Difficulty, Events (background/video/breaks),
TimingPoints, Colours and HitObjects are represented. Circles, sliders,
spinners and mania holds are supported. BOM/CRLF, legacy timing fields and
missing ApproachRate defaults are handled. Modern per-segment slider curves and
explicit B-spline degrees are supported. Hit samples and slider edge fields
remain raw strings. Storyboard command bodies are counted and skipped.

Malformed numeric records are skipped and counted; this is not a strict
playability validator. Fast paths use speculative reads; `Parser` copies inputs
into owned storage with **128 readable zero bytes
after the logical end**. See the
[full input contract](docs/compatibility.md) before integrating a consumer.
The compiled library and Python package select AVX2 on supported x86-64 CPUs
or NEON on AArch64, with scalar fallback. Header-only C++ uses the caller’s compile flags.
Measurements are
bounded to the documented corpus and host; see [performance](docs/performance.md).

The project concept and original SIMD hitobject prototype are by
[Flamme](https://github.com/infernalfire72).
