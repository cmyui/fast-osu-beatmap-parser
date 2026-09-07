# fosu — fast osu! beatmap parsing

A C++20 parser for legacy `.osu` beatmap files. Its primary target is
**one fresh Linux process → read one original beatmap → parse → write the complete
result → exit**. It also provides a header-only C++ library and a small C ABI for
in-process callers, plus an installable Python package backed by CFFI.

| Interface | Result | Use |
|---|---|---|
| [One-shot executable](oneshot/README.md) | Complete binary stream on stdout | Lowest measured process lifetime on Linux/Zen 4 |
| [C++ library](docs/library.md) | `Beatmap` with vectors and borrowed strings | Direct parsing in a C++ application |
| [C API](docs/c-api.md) | Handle-owned input and contiguous arrays | C and other FFI callers |
| [Python package](docs/python.md) | Owned `Beatmap` with named fields and records | Python apps; optional zero-copy NumPy arrays |

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

One AVX2 load classifies the hitobject prefix `x,y,time,type,hitSound` and
finds its newline. Delimiter positions select a compile-time permutation and
shuffle table; multiply-add instructions convert several fields together.
The result lands directly in its final record. Unusual shapes take a scalar
fallback, including signed/wide coordinates and fractional timestamps.

Timing-point lines reuse their delimiter geometry within the current section.
Slider points are written through a cursor, with vector/SWAR decimal conversion
and a general numeric fallback. Metadata uses compact key/type tables. These
numeric kernels, metadata definitions and hitobject framing are shared between
the library and executable through compile-time templates. Storage-specific
slider handling stays separate: vectors for library callers, inline records
for stdout. There are no virtual calls or macros that change `Beatmap`'s layout.

The executable adds a freestanding runtime, a single input/output arena and
streamed records. Its fastest configuration uses Linux multi-size transparent
huge pages to reduce first-touch faults. The library leaves allocator and host
policy to its caller. No parsed results or input files are cached across
processes; profile-guided builds contain code-generation feedback only.

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
requires x86-64-v3, while the one-shot binary targets Zen 4.

The project concept and original SIMD hitobject prototype are by
[Flamme](https://github.com/infernalfire72). The implementation extends that
technique with parallel delimiter extraction, compile-time masks, fallback
validation and full-file parsing. The one-shot runtime and memory placement
were developed with Fable; the combined implementation is reviewed and measured
across both storage paths.
