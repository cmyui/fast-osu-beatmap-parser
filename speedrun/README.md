# speedrun: the parser as a one-shot process

The metric here is different from everything in the top-level README:
**wall time from spawning a process with a map path to that process
exiting**, having parsed the map and written the whole result to stdout.
No state survives between runs. This is what a service pays when it
shells out to parse one beatmap.

```sh
sh speedrun/build.sh                         # -> build/speedrun (Linux x86-64, Zen 4 target)
gcc -O2 speedrun/proc_bench.c -o build/proc_bench
g++ -std=c++20 -O3 -Iinclude -march=x86-64-v3 speedrun/run1.cpp -o build/run1   # the reference program
taskset -c 5 build/proc_bench ref    build/run1     <dir> ref.tsv   # master's output, per file
taskset -c 5 build/proc_bench verify build/speedrun <dir> ref.tsv   # byte-identical?
taskset -c 5 build/proc_bench time   build/speedrun <dir> 3         # spawn-to-exit, best of 3 per file
```

`proc_bench` spawns one process per (file, rep) with `posix_spawn`, times
`posix_spawn` -> `wait4`, sends stdout to `/dev/null` while timing and
hashes it while verifying. `compare <dir> <reps> <limit|0> <bin>...` runs
several binaries in rotating order within every (file, rep), so drift on
a shared VM lands on all of them equally. Children inherit the driver's
CPU affinity.

## What "the same output" means

`dump.hpp` defines a canonical binary serialization of a parsed
`Beatmap`: every parsed value the library exposes plus its path counters
and the pool points no slider references, strings by value, doubles as
raw bit patterns, in a fixed order. `run1.cpp` is the obvious program
written against the library (`read_file_padded`, `parse`, dump, `write`)
and produces the reference. `speedrun` must produce the identical byte
stream for all 10,000 production maps, and does. Derived indices
(`Slider::point_begin`, running slider index) are not part of the format;
a slider's points follow it inline so the stream can be emitted while
parsing.

## Results (Zen 4 VM, mean spawn-to-exit per map, all 10,000 production maps, best of 3)

| program | µs / map (mean) | p50 | notes |
|---|---|---|---|
| `run1`, dynamic libstdc++ (the obvious program) | 1149 | 1106 | 186 page faults, dynamic loader, libstdc++ init |
| `run1`, `-static` | 494 | 449 | glibc's static init alone is ~170 µs |
| **`speedrun`** | **169** | **155** | 5.4 page faults, 6 syscalls |
| empty `_start`/`exit` binary | ~105 | 103 | the exec + exit floor on this VM |

How the speedrun got there (1000-map subset, mean µs per map):
freestanding binary with 4 KB pages 220 -> multi-size THP folios 197 ->
small-copy memcpy, register-held context, folio-aware arena 163 -> no
`.bss`, arena beside the binary, no early flush 157 -> `-O2`, one-page
context, table-driven kv 150.

The parser's own user time on a median (28 KB) map is ~20 µs at IPC 3.0,
i.e. the library's warm speed even though every process starts cold.
Roughly: exec/exit floor 100 µs, I/O and memory setup 14 µs (open 2.2,
fstat 0.6, mmap 1.6, madvise 0.6, read into a fresh folio 8.6), parse
~20 µs, freeing the folio at exit ~6 µs, write 0.4 µs.

## How it gets there

- **Freestanding.** `rt.hpp` is the whole runtime: raw syscalls, `_start`,
  `memcpy`/`memset`/`memmove`/`memcmp` (small sizes via overlapping loads:
  `rep movsb` costs ~30 cycles of startup and the parser copies ~1,500
  short strings per map), a vectorized `memchr`. No libc, no libstdc++,
  no relocations, one RWX `PT_LOAD` (`-Wl,-N`), no `.bss`. Binary ~62 KB,
  so the kernel's 64 KB fault-around maps all of it on the first
  instruction fetch.
- **One arena, streamed output.** The file is read into an anonymous
  arena; the canonical dump is written directly after it (hit-object
  records land straight from the SIMD prefix store; a slider's points are
  written as they are parsed) and sent with one `write`. Everything not
  in file order (header fields, timing points, breaks, colours) goes into
  a trailer. Nothing is materialized as a `Beatmap`.
- **Folio-aware placement.** With multi-size THP in `madvise` mode, an
  anonymous fault takes the largest enabled folio whose aligned block
  fits the VMA and is still empty. The arena is mapped at a 2 MB-aligned
  base plus the folio size that should hold the working set, and stops
  short of the next 2 MB boundary: the first touch gets exactly one
  right-sized folio (a 128 KB folio costs ~6.4 µs against ~1.4 µs per 4 KB
  page on this VM, i.e. 5x cheaper per byte), a larger working set walks
  up through 2x, 4x, ... folios, and no 2 MB folio (65 µs to zero) can be
  chosen. All mutable state lives on the already-resident initial stack
  page, reached through a global register (`r15`), so `.data` is never
  copied-on-write.
- **Table-driven key/value sections.** Four cold `switch` blobs became one
  table and one code path; in a fresh process each kv line otherwise
  executed its own never-before-seen code (~500 cycles per line).
- **Exact numerics.** Same integer mantissa and single division as the
  library; the rare general case (>18 digits, exponent) uses the vendored
  `fast_float` (correctly rounded, like glibc's `strtod`). The production
  corpus never reaches it in a parsed numeric field.

### Machine configuration this depends on

Multi-size THP is off by default. The numbers above need, on the target
host (resets on reboot; `madvise` mode only affects processes that ask):

```sh
for s in 64 128 256; do
  echo madvise > /sys/kernel/mm/transparent_hugepage/hugepages-${s}kB/enabled
done
```

(64/128/256 KB measured marginally better than also enabling 512 KB and
1 MB; 64 KB alone or 128 KB alone are a few µs worse.) Without it the
program still runs; it costs ~25-30 µs more per map.

### Measured and not kept

PGO trained on the corpus (no gain in the cold-process regime, user cycles
+3%), `-Os` (+30% user cycles), `-O3` vs `-O2` (`-O2` is smaller and
faster), `-march=znver4` AVX-512 codegen vs `x86-64-v3` (identical),
software-prefetching the text or the lane-mask table (no effect: the cold
code cost is front-end warm-up, not cache misses), page-fault batching
with `MADV_POPULATE_WRITE` (saves ~15% per page; folios save 80%).
Binary size costs ~3 µs per 70 KB of text and ~3 µs per 270 KB of `.bss`
at exec, which is why both were trimmed. Mapping the input file over the
arena instead of `read()`ing it (`-DINPUT_MMAP`, `MAP_POPULATE`,
padding still zero because the anonymous arena continues after the last
file page) leaves the parser's user cycles unchanged and costs ~8 µs more
per map end to end (file-page mapping, VMA split and teardown outweigh a
28 KB copy), so the input is copied.
