# C++ library

Include `fosu/parser.h`, add `src` to your include path, and compile as C++20.
The header-only parser uses the caller's compiler flags. Define
`FOSU_DISABLE_SIMD` to disable explicit SIMD paths.

```cpp
#include <fosu/parser.h>

fosu::Parser parser;
fosu::Beatmap* map = parser.parse_file("map.osu");
if (!map) return 1;
// Read map->title, map->ar, map->hit_objects, ...
```

`parse(std::string_view)` and `parse_file(path)` return `nullptr` for invalid
options, when memory runs out, or when the target client would not load the
map; `parse_file` also returns `nullptr` when the file cannot be read, with
`errno` describing the error. `parser.error()` then gives a `ParseErrorCode`
and, for `Unloadable`, the 1-based line responsible. Reusing one parser for
many maps reuses its memory.

## Target client

```cpp
auto map = parser.parse(input, {.client = fosu::Client::Lazer});
```

`ParseOptions::client` chooses whose behaviour to follow where osu!stable and
osu!lazer read a map differently: `Client::Stable`, the default, or
`Client::Lazer`. See [the target client rules](compatibility.md#target-client).

## Ownership

`Parser` owns input bytes and parsed arrays. Its result remains valid until
the next parse attempt or parser destruction. Use separate parsers concurrently.
Pointer/span input is copied; callers need neither SIMD padding nor a retained
input buffer. Fields and array elements are writable; strings are
`std::string_view` values.

To keep a result, keep its parser alive or use one parser per result.

Native records are defined in [beatmap.h](../src/fosu/beatmap.h) and
[beatmap_header.h](../src/fosu/beatmap_header.h). A slider's point range indexes
`slider_points` and excludes its head position. Use that range rather than
assuming every pooled point belongs to an accepted slider.

## Section selection

```cpp
auto listing = parser.parse(input, size, {
    .sections = fosu::kSectionMetadata | fosu::kSectionDifficulty,
});
if (!listing) return 1;
```

The default parses all sections; skipped fields retain defaults or empty arrays.
Include General for mode-dependent difficulty rules and Events for break-dependent
combo rules. See [the parsing contract](compatibility.md) for errors and limits.

Slider end times are not calculated by default. Request the work per parse with
`{.calculate_slider_end_times = true}`.
`HitObject::end_time` is a `double`. Uncalculated slider endpoints are `0`;
do not use them unless calculation was requested. Other object types always
have their endpoints populated.
Reading a field never triggers calculation.

## Runtime engine selection

Link the installed `fosu::fosu` CMake target:

```cpp
#include <fosu/parser.h>
#include <fosu/runtime.h>

const auto* engine = fosu::runtime_engine();
if (!engine) return 1;
fosu::Parser parser(*engine);
```

Selection happens once from `FOSU_BACKEND=auto|scalar|avx2|neon`; an unavailable
explicit request returns null. Keep the library loaded while using its engine
or parsers. Headers and runtime must come from the same revision; there is no
stable binary ABI. See [build instructions](build.md).
