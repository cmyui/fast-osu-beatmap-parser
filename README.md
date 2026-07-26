# fast-osu-beatmap-parser (fosu)

A header-only C++20 parser for the `.osu` beatmap format, built around a
table-driven AVX2 fast path for hitobject lines, a zero-copy design, and
data-fitting fast paths that exploit what editor-emitted files guarantee.

**Design goal: performance for valid, editor-emitted beatmaps.** This
project deliberately inverts the usual priority order — robustness against
hand-edited/adversarial files and code readability are explicit non-goals.
Format regularities observed in real ranked maps (integer timing offsets,
key names unique on their first bytes, field-count conventions per format
version) are treated as free information and exploited directly. Structurally
surprising lines defer to generic fallback parsers rather than being
validated up front, so common shapes pay nothing.

The SIMD hitobject technique is based on a prototype by
[Flamme](https://fla.me), extended here with parallel delimiter extraction,
compile-time mask generation, structural validation, and a full-format parser
around it.

## Usage

```cpp
#include <fosu/parser.hpp>

auto buf = fosu::read_file_padded("map.osu");   // guarantees 128B zero padding
fosu::Beatmap bm = fosu::parse(buf);
// bm.hit_objects, bm.timing_points, bm.sliders, bm.ar, bm.title, ...
// string_view fields point into `buf` — keep it alive.

// Parse-many loop: reuse both buffers and the steady state allocates
// nothing (+32% measured on real maps; see Benchmarks).
fosu::FileBuffer fb;
fosu::Beatmap bm2;
for (const char* path : paths) {
    if (!fosu::read_into(path, fb)) continue;
    fosu::parse_into(fb, bm2);      // clears bm2, keeps vector capacity
    consume(bm2);                   // valid until the next parse_into
}
```

Bytes that don't come from a file (e.g. an HTTP body) parse zero-copy via
`fosu::parse(data, size)` as long as the buffer has `fosu::kBufferPadding`
(128) readable zero bytes past the end.

```sh
make test          # native + x86-64-v3 test suites (Rosetta on Apple Silicon)
make bench         # synthetic corpus benchmark
make bench BENCH_ARGS=/path/to/osu/Songs/dir   # real .osu files
```

## How the hitobject fast path works

A hitobject line prefix is `x,y,time,type,hitSound` with variable-width
fields — the classic reason parsers fall back to scalar `strtol` loops.
Instead:

1. **One vector compare classifies all 32 leading bytes.** AVX2 has no
   unsigned byte compare, so bytes are biased such that `'0'..'9'` land in
   `[-128, -119]` and every non-digit compares greater. `vpmovmskb` yields a
   delimiter bitmask.
2. **Delimiter positions are extracted in parallel**, not serially: a
   `blsr` chain (1 cycle each) peels the low bits so all four `tzcnt`s are
   independent. The naive alternative — `tzcnt`, shift, repeat — puts
   ~20 serial cycles on the critical path; this takes ~7.
3. **The table index comes straight from the raw positions.** The length
   signature `((x0*3+y0)*10+t0)*3+ty0` algebraically reduces to
   `60*p0 + 27*p1 + 2*p2 + p3 - 158`, so field lengths are never computed.
4. **One 64-byte table entry** (a `vpermd` dword permutation + `vpshufb`
   byte shuffle, fused into a single cache line, generated `consteval`)
   normalizes every digit to a fixed position with zero padding.
5. **One `vpmaddubsw` + `vpmaddwd` chain** converts x, y, and type to
   integers simultaneously. Time finishes in-vector too: real maps top out
   at 7 time digits (a 9-digit time is a 27h+ timestamp), so `vpackusdw` +
   one more `vpmaddwd` pair the mid4/low4 words and a single 32-byte store
   writes `{x, y, type, hs, time, end_time, slider}` — the prefix never
   crosses into GP registers and the int32 overflow branch exists only on
   the never-taken 9-10 digit path. hitSound (1–2 digits; osu! bitflags go
   to 15) is parsed scalar since its length isn't part of the table index.

### Correctness model

The fast path *proves* a line is well-formed before trusting its output,
and returns the work to a lenient scalar parser otherwise:

- each field length is bounds-checked (the mixed-radix index can alias back
  into range when a field is oversized or empty — a fuzzer-found case);
- all four delimiters must be literal commas;
- time must fit `int32`; hitSound must terminate the prefix legally.

The scalar fallback accepts what stable/lazer accept in the wild: negative
and 4+ digit coordinates (Aspire maps), negative times, decimal coordinates
(truncated), `INT32` saturation. A 300k-line fuzzer (25% mutation rate)
asserts the invariant *fast path accepts ⇒ results byte-identical to the
scalar reference*, and the benchmark cross-checks full-corpus checksums
between both paths on every run.

Both paths require `fosu::kBufferPadding` (128) readable zero bytes past the
buffer end — `read_file_padded`/`read_into`/`make_padded` provide this.

## Format coverage

- **[General]/[Editor]/[Metadata]/[Difficulty]** — typed fields for all
  documented keys; AR falls back to OD for pre-v8 files; BOM and CRLF
  handled; format versions v3–v14.
- **[Events]** — background, video, breaks; storyboard commands counted and
  skipped.
- **[TimingPoints]** — full 8-field and legacy 2-field forms, double
  precision time/beatLength.
- **[Colours]** — combo colours.
- **[HitObjects]** — circles, sliders (curve type, control points into a
  shared pool, slides, length, edge sounds/sets), spinners, mania holds.
  Per-object `hitSample`, slider `edgeSounds`/`edgeSets` are stored as raw
  `string_view`s (zero cost; parse on demand).

Not handled: storyboard command bodies, the lazer-era per-segment curve
type syntax (v128+ files), decimal `x,y` on the fast path (falls back).

## Benchmarks

Real-map corpus (`bench/fetch_corpus.sh`): 17 ranked .osu files spanning
format v3–v14 — The Unforgiving (13-diff 2012 marathon album, ~500 timing
points per diff), Freedom Dive, The Big Black, Blue Zenith, and Disco
Prince (the first ranked map, 2007). 0.82 MB, 17,902 hitobjects, 100%
taken by the fast path, zero malformed lines. Best of 9 runs, single
thread. `make bench BENCH_ARGS=bench/corpus` reproduces.

### AMD EPYC Genoa (Zen 4), Ubuntu 24.04, gcc 13.3

Shared-tenancy VM; runs pinned to one core with `taskset`, best-of-reps
across interleaved A/B runs (min-taking is robust to neighbor noise).

| parser | MB/s | ns/object | vs baseline |
|---|---|---|---|
| fosu AVX2 | 910 | 50.1 | 6.7× |
| fosu AVX2, reused `Beatmap` (`parse_into`) | 1170 | 38.9 | 8.7× |
| fosu scalar | 630 | 72.4 | 4.7× |
| getline+sscanf baseline | 135 | 340 | 1× |

The headline metric is the fresh-`parse()` row — every call pays its own
result-object construction, like a caller that keeps the Beatmap. The
`parse_into` row is the secondary mass-parse metric: the same parser
writing into a reused `Beatmap` (vector capacity kept across parses),
which removes the ~25% of the whole parse that goes to malloc,
first-touch page faults, and free. Both rows produce bit-identical
results (whole-corpus checksum cross-checked every run).

Hitobject-prefix microbenchmark (isolates the SIMD technique from parser
overhead): **4.1 ns/line AVX2 vs 20.6 ns/line scalar** — 5×, roughly
15 cycles for a full `x,y,time,type,hitSound` parse.

Homogeneous per-section costs (AVX2 path): circles 13 ns/line, sliders
73 ns/line (200k-line synthetic corpora), timing points 29 ns/line
(measured on the corpus's 6,629 real timing lines, whole-parse).

A lesson learned the hard way: an earlier synthetic corpus (maps 10–50×
larger than typical ranked maps, unrealistically sparse timing points)
showed a *regression* for changes that are a clear win on real maps —
allocation and code-layout effects dominate at unrealistic map sizes.
Benchmark against real beatmaps.

### I/O strategy (why there's no mmap)

Measured end-to-end (open → bytes → parse → release) on the same corpus,
hot page cache, min-of-reps per phase: byte acquisition was 16.5% of
end-to-end with the original stdio path. Three findings now baked into
`io.hpp`:

- `std::make_unique<char[]>` value-initializes — the old read path
  memset the whole buffer to zero and then `fread` over it;
- raw `open`/`fstat`/`read` beats the stdio equivalent by ~2.6 µs/file
  (FILE allocation, seek-based sizing, buffering logic);
- **mmap measured 13% slower end-to-end than `read()`** despite the
  cheapest acquire: per-file `munmap` costs about as much as the entire
  read copy, and first-touch soft faults land inside the parse loop.
  `MAP_POPULATE` just moves the fault cost and keeps the munmap bill.
  Small hot files parsed once sequentially are `read()`'s home turf.

Together: 895 → 951 MB/s end-to-end from the read side alone, on top of
the `parse_into` gain above. The fixed syscall floor (`open`+`fstat`+
`close`) is ~3 µs/file on this VM — the remaining I/O cost is not
attackable without batching (io_uring) or skipping the filesystem, and
production consumers feeding network bytes through `parse(data, size)`
skip it entirely.

### Apple M3, macOS 26 (Rosetta 2 for the x86 rows)

| parser | MB/s | ns/object |
|---|---|---|
| fosu AVX2 (Rosetta 2 translated) | 586 | 71.1 |
| fosu scalar (native arm64) | 681 | 61.1 |
| getline+sscanf baseline (native) | 130 | 320.4 |

Rosetta numbers are included only to show translation cost — its 256-bit
ops decompose to 128-bit NEON, which halves the SIMD advantage (2× vs the
3.9× on real silicon). Don't quote them as x86 performance.

One more honest caveat: whole-file speedup from the SIMD path is
Amdahl-limited (1.35× on Zen 4) — slider parameters, timing-point doubles,
and line handling dominate once prefixes are cheap. The next wins are
listed below.

## Beyond the prefix: SWAR + data-fitting everywhere else

The non-prefix hot paths use branchless SWAR (plain integer ops, portable
to ARM) and format knowledge measured from real ranked maps:

- **Fused [HitObjects] loop**: the section owns its own line iteration, and
  the prefix's single 32-byte load doubles as the newline scan — a bare
  5-field circle line (60% of real hitobject lines) is fully parsed,
  including finding the line end, from one load. No memchr, no separate
  probe.
- **Slider control points** (`|x:y|…`): each coordinate's digit-run length
  comes from an 8-byte nibble-classify + tzcnt, and 1–4 digit values
  convert with two multiplies — no per-digit loop, no length branch
  mispredicts. Points write through a raw cursor with one bounds ensure
  per slider; the trailing edgeSounds/edgeSets/hitSample fields get their
  comma positions from a single 32-byte scan. Signs and 5+ digit Aspire
  values take the general path.
- **Timing points**: a fused section loop parses each line in one pass —
  two 32-byte loads (covering the real-world max line of 39 bytes) serve
  the newline scan, a comma mask, and a digit-classify mask; seven comma
  positions come from a blsr/tzcnt chain; every field converts
  speculatively (integer and decimal beatLength share one branchless
  instruction stream, sign OR'd into the double's sign bit) and a single
  accumulated `valid` predicate — including a whole-line purity check,
  `popcount(nondigits) == commas + dot + minus` — decides. Odd shapes
  (old 2/7-field formats, decimal offsets, >27h timestamps) defer to the
  generic parser. Measured 65 -> 29 ns/line on all 6,629 real timing
  lines of the corpus, integrated.
- **Decimal parsing** (`parse_double`): digit runs are consumed 8 at a
  time with the three-multiply SWAR reduction; >18 significant digits or
  exponents delegate to strtod.
- **Sections and keys dispatch on their first bytes**: `[G` can only be
  `[General]`, `Titl` + one byte distinguishes `Title`/`TitleUnicode`, and
  the matched key's known length locates the value — no full string
  compares, no memchr, no trim.
- **Slider pools** are reserved once per map, at the first slider — eager
  per-map reservation wastes multi-MB allocations on slider-free maps and
  costs more than the reallocations it avoids.

## Future work

- NEON port of the prefix fast path (16-byte window + `shrn` movemask
  equivalent) so the technique runs natively on Apple Silicon / ARM
  servers (the SWAR paths already are portable).
- Slider body is still ~84 ns/line — profile-guided work on the remaining
  branch structure and `Slider` store layout.
- Align the synthetic corpus generator with real-map distributions
  (timing-point density, map sizes) — see the benchmark lesson above.
- Whole-file benchmark against rosu-map and osu!lazer's decoder.
