# fosu — fast osu! beatmap parsing

Parse `.osu` beatmaps into named fields and records from Python, C++, or C.
fosu combines SIMD parsing with the official osu! legacy decoder's acceptance
rules, including unusual numeric forms and malformed records.

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.ar, beatmap.hit_objects[0].time)
```

Python results own their native storage. Strings and records are exposed as
needed, and optional NumPy views give you read-only arrays without copying.
Install a prebuilt wheel, or run `python -m pip install .` in a source checkout.
See the [Python guide](docs/python.md) for installation and the complete API.

The C++20 interface is header-only:

```cpp
#include <fosu/parser.hpp>

fosu::Parser parser;
auto parsed = parser.parse_file("map.osu");
if (!parsed) return 1;
fosu::Beatmap& map = *parsed.value();
// map.title, map.ar, map.hit_objects, map.sliders, map.slider_points, ...
// map remains valid until parser parses another beatmap or is destroyed.
```

Recorded warm parsing times on our **10,000-map corpus** are roughly **21 µs
per map in C++** and **25 µs from Python**. These are means of per-map minima
on a pinned Zen 4 core, starting with resident input bytes; they exclude file
I/O and process startup. See [performance](docs/performance.md) for the exact
measurements, host, repetition counts, and reproduction commands.

| Interface | Result | Use |
|---|---|---|
| [C++ library](docs/library.md) | Parser-owned `Beatmap` view | Direct parsing in a C++ application |
| [C API](docs/c-api.md) | Handle-owned parser and result view | C and other FFI callers |
| [Python package](docs/python.md) | Owned `Beatmap` with named fields and records | Python apps; optional zero-copy NumPy arrays |
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
