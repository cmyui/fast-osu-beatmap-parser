# C++ library

Include `fosu/parser.hpp` and compile as C++20. There is no library to link.
On the measured Linux/Zen 4 host, GCC with `-O3 -march=x86-64-v3 -mtune=znver4`
is the baseline. `make` also uses `-fno-plt -fno-stack-protector` for its Linux
benchmark and shared-library targets. Applications own their build policy.
Without AVX2/BMI compile flags the parser uses its scalar path; there is no
runtime CPU dispatch.

## Ownership and reuse

```cpp
#include <fosu/parser.hpp>

fosu::FileBuffer input;
fosu::Beatmap result;
for (const char* path : paths) {
    if (!fosu::read_into(path, input)) continue;
    fosu::parse_into(input, result);
    consume(result);
}
```

`read_into` retains input capacity. `parse_into` resets values and retains vector
capacity, growing only when needed. It retains no result from the previous map.
Use separate buffers/results for concurrent calls. Reusing a result is useful
in servers and avoids depending on an allocator's page-retention policy.

`parse(input)` returns a fresh `Beatmap`; `parse_into(input, result)` fills an
existing one. The pointer/size overloads accept original bytes without copying
when **128 readable zero bytes follow the logical input**. `make_padded(view)`
copies network or other unpadded bytes into a suitable `FileBuffer`.

String fields borrow the input, except immutable defaults such as `"Normal"`.
Keep that input alive and unchanged for the lifetime of those views. Moving a
`FileBuffer` transfers its ownership without relocating its bytes. Re-reading
into it can invalidate all earlier views. Parsing into a result invalidates
its previous vectors, references and iterators. Parser options and vector
capacity are per caller; fosu does not call `mallopt`, set huge-page policy or
create background threads.

## Records

`Beatmap` exposes metadata fields directly (`title`, `ar`, `mode`, etc.) and
vectors `hit_objects`, `sliders`, `slider_points`, `timing_points`, `breaks`,
`combo_colours`. `HitObject::slider` is an index, or `HitObject::kNoSlider`.
Each slider's `[point_begin, point_begin + point_count)` selects its control
points, excluding its head position. Point coordinates are signed 32-bit
integers; object and break times are double milliseconds. See the [numeric contract](compatibility.md) for bounds,
fractional values, inherited NaN timing points and skipped malformed records.

On the supported 64-bit ABIs, native hitobjects occupy 56 bytes and sliders 56
bytes, with `std::string_view` fields. A separate compact representation is
available without changing the native public record types:

```cpp
#include <fosu/offset_beatmap.hpp>

fosu::OffsetBeatmap compact;
fosu::parse_into(input, compact);
auto sample = compact.resolve(compact.hit_objects[0].hit_sample);
```

`OffsetBeatmap` uses the C API's 48-byte hitobjects and 40-byte sliders. Record
strings contain 32-bit offsets/lengths into the borrowed input; metadata still
uses `std::string_view`. `BasicBeatmap` also takes the array container as a
template parameter (`std::vector` by default); the C ABI handle instantiates it
with arena-backed arrays without changing either public type. The shared 64 MiB input limit keeps parsed spans within `uint32_t`.
`parse_into` sets the string base automatically. Both representations instantiate
the same parser; the C API adds input ownership and status-code translation.

The header-only C++ object layout is not a versioned binary ABI: rebuild callers
when updating headers. Use the [versioned C interface](c-api.md) across an FFI.

## Selective parsing

```cpp
auto difficulty = fosu::parse(input, {.sections = fosu::kSectionDifficulty});
auto listing = fosu::parse(input, {
    .sections = fosu::kSectionMetadata | fosu::kSectionDifficulty,
});
```

Unrequested sections are skipped and parsing can stop once all requested
sections have been consumed. This avoids most work for metadata/difficulty
callers. Fields from skipped sections retain defaults. `ParseOptions::use_simd`
can disable SIMD for cross-checking; path counters naturally differ between
scalar and SIMD runs.

## Performance

The parser writes records directly into reserved vector capacity and publishes
the sizes once per section, so the steady-state cost of a `Beatmap` result is
the records themselves. In a fresh process the first parse also pays the page
faults of that memory through the process allocator; the C ABI's arena reduces
those for C and Python callers, while the header-only interface keeps the
caller's allocator. Use `-O3` for the hosted library on the measured target;
`-O2` was slower.
Profile-guided compilation of the calling application can improve it further.
[The benchmark guide](performance.md) includes an executable GCC experiment
with disjoint training/evaluation files. A header-only library cannot supply a
universal profile for an application's call sites, allocator and input mix.
