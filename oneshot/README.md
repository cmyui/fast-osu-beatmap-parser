# One beatmap per process

The executable is a process-level benchmark and demonstration of the library's
parsing engine; the library, C ABI and Python package are the primary products. It
minimizes wall time from process creation through exit, including
opening and reading the original `.osu`, parsing it and writing the complete
result to stdout. It needs no retained state, cache, sidecar or preprocessing of
the input. Every successful invocation delivers its result.

```sh
make oneshot CXX=g++                    # Linux x86-64, GCC, Zen 4 target
build/release-avx2-bundled/fosu_oneshot map.osu > map.fosu
python3 oneshot/decode.py < map.fosu
```

The build uses GCC `-O2 -march=znver4`, a custom entry point and direct Linux
x86-64 syscalls. It links neither libc nor libstdc++, uses no dynamic loader,
and emits a small static ELF with one RWX load segment. Those choices belong
to this constrained executable; they are not applied to the hosted library.
It also disables stack canaries, fortification, PIE and RELRO. Applications
parsing untrusted input should use the C++ library with appropriate compiler
protections or the hardened C ABI/Python products.
See [performance](../docs/performance.md) for measured results and comparisons.

## Process and memory design

The executable calls the same `Parser::parse_file` as the hosted library.
Parser owns the padded input and contiguous Beatmap arrays in one working
arena. The compile-time AVX2 engine fills those arrays, then a bounded 64 KiB
output buffer serializes the result to stdout. The process releases mappings
and descriptors when it exits.

The freestanding runtime implements the small POSIX surface used by Parser
and the arena OS layer using direct syscalls. It does not duplicate format
rules, section loops or beatmap storage. The
[vendored fast_float header](../src/fosu/engine/third_party/fast_float.md) supplies
the bounded, locale-independent numeric fallback.

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
shows how to omit the advice for comparison. File contents are never cached by
the executable, regardless of page size. The benchmark warms the kernel's file
cache before timing, so its numbers describe fresh processes with resident
input, not cold disk/S3 fetches.

## Output contract

`FOSUDMP5` is a little-endian stream containing every logical `Beatmap` field,
all four counters, explicit slider/pool indices and points left by failed
slider lines. Strings are length-prefixed bytes and doubles retain raw IEEE-754
bits, including object and break timestamps. The last eight bytes give the
trailer's length, excluding that footer;
consumers locate the trailer from the end and read its object count before
walking the variable-length records. Searching for `TRLR` inside data is not a
valid way to find a record boundary.

The field order is specified by the shared serializer, [canonical_dump.h](../tests/support/canonical_dump.h); the independent
[Python decoder](../oneshot/decode.py) demonstrates reconstruction of
metadata, arrays and the complete point pool. A stream consumer should accept
output only after the process exits successfully: an error may follow a
partial write. There is no `--dump` switch; output is always written.

## Limits and errors

This is a Linux/Zen 4 executable for legacy beatmaps, with 4 KiB
base pages and the runtime/ELF choices above. It accepts one regular file path
and a blocking stdout. Input must be at most 64 MiB;
wire lengths and pool indices are 32-bit. Array capacities follow the same
input-derived bounds as the library; there are no separate fixed section or
record-count limits.
The shared [parsing contract](../docs/compatibility.md) defines numeric bounds,
malformed-record handling and the consumer's gameplay responsibilities.

| Exit | Meaning |
|---|---|
| 0 | Complete result written |
| 1 | Input open/size/read or memory-mapping failure |
| 2 | Wrong argument count |
| 3 | Output failure, including nonblocking `EAGAIN` |
| 5 | Runtime assertion failure |
| 6 | Input exceeds 64 MiB |

Interrupted reads/writes are retried; partial writes are completed. An input
read that ends before the size reported by `fstat` is an error. An ordinary
shell may report a signal instead of an exit code, for example `SIGPIPE` when
a downstream consumer closes its pipe.

## Verification

```sh
make CXX=g++ test-oneshot references
python3 tests/verify_stream.py build/release-avx2-bundled/reference_native build/release-avx2-bundled/fosu_oneshot /path/to/maps
```

For a release comparison, build the reference against a separately exported
master revision, as shown in the [benchmark guide](../docs/performance.md).
The reference and executable use the same serializer over the same Beatmap.
Their complete streams are compared, including counters and orphaned points;
independent reference tests check parsing semantics. Serialization cost should
not be mistaken for the cost of returning a Beatmap to an in-process caller.
