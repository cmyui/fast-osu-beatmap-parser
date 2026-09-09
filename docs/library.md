# C++ library

Include `fosu/parser.h`, add `src` to your include path, and compile as C++20.
The header-only parser uses the caller's compiler flags. Define
`FOSU_DISABLE_SIMD` to disable explicit SIMD paths.

```cpp
#include <fosu/parser.h>

fosu::Parser parser;
auto result = parser.parse_file("map.osu");
if (!result) return 1;
fosu::Beatmap& map = *result.value();
// Read map.title, map.ar, map.hit_objects, ...
```

## Ownership

`Parser` owns input bytes and parsed arrays. Its result remains valid until
the next parse attempt or parser destruction. Use separate parsers concurrently.
Pointer/span input is copied; callers need neither SIMD padding nor a retained
input buffer. Fields and array elements are writable; strings are
`std::string_view` values.

To retain a result across parser reuse, call `map.copy(destination_arena)`.
It returns `Result<Beatmap>` and copies all referenced arrays and strings.
Check success before using the copy. The destination arena must outlive it;
clearing or releasing that arena invalidates its copies. `Beatmap` itself does
not own or release storage.

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
`{.calculate_slider_end_times = true}` (also accepted by `parse_file`).
`HitObject::end_time` is an `EndTime`, a `std::variant<double, CalculationState>`:
use `std::get_if<double>(&object.end_time)` to read a calculated endpoint, or
`std::get<CalculationState>(object.end_time)` to read `kNotCalculated`.
Only uncalculated sliders have that state; other objects have numeric endpoints.
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
