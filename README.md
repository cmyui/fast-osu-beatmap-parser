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

Python returns fully populated, detached objects and lists. All supported
fields are eager; returned values do not retain native memory or depend on a parser.
Install a prebuilt wheel with `python -m pip install fosu`, or run
`python -m pip install .` in a source checkout.
See the [Python guide](docs/python.md) for installation and the complete API.

The C++20 interface is header-only:

```cpp
#include <fosu/parser.h>

fosu::Parser parser;
fosu::Beatmap* map = parser.parse_file("map.osu");
if (!map) return 1;
// map->title, map->ar, map->hit_objects, map->sliders, ...
// map remains valid until parser parses another beatmap or is destroyed.
```

## Performance

A parser keeps its memory between calls; reuse one (`fosu::Parser` or
`fosu.Parser`) when parsing many maps.

See the [parser comparison](docs/comparison.md) and
[per-option costs](docs/performance.md) on x86-64 and AArch64; the
[harness](bench/comparison/README.md) reproduces them.

## Interfaces

| Interface | Result | Use |
|---|---|---|
| [C++ library](docs/library.md) | Parser-owned `Beatmap` view | Direct parsing in a C++ application |
| [Python package](docs/python.md) | Detached `Beatmap` and lists | Eager Python values |

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

See [builds and checks](docs/build.md) for compiler/ISA profiles and
sanitizers.

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

## Acknowledgments

- [osu!](https://github.com/ppy/osu) and
  [osu!framework](https://github.com/ppy/osu-framework) by ppy Pty Ltd and
  contributors are FOSU's primary references for beatmap behavior. FOSU adapts
  curve approximation from osu!framework 2026.807.0 and osu!. FOSU's slider
  event ordering and endpoint exclusion follow `SliderEventGenerator`; full-map
  stacking follows `OsuBeatmapProcessor`. The ppy copyright and MIT license
  notices remain in the relevant headers.
- [Flamme](https://github.com/infernalfire72) originated the project concept
  and the SIMD hitobject prototype on which FOSU's AVX2 prefix parser builds.
