# Single-beatmap executable

This target optimizes one fresh Linux x86-64 process that opens an original
`.osu` file, materializes all parsed values, and exits. It targets Zen 4 with
AVX-512 and 4 KiB pages. Build with GCC 13 on Linux:

```sh
make oneshot
./build/fosu_oneshot /path/to/map.osu

# Optional compiler profiling; every training invocation is a fresh process.
taskset -c 5 make oneshot-pgo CORPUS=/path/to/corpus
./build/fosu_oneshot_pgo /path/to/map.osu
```

The normal invocation produces no stdout. The full result is materialized and
kept observable to the compiler before exit. `--dump` emits every parsed value
in a binary verification format; serialization is outside the default workload.
The entry point is the place to add an in-process consumer if needed.

## Execution model

- A small assembly entry point calls `main` and invokes Linux `exit` directly.
  The binary has no dynamic loader or libc/libstdc++ startup.
- The original file is mapped read-only. Its partial last page supplies zero
  padding; an anonymous page replaces the reserved page past EOF when required.
- Allocations advance through a 128 MiB virtual arena. Only touched pages become
  resident. Deallocation does nothing: process exit reclaims the arena, input
  mapping, and descriptor. No parse result or mutable arena survives a run.
- `FOSU_ONESHOT_COMPACT` selects 24-byte hitobjects and 40-byte sliders, reducing
  output page faults. The ordinary header-only API retains its default layout.
  Do not mix these layouts in one program.
- AVX-512 scans memory spans in the minimal runtime. The parser keeps its AVX2
  prefix kernel. Long decimal fallbacks use the vendored fast_float header.

This is a constrained executable, not a replacement C/C++ runtime. It has no
TLS, exceptions, global-constructor support, threads, or general libc facility.
The runtime's decimal conversion supports the decimal/exponent shapes reached
by this parser; it is not a general `strtod` implementation.

Compact output requires hitobject coordinates to fit signed 16-bit integers,
type/hitsound to fit unsigned bytes, fewer than 65,536 slider entries, and object
sample/slider edge strings to fit 16-bit lengths. String addresses must be below
2^48. Control-point coordinates, times, metadata, timing-point values, and all
floating-point calculations retain their original widths. This target assumes
these numeric bounds; it is not an adversarial-input validator. Arena exhaustion
exits with status 71; invalid compact string/index representation exits with 70.
Input/argument errors return nonzero.

## Correctness

Build the reference consumer against the exact baseline headers. For example,
after exporting master’s `include` directory into `build/master/include`:

```sh
g++ -std=c++20 -O3 -march=x86-64-v3 -mtune=znver4 \
  -fno-plt -fno-stack-protector -Ibuild/master/include \
  bench/oneshot_reference.cpp -o build/master_reference

python3 tests/test_oneshot.py build/master_reference \
  build/fosu_oneshot build/fosu_oneshot_pgo
python3 bench/oneshot_verify.py build/master_reference \
  build/fosu_oneshot_pgo /path/to/corpus --expected-files 10000
```

The verifier compares bytes directly for every file, including float bits,
string contents, pool indices, unused pool entries, and every statistic.
Addresses, vector capacities, and object padding are excluded. The ordinary
benchmark’s checksum is narrower and is not this target’s correctness gate.

## Timing

```sh
taskset -c 5 build/oneshot_process /path/to/corpus 0 3 \
  build/master_reference build/fosu_oneshot build/fosu_oneshot_pgo \
  > build/oneshot-results.csv
python3 bench/oneshot_summary.py build/oneshot-results.csv
```

The driver times `posix_spawn` through `wait4`, including process startup, input
acquisition, parsing, and exit. Each invocation parses one file. It reads input
outside the timed region first to define a resident-input, cold-process workload;
it does not clear the host page cache or cache parsed results. CPU affinity is
inherited from `taskset`. Executable order rotates within each file/repetition.
The limit `0` uses the entire corpus; a positive limit samples evenly across it.

The summary reports both each file's minimum across repetitions and all observed
runs. Best-of-repetition figures estimate performance with less scheduling noise;
they are not average experienced latency. Keep raw samples when comparing small
changes on a shared VM. PGO trains on every fifth sorted file; the full corpus
remains the validation and timing set. Profiles are compiler branch counts, not
cached input or decoded values.

### Measured process lifetime

September 2026, GCC 13.3, Ubuntu 24.04/glibc 2.39, Linux 6.8, shared Zen 4 VM.
One CPU, rotating paired order, 10,000 files totaling 402,593,897 bytes, three
fresh processes per file and executable. The baseline is the reference consumer
built against master `4100573` with its default Linux benchmark flags.

| Executable | Mean of per-file minima | Mean of all runs | p99 of per-file minima | Minor faults/run at minimum |
|---|---:|---:|---:|---:|
| Master, ordinary dynamic linking | 1046.52 µs | 1098.78 µs | 1328.67 µs | 155.7 |
| Standalone, no PGO | 186.22 µs | 203.79 µs | 330.26 µs | 21.9 |
| Standalone, PGO | **177.76 µs** | **193.07 µs** | **321.94 µs** | **21.9** |
| Empty minimal executable, reference floor | 95.58 µs | 105.15 µs | 113.62 µs | 3.5 |

The PGO row is 5.89× faster by per-file minima and 5.69× by observed mean.
The baseline's canonical output matched the PGO executable byte-for-byte on all
10,000 files, including all counters and raw floating-point bits. The aggregate
verification SHA-256 was
`7b959fecd18d90cd7d13e3f9c148c0173bc7a2afb73f0d0a50e94914bf8ba9c3`.

These are full process timings with resident input, not storage-cache-miss
latencies or parse-only throughput. The empty executable is a measured point
of comparison, not a proof of a universal lower bound. No global-optimality
claim follows from these measurements.
