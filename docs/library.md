# C++ library

Include `fosu/parser.h` and compile as C++20. There is no library to link.
On the measured Linux/Zen 4 host, GCC with `-O3 -march=x86-64-v3 -mtune=znver4`
is the baseline. Native Linux AVX2 builds also use `-fno-plt` and the
[compiled-target hardening policy](build.md#hardening). Header-only applications
own their build policy.
The header-only parser uses AVX2/BMI when enabled by the compiler, or NEON on
AArch64, with scalar fallback. Define `FOSU_DISABLE_SIMD` for scalar parser
engine. For automatic runtime CPU selection, link `fosu::fosu` and use:

```cpp
#include <fosu/parser.h>
#include <fosu/runtime.h>

const auto* engine = fosu::runtime_engine();
if (!engine) return 1;  // explicitly requested backend unavailable
fosu::Parser parser(*engine);
```

`runtime_engine()` selects once from `FOSU_BACKEND=auto|scalar|avx2|neon`.
Keep the runtime library loaded while using its engine or parsers. Compile
callers and the runtime from the same release; this is not a stable binary ABI.

## Ownership and reuse

```cpp
#include <fosu/parser.h>

fosu::Parser parser;
for (const char* path : paths) {
    auto parsed = parser.parse_file(path);
    if (!parsed) return parsed.error();
    fosu::Beatmap& beatmap = *parsed.value();
    consume(beatmap);
}
```

`Parser` owns one working arena containing its padded input and current parsed
records. A parse clears and reuses that arena. The returned `Beatmap` is a plain
view into it and remains valid until the next parse attempt or parser destruction.
Its fields and array elements are writable. Strings use `std::string_view`:
the field can be reassigned, but the view does not expose writable characters.
Use separate parser instances for concurrent calls.

The byte-span overload copies its input into the working arena. `parse_file`
reads directly into it. Callers do not need to provide SIMD padding or preserve
the original input. Parser options are per call; fosu does not call `mallopt`,
change system-wide page policy or create background threads.

Copy into a longer-lived arena when a result must survive parser reuse:

```cpp
fosu::Arena* program_arena = fosu::arena_alloc();
if (!program_arena) return fosu::Error{fosu::ErrorCode::AllocationFailure};
fosu::Parser parser;

auto first = parser.parse(first_input, first_size);
if (!first) {
    fosu::arena_release(program_arena);
    return first.error();
}
auto copied = first.value()->copy(*program_arena);
if (!copied) {
    fosu::arena_release(program_arena);
    return copied.error();
}
fosu::Beatmap retained = copied.value();
auto second = parser.parse(second_input, second_size);
if (!second) {
    fosu::arena_release(program_arena);
    return second.error();
}
consume(retained);

fosu::arena_release(program_arena);
```

The destination arena owns every copied array and string. Multiple retained
beatmaps can share a program-lifetime arena; clearing or releasing that arena
invalidates all of them. Copying duplicates every referenced array and string;
editing the copy's records does not change the source. `Beatmap` does not own
or release its storage.

## Records

`Beatmap` exposes metadata fields directly (`title`, `ar`, `mode`, etc.) and
contiguous spans `hit_objects`, `sliders`, `slider_points`, `timing_points`,
`breaks`, and `combo_colours`. `HitObject::slider` is an index, or
`HitObject::kNoSlider`.
Each slider's `[point_begin, point_begin + point_count)` selects its control
points, excluding its head position. Point coordinates are signed 32-bit
integers; object and break times are double milliseconds. See the [numeric contract](compatibility.md) for bounds,
fractional values, inherited NaN timing points and skipped malformed records.

On the supported 64-bit ABIs, native hitobjects occupy 56 bytes and sliders 56
bytes, with `std::string_view` fields. Rebuild callers when updating headers.

## Selective parsing

```cpp
fosu::Parser parser;
auto difficulty = parser.parse(input, size, {
    .sections = fosu::kSectionDifficulty,
});
if (!difficulty) return difficulty.error();
consume(*difficulty.value());

auto listing = parser.parse(input, size, {
    .sections = fosu::kSectionMetadata | fosu::kSectionDifficulty,
});
if (!listing) return listing.error();
```

Unrequested sections are skipped and parsing can stop once all requested
sections have been consumed. This avoids most work for metadata/difficulty
callers. Fields from skipped sections retain defaults. Engine selection is
separate from per-call parsing options. Define `FOSU_DISABLE_SIMD` when building
a scalar-only application; parsing-path counters naturally differ between
scalar and SIMD runs.

## Performance

The parser derives safe array capacities from the bounded input size, allocates
the result arrays together, builds records as local values and copies them into
contiguous arena memory. The parser arena reserves virtual address space, makes
it writable in fixed-size chunks and reuses touched pages across calls. Its OS
boundary uses `mmap`/`mprotect` on Linux and macOS; Linux huge-page advice
remains optional.

A lock-free single-slot pool retains one released parser arena without sharing
live storage. Keeping a `Parser` is still the clearest expression of ownership
and avoids acquiring a pooled arena.
