# fosu — fast osu! beatmap parsing

Parse `.osu` beatmaps into named fields and records from Python, C++, or C.
fosu combines SIMD parsing with the official osu! legacy decoder's acceptance
rules, including unusual numeric forms and malformed records.

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.ar, beatmap.hit_objects[0].time)
```

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
and the [reproducible harness](bench/comparison/README.md). The
[FOSU-only hot-loop benchmarks](docs/performance.md) use per-map minima and are
not mixed into this comparison.

## Interfaces

| Interface | Result | Use |
|---|---|---|
| [C++ library](docs/library.md) | Parser-owned `Beatmap` view | Direct parsing in a C++ application |
| [C API](docs/c-api.md) | Handle-owned parser and result view | C and other FFI callers |
| [Python package](docs/python.md) | Detached `Beatmap` dataclass and lists | Ordinary mutable Python values |
| [One-shot executable](oneshot/README.md) | Complete binary stream on stdout | Process-lifetime benchmark on Linux/Zen 4 |

All native representations are checked on the same fixed corpus of **10,000
ranked/approved maps, 402,593,897 bytes**. Comparisons cover strings, raw float
bits, every pool entry/index and parser counters. Prior releases are regression
baselines; intentional correctness fixes are accounted for separately. See
[compatibility](docs/compatibility.md) for the parsing contract and independent
references.

```sh
cmake -S . -B build/native
cmake --build build/native --target check -j4  # library and native checks
```

See [builds and checks](docs/build.md) for compiler/ISA profiles, sanitizers,
benchmarks and the optional Linux one-shot executable.

## Implementation

AVX2 classifies delimiters and digits together, then uses compile-time shuffle
masks and multiply-add instructions to convert common hitobject prefixes and
slider points. Timing points reuse validated shape geometry within a section.
Unusual numeric forms take a bounded scalar conversion path; metadata uses
key/type tables.

Input size gives safe upper bounds for fixed arrays without a second scan. The
parsing engine builds each record as a local value, then copies it into contiguous
arena memory owned by the parser. C++, C and Python share the same `Beatmap`
model; the C boundary converts its result once to the versioned ABI records.
One inactive parser arena is retained for cheap fresh-parser reuse.

The public `Parser` prepares input, allocates arrays and owns their lifetime.
One engine call interprets the complete document. C/Python builds keep the
scalar engine in the core and load only the selected AVX2 or NEON library;
header-only builds select their engine at compile time.

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
or NEON on AArch64, with scalar fallback. Header-only C++ uses the caller’s compile flags;
the one-shot binary targets Zen 4. Measurements are
bounded to the documented corpus and host; see [performance](docs/performance.md).

The project concept and original SIMD hitobject prototype are by
[Flamme](https://github.com/infernalfire72).
