# fosu — fast osu! beatmap parsing

A C++20 parsing library for legacy `.osu` beatmap files: a header-only C++
interface, a small in-process C ABI with owned storage, and an installable
Python package backed by CFFI. The same kernels also power a freestanding
one-shot executable used as a process-level benchmark and demonstration.
Correctness follows the official osu! legacy decoder's acceptance rules;
unusual or malformed input is skipped and counted rather than trusted.

| Interface | Result | Use |
|---|---|---|
| [C++ library](docs/library.md) | `Beatmap` with vectors and borrowed strings | Direct parsing in a C++ application |
| [C API](docs/c-api.md) | Handle-owned arena: input copy and contiguous arrays | C and other FFI callers |
| [Python package](docs/python.md) | Owned `Beatmap` with named fields and records | Python apps; optional zero-copy NumPy arrays |
| [One-shot executable](oneshot/README.md) | Complete binary stream on stdout | Process-lifetime benchmark on Linux/Zen 4 |

The native representations are checked on a fixed corpus of **10,000
ranked/approved maps, 402,593,897 bytes**, and a broader cache corpus. Comparisons
cover strings, raw float bits, every pool entry/index and parser counters.
Prior releases are regression baselines; intentional correctness fixes are
accounted for separately. See [compatibility](docs/compatibility.md) for the
parsing contract and independent references, and
[performance](docs/performance.md) for measured boundaries and reproduction.

```cpp
#include <fosu/parser.hpp>

auto input = fosu::read_file_padded("map.osu");
if (!input) return 1;
fosu::Beatmap map = fosu::parse(input);
// map.title, map.ar, map.hit_objects, map.sliders, map.slider_points, ...
// Keep input alive and unchanged while using map's string views.
```

```sh
make test                           # scalar + AVX2; Rosetta on Apple Silicon
make lib test-c-api CXX=g++          # hosted C ABI
make oneshot CXX=g++                # Linux x86-64, Zen 4 target
build/fosu_oneshot map.osu > map.fosu
python3 examples/decode_oneshot.py < map.fosu
```

```python
import fosu

beatmap = fosu.parse_file("map.osu")
print(beatmap.title, beatmap.ar, beatmap.hit_objects[0].time)
```

Install a prebuilt wheel or run `python -m pip install .` in a source checkout.
The [Python guide](docs/python.md) covers installation, automatic ownership,
section selection and NumPy access.

## Parsing strategy

One AVX2 load per hitobject line yields its newline, comma and non-digit
masks. The four prefix field lengths are packed into 16-bit lanes so a single
subtraction, addition and mask validate every bound and one multiply selects a
compile-time permutation table; multiply-add instructions then convert
`x,y,time,type` together and a branchless select reads `hitSound`. Circles
with the common 8-byte sample finish inline; sliders take an out-of-line
routine that converts the first two control points speculatively from one
32-byte window (most sliders have one or two), converts each further point
with one shuffle, and parses the length with SWAR. Blank, comment and header
lines are only examined after the editor shape fails, so no per-line
whitespace scan runs on the fast path.

Records are written through raw cursors into reserved vector capacity and
published once per section, with ordinary vector operations for debug and
sanitizer builds; timing points reuse per-section shape geometry the same way.
Metadata uses compact key/type tables. These kernels are shared by
the library, the C ABI and the executable through compile-time storage
policies; the C ABI handle keeps its input copy and arrays in an arena, with
heap growth when needed. A freed handle can leave one bounded spare arena for
a later parse; library unload releases it.
The executable adds a freestanding runtime and streams records to stdout. No
parsed results or input files are cached across processes; profile-guided
builds contain code-generation feedback only.

## Coverage and assumptions

General, Editor, Metadata, Difficulty, Events (background/video/breaks),
TimingPoints, Colours and HitObjects are represented. Circles, sliders,
spinners and mania holds are supported. BOM/CRLF, legacy timing fields and
missing ApproachRate defaults are handled. Hit samples and slider edge fields
remain raw strings. Storyboard command bodies are counted and skipped;
lazer's newer per-segment curve syntax is outside this parser's scope.

Malformed numeric records are skipped and counted; this is not a strict
playability validator. Inputs are limited to 64 MiB. Fast paths use speculative
reads; C++ byte buffers need **128 readable zero bytes after the logical end**.
File helpers, the C API and Python supply that padding. See the
[full input contract](docs/compatibility.md) before integrating a consumer.
A scalar build works without AVX2; the default Linux x86-64 library target
requires x86-64-v3, while the one-shot binary targets Zen 4. Measurements are
bounded to the documented corpus and host; see [performance](docs/performance.md).

The project concept and original SIMD hitobject prototype are by
[Flamme](https://github.com/infernalfire72). The implementation extends that
technique with parallel delimiter extraction, compile-time masks, fallback
validation and full-file parsing. The one-shot runtime and memory placement
were developed with Fable; the combined implementation is reviewed and measured
across both storage paths.
