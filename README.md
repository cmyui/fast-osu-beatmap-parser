# fast-osu-beatmap-parser (fosu)

A header-only C++20 parser for the `.osu` beatmap format, built around a
table-driven AVX2 fast path for hitobject lines, a zero-copy design,
adaptive shape caching, section-selective parsing, and data-fitting fast
paths that exploit what editor-emitted files guarantee.

**Design goal: performance for valid, editor-emitted beatmaps.** This
project deliberately inverts the usual priority order — robustness against
hand-edited/adversarial files and code readability are explicit non-goals.
Format regularities observed in real ranked maps (integer timing offsets,
key names unique on their first bytes, field-count conventions per format
version) are treated as free information and exploited directly. Structurally
surprising lines defer to generic fallback parsers rather than being
validated up front, so common shapes pay nothing.

fosu is built on an idea and prototype by
[Flamme](https://github.com/infernalfire72) — the project concept and the
original SIMD hitobject parser are Flamme's. It is extended here with
parallel delimiter extraction, compile-time mask generation, structural
validation, and a full-format parser around the technique.

## Usage

```cpp
#include <fosu/parser.hpp>

auto buf = fosu::read_file_padded("map.osu");   // guarantees 128B zero padding
fosu::Beatmap bm = fosu::parse(buf);
// bm.hit_objects, bm.timing_points, bm.sliders, bm.ar, bm.title, ...
// string_view fields point into `buf` — keep it alive.

// Parse-many loop: reuse both buffers and the steady state allocates
// nothing — the reliable mass-parse path on any allocator (see Benchmarks).
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

Callers that need only part of a file can say so — unwanted sections are
skipped with a single memchr jump and parsing stops once every requested
section has been consumed. `[Difficulty]` lives in the first ~2KB of a
file whose remaining ~98% is hit objects, timing and events, so this is
work *elimination*, not acceleration: measured 0.39us vs 29.6us per map
across the 10k-map production corpus (~75x) for a difficulty-only
caller.

```cpp
// Just the difficulty attributes (OD/AR/CS/HP), ~75x cheaper:
fosu::Beatmap d = fosu::parse(buf, {.sections = fosu::kSectionDifficulty});
// Metadata + difficulty for a listing page:
fosu::Beatmap m = fosu::parse(
    buf, {.sections = fosu::kSectionMetadata | fosu::kSectionDifficulty});
```

```sh
make test          # native + x86-64-v3 test suites (Rosetta on Apple Silicon)
make bench         # synthetic corpus benchmark
make bench BENCH_ARGS=/path/to/osu/Songs/dir   # real .osu files
make bench-pgo BENCH_ARGS=bench/corpus         # profile-guided build (+2-3%)
make coldstart BENCH_ARGS=bench/corpus-large COLD_CPU=5   # fresh-process single-map latency
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

All numbers come from real ranked beatmaps. Three corpora, three roles:

| corpus | files | size | objects | checksum | role |
|---|---|---|---|---|---|
| historical (`bench/fetch_corpus.sh`) | 17 | 0.82 MB | 17,902 | `e7f15a16a8370431` | continuity anchor — every number ever reported |
| popular (`bench/fetch_corpus_large.sh`) | 167 | 3.86 MB | 70,732 | `c16c5c00b51eaa2e` | most-played ranked sets, census + validation |
| production | 10,000 | 403 MB | 8,070,196 | `31999b0f474ecd4a` | the 10,000 most-played ranked/approved maps on a private server (contains play data, not distributed; rebuilt September 2026 from its object store, so the July set's `272b2f66dcd5f90a` is superseded) — the adjudicator |

The historical corpus: The Unforgiving (13-diff 2012 marathon album,
~500 timing points per diff), Freedom Dive, The Big Black, Blue Zenith,
and Disco Prince (the first ranked map, 2007); 100% fast-path, zero
malformed lines. Best of 9 runs, single thread; `make bench
BENCH_ARGS=bench/corpus` reproduces. The production corpus parses at
1517 MB/s fresh / 1563 reuse / 924 scalar even though 403 MB cannot fit
in L3 — the parser streams from DRAM without becoming memory-bound.

### AMD EPYC Genoa (Zen 4), Ubuntu 24.04, gcc 13.3

Shared-tenancy VM; runs pinned to one core with `taskset`, best-of-reps
across interleaved A/B runs (min-taking is robust to neighbor noise).

| parser | MB/s | ns/object | vs baseline |
|---|---|---|---|
| fosu AVX2 | 1408 | 32.4 | 10.4× |
| fosu AVX2, reused `Beatmap` (`parse_into`) | 1421 | 32.1 | 10.5× |
| fosu scalar | 764 | 59.7 | 5.6× |
| getline+sscanf baseline | 136 | 336 | 1× |

(September 2026, current master, the default Linux `make bench` flags
with gcc 13.3. PGO adds ~5% on top — see Build notes. The popular
corpus, 167 files, parses at 1627 MB/s fresh / 1666 MB/s reuse / 972
MB/s scalar on the same setup.)

The headline metric is the fresh-`parse()` row — every call pays its own
result-object construction, like a caller that keeps the Beatmap. The
`parse_into` row is the secondary mass-parse metric: the same parser
writing into a reused `Beatmap` (vector capacity kept across parses).
Both rows produce bit-identical results (whole-corpus checksum
cross-checked every run).

The two rows used to sit 32% apart (930 vs 1230 MB/s), and the gap was
profiled to be entirely glibc returning pool pages to the kernel
between parses and the kernel re-zeroing them on the next parse (~5.6
minor faults per parse; allocator bookkeeping itself measured ~0.2%).
It then closed by accident. The census-driven reserve fix (`/24` →
`/16`) pushed the largest per-parse allocation — the biggest corpus
file's hit_objects vector, ~195 KB — past glibc's 128 KB mmap
threshold (under `/24` it was ~130 KB: just below). Freeing an mmapped
chunk trips glibc's *dynamic threshold adaptation*: the mmap threshold
ratchets up to that chunk's size and the trim threshold doubles it,
after which every parse's pools come from the heap top and sit below
the trim threshold when freed. The give-back simply stops. Verified in
both directions: pinning the thresholds with `GLIBC_TUNABLES` (which
disables the ratchet) reopens the gap (~745 vs ~1175 MB/s), and the
pre-`/16` tree rebuilt today measures ~900 MB/s fresh, interleaved
against current master on the same core minutes apart.

Fresh-parse workloads shouldn't rely on the accident — whether the
ratchet fires depends on map sizes and the allocator. A process can
set the same policy deliberately (it is process-global application
policy, which is why fosu never sets it itself), at the cost of
retaining high-water-mark memory:

```cpp
mallopt(M_MMAP_THRESHOLD, 64 << 20);   // large pools stay on the heap
mallopt(M_TRIM_THRESHOLD, INT_MAX);    // the heap never shrinks
// fresh parse() measured 900 -> 1236 MB/s with these — equal to reuse
// (measured before the ratchet effect made this the default outcome).
```

— or, scoped and allocator-independent: keep a `Beatmap` alive and use
`parse_into`, which pins one object's pools regardless of malloc
policy. That is why reuse stays the recommended mass-parse path even
now that the fresh row matches it here. Non-glibc allocators
(jemalloc/tcmalloc/mimalloc) return pages lazily and shrink the gap on
their own.

Hitobject-prefix microbenchmark (isolates the SIMD technique from parser
overhead): **4.1 ns/line AVX2 vs 20.5 ns/line scalar** — 5×, roughly
15 cycles for a full `x,y,time,type,hitSound` parse.

Per-line-class budgets, measured on homogeneous workloads built from
the production corpus's real lines (steady-state reuse, hardware
counters):

| line class | bytes/line | cycles/line | instructions/line | IPC |
|---|---|---|---|---|
| circles | 27.8 | 40.4 | 152 | 3.76 |
| sliders | 67.2 | 198.8 | 725 | 3.65 |
| timing points | 32.5 | ~106 (with shape cache) | ~354 | 3.4 |

All three classes run at IPC 3.4–3.8: the parser is throughput-bound,
not latency- or mispredict-bound. (Budgets measured July 2026; the
slider-params path has since lost roughly a quarter of its cost — see
the ablation list — so the sliders row is an upper bound today.) A
slider deep-dive of *conversion kernels* (SIMD point walks, fused
speculative tail, pool cursor) measured every variant within noise; what
did pay was the *bookkeeping* around them: a Slider filled in place
instead of copied in, a point pool that stops value-initializing what
it is about to overwrite, and one 16-byte classify per control-point
pair instead of two 8-byte ones.

### What each optimization is worth (ablation audit)

Each fast path toggled off individually against the full build (same
interleaved-rounds methodology, popular corpus). Marginal value on the
real corpus: hit_objects reserve estimate **+27%**, deferred slider
pool reserve **+16%**, one-pass timing parser **+7.8%**, timing fused
section **+4.5%**, hitobjects fused section **+4.3%** (measured with the
SIMD prefix retained per-line, so this is fusion itself), SWAR
coordinate path **+4.2%** (all July 2026); slider point-pair classify
**+3.1%**, in-place Slider + no-fill point pool + direct extras
**~+2.8%**, 64-byte newline window before memchr **+2.6%**, fused
[Events] loop **+2.1%**, masked slider-length parse **+1.8%**,
straight-line shape-cache insert **+1.4%** (September 2026; together
1416 → 1632 MB/s fresh). Two pieces measured ~zero: a 32-byte SIMD
newline probe in the main line loop (deleted — the fused section loops
had eroded its value to nothing) and the slider-extras 32-byte scan
(kept; its ceiling remained inside measurement noise at every corpus
size). One measured slightly negative and was reverted: hashing the
nondigit mask into the timing shape-cache slot (the alternating
inherited/uninherited collision it guards against does not occur often
enough to pay for the extra multiply).

### The large corpus (popular maps)

`bench/fetch_corpus_large.sh` fetches the second corpus: 167 .osu files
from the 43 most-played ranked mapsets (all-time playcount via mirror
APIs, cross-checked between osu.direct and nerinyan; top sets per mode;
plus the most-played recently-ranked sets for modern file shapes).
3.86 MB, 70,732 hitobjects, corpus checksum `c16c5c00b51eaa2e`.

A structural census — re-run at 10,011-map scale on the production
corpus — validated every data-fitting assumption against the real
population: slider coordinates exceed 3 digits in 33 of 12.3 million
(0.62% signed), times top out at 7 digits (the fast path allows 8),
beat_length fractions exceed 13 digits in 10 of 1.16 million lines, and
the pool reserve estimates never under-reserve across all 10,011 files.
The one census-found defect: the hit_objects `/24` bytes-per-object
divisor under-reserved on old Big Black diffs with bare 15-byte lines,
which is why the divisor is now `/16` (the observed minimum line +
newline; `/24` would have under-reserved on 81 of the 10k files).

A lesson learned the hard way: an earlier synthetic corpus (maps 10–50×
larger than typical ranked maps, unrealistically sparse timing points)
showed a *regression* for changes that are a clear win on real maps —
allocation and code-layout effects dominate at unrealistic map sizes.
Benchmark against real beatmaps.

### Build notes (from the disassembly audit)

- **gcc over clang**: gcc 13 measures ~4-6% faster than clang 18 on
  this code (re-verified July 2026: fresh best-of ~1210 vs ~1160 MB/s
  across interleaved rounds) — and clang with its own PGO did not close
  the gap when audited, so the difference is codegen quality on
  intrinsics-heavy code, not branch-layout luck. The Makefile's `CXX`
  default is clang++; the numbers above are `CXX=g++` builds.
- **Tuning flags are worth +4-6% combined** (now the Linux default in
  the Makefile): `-mtune=znver4` alone is +3-5% — pure Zen 4 instruction
  scheduling, same portable x86-64-v3 ISA — plus `-fno-plt` and
  `-fno-stack-protector` (Ubuntu enables stack-protector-strong by
  default). `-march=native` measured no better than `-mtune` alone: the
  extra AVX-512 ISA buys the compiler nothing here.
- **PGO is worth ~5% on top** (and +14% on the scalar build):
  `make bench-pgo BENCH_ARGS=bench/corpus` trains on the corpus and
  rebuilds with measured branch probabilities. Full stack (tuning + PGO)
  peaks at ~1325 MB/s fresh / ~1355 MB/s reuse.
  Header-only libraries can't ship a profile; consumers should train on
  their own workload.
- Two audit findings are baked into the source: `HitObject` construction
  skips `emplace_back()`'s 48-byte zero-fill (the parser writes every
  field on all paths), and the timing-point fast parser is force-inlined
  into its section loop (gcc otherwise leaves a per-line call with
  per-call constant rebuilds).

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

### Cold start: one beatmap in a fresh process

The loop benchmarks above run warm: code, branch predictors and heap
pages are all primed by the previous iteration. A caller that parses a
single map in a fresh process sees something else. `bench/coldstart.cpp`
reads one file, times exactly one `parse()`, then times eight more in
the same process; `bench/coldstart.sh` runs it once per file and
aggregates (`make coldstart BENCH_ARGS=bench/corpus-large COLD_CPU=5`;
`FOSU_PERF=1` adds hardware counters via `perf_event_open`). On the
popular corpus, Zen 4 VM:

| | µs/file | MB/s | minor faults/file |
|---|---|---|---|
| first parse in a fresh process | 41 | 560 | 12.3 |
| best of 8 further parses, same process | 13 | 1650 | 0 |

(On the production corpus, whose files average 40 KB: 64.6 µs/file cold
at 19.4 faults vs 22.0 µs warm, 2.9×.) Three times slower, and the gap
is not the parser's code. Controlled
experiments (`FOSU_COLD_MODE=prefault,warmcode,ptable,pcode`, measured
before the September changes, 46.8 µs total) decompose it:

| component | µs | how it was isolated |
|---|---|---|
| warm compute | ~14 | the warm row |
| heap provisioning: ~12 first-touch page faults + `brk`/`mmap` | ~18.5 | `prefault` (pre-touched, never-trimmed heap) removes it |
| cold code (icache, uop cache, BTB) | ~4.5 | `warmcode` (parse a tiny map first) removes it |
| branch-predictor training on the map's own data | ~7 | remains with both |

A first-touch anonymous page costs **~1.35 µs on this VM** (measured in
isolation; `MADV_POPULATE_WRITE` is 1.12 µs/page, a bare `mmap` 1.5 µs,
`munmap` 0.25–0.8 µs/page), so each 4 KB of result written costs about
as much as parsing 2 KB of input. The result is bigger than the input:
32.7 KB of vectors for a 22.5 KB average file (1.45×), 10.4 pages
first-touched per parse — 5.5 hit_objects, 2.6 sliders, 1.1 points,
1.3 timing. Software-prefetching the lane-mask table or the text
segment changed nothing (the cold-code cost is front-end warm-up, not
cache misses), populate saves only the trap (~15%/page), and a 2 MB
huge page zeroes far more than it saves. The levers that remain are
structural and change the public types, so they are not taken here:
one packed arena for all four vectors would first-touch 8.7 pages/file
(rounding waste of four separate regions), compact structs (48→32-byte
`HitObject`, 56→40 `Slider`, 40→24 `TimingPoint`) 8.0, both 6.2 —
about 14% of the cold time. Multi-size THP (64 KB anonymous folios,
`hugepages-64kB/enabled=madvise`, off by default) would cut the
per-page cost ~10× for an aligned arena, but that is host
configuration. The `read()` that precedes the parse pays the same bill
for the file buffer: ~20 µs for a 22.5 KB file, 12 faults plus four
syscalls.

### One map per process: `speedrun/`

For the "spawn a process, parse one map, exit" case the parse call is a
small part of the bill: the obvious program (dynamic libstdc++, `parse`,
dump to stdout) takes ~1149 µs per map on the production corpus, of
which ~35 µs is parsing. `speedrun/` is the same parser as a
freestanding static binary — no libc, one arena, output streamed from
the SIMD stores, multi-size THP folios — at **169 µs per map** against
an exec+exit floor of ~105 µs, producing byte-identical output on all
10,000 maps. Design, harness and the measured ladder are in
[speedrun/README.md](speedrun/README.md).

### Apple M3, macOS 26 (Rosetta 2 for the x86 rows)

| parser | MB/s | ns/object |
|---|---|---|
| fosu AVX2 (Rosetta 2 translated) | 629 | 72.5 |
| fosu AVX2, reused `Beatmap` (Rosetta 2) | 656 | 69.5 |
| fosu scalar (native arm64) | 509 | 89.6 |
| getline+sscanf baseline (native) | 134 | 339.5 |

Rosetta numbers are included only to show translation cost (~2.2× vs the
same binary on real Zen 4 silicon; 256-bit ops decompose to 128-bit
NEON). The translated SIMD build now beats the native scalar build even
through that penalty — which is also the case for the NEON port listed
under future work.

One more honest caveat: whole-file speedup from the SIMD path is
Amdahl-limited (~1.7× on Zen 4) — slider parameters, timing-point doubles,
and line handling dominate once prefixes are cheap.

## Beyond the prefix: SWAR + data-fitting everywhere else

The non-prefix hot paths use branchless SWAR (plain integer ops, portable
to ARM) and format knowledge measured from real ranked maps:

- **Fused [HitObjects] loop**: the section owns its own line iteration, and
  the prefix's single 32-byte load doubles as the newline scan — a bare
  5-field circle line (60% of real hitobject lines) is fully parsed,
  including finding the line end, from one load. Longer lines get a
  second 32-byte compare (64 bytes cover 82% of hitobject lines in the
  production census) before anything calls memchr.
- **Slider control points** (`|x:y|…`): one 16-byte load classifies a
  whole `|x:y` pair — the non-digit mask gives both digit counts, a ':'
  mask validates the separator with no dependent byte load — and 1–4
  digit values convert with two multiplies. Points write through a raw
  cursor into a pool whose element type has a no-op default constructor,
  so the per-slider resize never value-initializes what is about to be
  overwritten; the `Slider` itself is emplaced and filled in place rather
  than built on the stack and copied. Length is parsed from one 32-byte
  classify with the same integer mantissa and single division
  `parse_double` uses (bit-identical); the trailing
  edgeSounds/edgeSets/hitSample fields get their comma positions from a
  single 32-byte scan. Signs, 5+ digit Aspire values and exotic numbers
  take the general path.
- **Fused [Events] loop**: storyboard command lines are indented and make
  up ~12% of all lines in the popular corpus; the section loop counts
  and skips them on their first byte and finds every line end with
  vector compares instead of a memchr call per line.
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
  lines of the corpus, integrated. On top of that sits an adaptive
  **shape cache**: a section's accepted lines deposit their exact
  (comma mask, nondigit mask, length) layout in a 16-entry table, and a
  matching later line replays through cached geometry — validity by key
  equality, the six small tail fields converted together by one cached
  shuffle + maddubs. The 10k census says the top 8 exact shapes cover a
  median 98.1% of a file's timing lines; measured +6.8% on timing
  (~29 -> ~27 ns/line). Timing sections are the one place this pays:
  they are shape-homogeneous, so the hit/miss branch predicts (see the
  entropy note below).
- **Section-selective parsing**: unwanted sections skip via one memchr
  jump and parsing stops once everything requested has been seen —
  0.39us vs 29.6us per map (~75x) for a difficulty-only caller on the
  production corpus.
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

## Why the hot loop's branches stay (the entropy-payment result)

The deepest finding of the project. A circle line costs 152 instructions
of which only ~25 convert digits — the rest is dispatch, validation and
bookkeeping — so an instruction-count attack looks obvious. Three
architectures were built, fuzz-pinned to bit-identical output, and
measured against the fused loop:

| how the circle/slider entropy is paid | result |
|---|---|
| predicted branches, in place (the shipped fused single-pass loop) | **champion** |
| one hit/miss branch + per-shape replay (single-pass shape cache) | −3–5% (+8.3% branch misses: the branch is a coin flip in kind-interleaved files) |
| branchless binning + homogeneous per-shape kernels (two-pass) | −22% (branch misses *fell* 23% and it lost anyway: records, a second pass and three traversals cost more than the branches did) |

The per-shape circle kernel itself measures **+37%** on a pure-circle
stream — the savings are real — but every delivery mechanism costs more
than it earns, because a beatmap's circle/slider interleaving is
genuine information that must be paid for somewhere, and the predictor
sitting on one well-placed branch with full local history is the
cheapest known way to pay it. Corollaries measured along the way:
replacing four never-failing byte compares with mask arithmetic lost
1.3% (predicted branches are free; arithmetic is not), and `[[likely]]`
annotations lost 5% to code-layout perturbation. The floor claim is
therefore not "minimal instructions" but **minimal expected cost under
the real branch distribution** — and the timing shape cache is the
exception that proves it: timing sections are shape-homogeneous, so
their hit/miss branch predicts, and the same idea that loses 3–5% on
hit objects wins 6.8% there.

## Future work

- NEON port of the prefix fast path (16-byte window + `shrn` movemask
  equivalent) so the technique runs natively on Apple Silicon / ARM
  servers (the SWAR paths already are portable).
- Whole-file benchmark against rosu-map and osu!lazer's decoder.
- PGO train/test split on the popular corpus (train on half, evaluate on
  the held-out half) to check profile generalization.
- Workload-level throughput: a file-level parallel parse driver
  (embarrassingly parallel; per-thread `parse_into` state), and a
  parse-once binary `Beatmap` cache for repeat workloads (recalc
  pipelines re-parse the same maps every rework).

Closed with measurements, so nobody re-treads them: slider *conversion
kernels* are at their floor (SIMD point-walk kernels, fused speculative
tail: all within noise — the September gains came from bookkeeping, not
conversion); line pipelining −19%; the single-pass
hitobject shape cache −3–5% and the branchless binned two-pass −22%
(the entropy result above); comma-mask folding −1.3% and mask-derived
hitSound corpus-dependent (both replaced free predicted branches with
paid arithmetic); `[[likely]]` hints −5%; `-fwhole-program`/static
linkage neutral to −6% (gcc's inlining judgment was already right);
splat-constant hoisting mechanically successful but a wash (the
rematerialization executes in OOO slack); mmap −13%; `-march=native` no
better than `-mtune`; `[Events]` batch-skipping subsumed and
closed: for callers that don't want the section, selective parsing
skips it entirely (the idea, generalized); for full parses that keep
breaks/backgrounds, the residual in-section skim was declined by gate
measurement — events content already parses at 3.7 GB/s, ~2.8% of total
cycles, a ~+1.4% ceiling.
