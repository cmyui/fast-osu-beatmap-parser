# One beatmap per process

The executable minimizes wall time from process creation through exit, including
opening and reading the original `.osu`, parsing it and writing the complete
result to stdout. It needs no retained state, cache, sidecar or preprocessing of
the input. Every successful invocation delivers its result.

```sh
make oneshot CXX=g++                    # Linux x86-64, GCC, Zen 4 target
build/fosu_oneshot map.osu > map.fosu
python3 examples/decode_oneshot.py < map.fosu
```

The build uses GCC `-O2 -march=znver4`, a custom entry point and direct Linux
x86-64 syscalls. It links neither libc nor libstdc++, uses no dynamic loader,
and emits a small static ELF with one RWX load segment. Those choices belong
to this constrained executable; they are not applied to the hosted library.
See [performance](../docs/performance.md) for measured results and comparisons.

## Process and memory design

Input and output share one anonymous arena. `read` populates input bytes and
leaves zero padding before the output cursor. Hitobject prefix SIMD stores land
directly in stdout records; slider points and strings follow inline. Metadata,
timing points, breaks and colours are collected for a trailer. Normal files
fit one output write; larger records/trailers can flush or grow storage.

The arena starts near the executable, at a chosen offset within a 2 MiB-aligned
region. This shares upper page-table pages with the binary and makes the first
working-set window eligible for appropriately sized anonymous folios while
excluding a wasteful 2 MiB folio. Larger maps may use further windows. Mutable
context is on the initial stack and addressed through `r15`; ordinary maps
touch no writable global data or overflow mapping.

Rare short timing-point lines can exceed the initial reserve hint. The timing
buffer then grows geometrically without discarding committed points. Breaks,
colours and orphaned slider points beyond inline storage use a lazy overflow
mapping. The process releases all mappings and descriptors when it exits.

Numeric conversion, prefix parsing, metadata tables/defaults and the hitobject
framing loop are shared with the C++ library. Vector allocation, streamed
slider rollback, event loops, timing-section loop and storage and top-level dispatch
retain their representation-specific implementations. The
[vendored fast_float header](third_party/README.md) supplies the executable's
rare general decimal fallback; native callers retain `strtod`.

## Optional host configuration

The fastest measured setup enables 64, 128 and 256 KiB multi-size transparent
huge pages in `madvise` mode. The executable requests `MADV_HUGEPAGE`; it never
changes host settings. On a compatible Linux kernel, an administrator can opt
in with:

```sh
for size in 64 128 256; do
  echo madvise > /sys/kernel/mm/transparent_hugepage/hugepages-${size}kB/enabled
done
```

These settings reset on reboot unless the administrator persists them. They
are host-wide policy for applications requesting huge pages, not private
parser state. The program also works with ordinary pages; the benchmark guide
reports a build without `madvise` separately. File contents are never cached by
the executable, regardless of page size. The benchmark warms the kernel's file
cache before timing, so its numbers describe fresh processes with resident
input, not cold disk/S3 fetches.

## Output contract

`FOSUDMP4` is a little-endian stream containing every logical `Beatmap` field,
all four counters, explicit slider/pool indices and points left by failed
slider lines. Strings are length-prefixed bytes and doubles retain raw IEEE-754
bits. The last eight bytes give the trailer's length, excluding that footer;
consumers locate the trailer from the end and read its object count before
walking the variable-length records. Searching for `TRLR` inside data is not a
valid way to find a record boundary.

The full field order is specified in [dump.hpp](dump.hpp); the independent
[Python decoder](../examples/decode_oneshot.py) demonstrates reconstruction of
metadata, arrays and the complete point pool. A stream consumer should accept
output only after the process exits successfully: an error may follow a
partial write. There is no `--dump` switch; output is always written.

## Limits and errors

This is a Linux/Zen 4 executable for valid editor-emitted beatmaps, with 4 KiB
base pages and the runtime/ELF choices above. It accepts one regular file path
and a blocking stdout. Input must be at most `UINT32_MAX - 128` bytes;
wire lengths and pool indices are 32-bit. It supports up to eight timing
sections, 32,784 breaks, 4,104 colours and 1,048,576 orphaned slider points.
Those limits exceed the evaluation corpus and fail explicitly when exceeded.
The ordinary C++ library does not have these fixed section/overflow limits.

| Exit | Meaning |
|---|---|
| 0 | Complete result written |
| 1 | Input open/size/read or memory-mapping failure |
| 2 | Wrong argument count |
| 3 | Output failure, including nonblocking `EAGAIN` |
| 4 | More than eight timing sections |
| 5 | Runtime assertion failure |
| 6 | Input, break, colour or orphan-point limit exceeded |

Interrupted reads/writes are retried; partial writes are completed. An input
read that ends before the size reported by `fstat` is an error. An ordinary
shell may report a signal instead of an exit code, for example `SIGPIPE` when
a downstream consumer closes its pipe.

## Verification

```sh
python3 tests/test_oneshot.py build/oneshot_reference build/fosu_oneshot
python3 tests/test_oneshot_limits.py build/fosu_oneshot
python3 bench/oneshot_verify.py build/oneshot_reference build/fosu_oneshot /path/to/maps
```

For a release comparison, build the reference against a separately exported
master revision, as shown in the [benchmark guide](../docs/performance.md).
The hosted reference's serializer is a correctness tool, not an optimized
example of delivering library arrays. Its serialization cost should not be
mistaken for the minimum cost of returning a `Beatmap` to an in-process caller.
