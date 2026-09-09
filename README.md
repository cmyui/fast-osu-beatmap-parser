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
Install a prebuilt wheel, or run `python -m pip install .` in a source checkout.
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

These are historical measurements, not necessarily benchmarks of the current revision.

### Python APIs

FOSU's eager AVX2 Python interface averages **498 µs per map** from resident bytes on our
Hetzner Zen 4 VM. We compared public parser APIs on the fixed **10,000-map
corpus**, using the same **9,758 mutually accepted maps** for every row below.
Lower is better.

| Python interface | Resident bytes (µs/map) | Warm file (µs/map) |
|---|---:|---:|
| FOSU Python AVX2 (eager) | 498.3 | 510.7 |
| FOSU Python scalar (eager) | 548.7 | 554.7 |
| rosu-pp-py 4.0.2 | 310.5 | 319.1 |
| pyttanko 2.1.0 | 2,501.5 | 2,501.2 |
| OsuPyParser 1.0.7 | Unsupported (file-only API) | 5,070.8 |
| slider 0.8.4 | 14,091.0 | 14,119.7 |

Two complete batch passes on one pinned CPU, CPython 3.12; imports and startup
excluded. File inputs are in the OS page cache. FOSU's timings include constructing
and releasing every supported field as detached Python values. FOSU and rosu-pp-py
were measured together again; other libraries retain the same-cohort measurements from the
[comparison report](docs/comparison.md). Native PP-oriented results, such as
rosu-pp-py's, do not construct an equivalent Python object graph.

### C++ and other languages

Resident-input API latency on the same Hetzner host, using **9,986 common maps**
for every row. These are means of two per-call passes, **not directly
comparable to the Python batch measurements above**. Lower is better.

| Library / interface | Mean µs/map |
|---|---:|
| FOSU C++ AVX2 | 32.5 |
| FOSU C++ scalar | 77.8 |
| rosu-pp (Rust) | 332.9 |
| Coosu (C#) | 619.6 |
| rosu-map (Rust) | 680.0 |
| OsuParsers (C#) | 851.0 |
| Official osu!lazer decoder (C#) | 2,839.7 |
| osu-parsers (TypeScript/JS) | 2,842.5 |
| osu-parser (JavaScript) | 54,387.8 |

FOSU rows were refreshed on the same cohort in a FOSU-only interleaved run;
competitor rows retain the original multi-runtime sweep. Different worker mixes
can affect CPU-cache warmth. See the report for both runs and their limitations.

Parsers differ in output and may also build slider geometry, apply gameplay
defaults, or derive statistics. No PP/difficulty calculation is requested in either comparison.
These are practical API costs, not identical-work claims.

See the [full comparison](docs/comparison.md) for versions, exact APIs,
Python object traversal, per-pass variation, failure counts,
and the [reproducible harness](bench/comparison/README.md). See [performance](docs/performance.md) for internal measurement commands.

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
missing ApproachRate defaults are handled. Hit samples and slider edge fields
remain raw strings. Storyboard command bodies are counted and skipped;
lazer's newer per-segment curve syntax is outside this parser's scope.

Malformed numeric records are skipped and counted; this is not a strict
playability validator. Inputs are limited to 64 MiB. Fast paths use speculative
reads; `Parser` copies inputs into owned storage with **128 readable zero bytes
after the logical end**. See the
[full input contract](docs/compatibility.md) before integrating a consumer.
The compiled library and Python package select AVX2 on supported x86-64 CPUs
or NEON on AArch64, with scalar fallback. Header-only C++ uses the caller’s compile flags.
Measurements are
bounded to the documented corpus and host; see [performance](docs/performance.md).

The project concept and original SIMD hitobject prototype are by
[Flamme](https://github.com/infernalfire72).
