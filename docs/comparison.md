# Parser comparison

This comparison asks: **how long does a normal public API take to produce a
decoded beatmap?** It covers Python, Rust, C#, JavaScript, and FOSU's native C++
interface. It is not a claim that every library produces the same model or
supports the same operations.

## Scope

The selection covers the official osu! decoder, established general-purpose
libraries across these ecosystems, and the widely used rosu PP bindings.
Versions are pinned published packages rather than unreviewed development
branches. This is a broad selection, not an exhaustive ranking of every parser.

| Library | Version | Entry point and relevant work |
|---|---|---|
| [FOSU](https://github.com/cmyui/fast-osu-beatmap-parser) | C++ and Python: `d31cddf` | Python `parse` / `parse_file` return detached dataclasses and lists; C++ `Parser` returns native records. No slider geometry or PP calculation. |
| [slider](https://github.com/llllllllll/slider) | 0.8.4 | `Beatmap.parse` / `from_path`. Eager Python objects and slider-curve construction. Public object access uses `stacking=False`; no difficulty or mods requested. |
| [rosu-pp-py](https://github.com/MaxOhn/rosu-pp-py) | 4.0.2 | `Beatmap(bytes=...)` / `Beatmap(path=...)`. Native PP-oriented model, not a full document representation. No PP/difficulty calculation requested. |
| [OsuPyParser](https://github.com/lenforiee/osupyparser) | 1.0.7 | `OsuFile(path).parse_file()`. Eager Python objects, file hash and derived statistics. Published API is file-only. |
| [pyttanko](https://github.com/Francesco149/pyttanko) | 2.1.0 | `parser.map(text_stream)`. Historical pure-Python, osu!standard-only PP model; no difficulty or PP calculation requested. |
| [rosu-map](https://github.com/MaxOhn/rosu-map) | 0.2.1 | `from_bytes::<Beatmap>`. General-purpose legacy document decoding. |
| [rosu-pp](https://github.com/MaxOhn/rosu-pp) | 4.0.1 | `Beatmap::from_bytes`. PP-oriented model; no difficulty calculation. |
| [osu!lazer](https://github.com/ppy/osu) | 2026.730.0 | Official `Decoder.GetDecoder<Beatmap>(reader).Decode(reader)`. Defaults, samples, control points and stable object sorting; all four rulesets registered. No separate storyboard, star/PP or rendering calls. |
| [OsuParsers](https://github.com/mrflashstudio/OsuParsers) | 1.7.2 | `BeatmapDecoder.Decode(lines)` using timed `StreamReader.ReadLine`. Rich C# objects and storyboard decoding. Its stream overload mishandles CRLF on Linux. |
| [Coosu](https://github.com/Coosu/Coosu) | 2.5.1 | `OsuFile.ReadFromStream(stream)`. Typed C# sections and normal post-deserialization processing. |
| [osu-parsers](https://github.com/kionell/osu-parsers) | 4.1.7 | `BeatmapDecoder.decodeFromBuffer(buffer, false)`. Rich TypeScript model; optional storyboard decoder disabled. `osu-classes` 3.1.0. |
| [osu-parser](https://github.com/nojhamster/osu-parser) | 0.3.3 | `parseContent(string)`. Legacy JavaScript API automatically derives slider endpoints, duration and max combo. Especially different work from FOSU. |

Replay-only libraries such as `osrparse` are not beatmap decoders. Historical
PP implementations/ports such as `oppai-ng` and `peace-performance-python` are
not measured here. `oppai-ng`'s documented `ezpp` entry point also calculates PP,
so timing it against parse-only calls would be misleading; this comparison
does not reach into its internal parsing functions. In particular, do not interpret this table
as a PP-calculator ranking: FOSU does not calculate PP.

## Measurement

Measured on 2026-09-08 UTC, using the fixed 10,000-map corpus: **402,593,897
bytes**, selected by Akatsuki playcount from ranked/approved maps. This is a
popularity-weighted selection, not a representative sample of all modes,
unranked maps or pathological input. Each selected map has equal weight in the
averages; playcount does not multiply its contribution.

The host is an eight-vCPU, 16 GiB shared-tenancy AMD EPYC Genoa (Zen 4) VM, Ubuntu 24.04,
kernel 6.8 and glibc 2.39. Workers were pinned to logical CPU 3. Other resident
server workloads were not stopped; these are not isolated bare-metal results.
No benchmark builds or other FOSU benchmarks ran concurrently with the timed
sweep.

- CPython 3.12.3; published Python wheels where available. FOSU was built from
  source with its bundled C++ runtime and selected via `FOSU_BACKEND`.
- GCC 13.3, `-O3`. AVX2 C++: `-march=x86-64-v3 -mtune=znver4`; scalar:
  `-march=x86-64 -DFOSU_DISABLE_SIMD`. Both retain stack protection/fortification.
- Rust 1.98.1 release build, thin LTO, one codegen unit and
  `-C target-cpu=x86-64-v3`.
- Node 20.20.2 / npm 10.8.2; .NET SDK 10.0.400 / Release, default runtime JIT/GC.

### Python headline: separate-process batches

The Python headline uses the 9,758-map common cohort established by the full
sweep below. Each library/API gets a **fresh process for each pass**, preloads
the inputs, warms on 64 evenly spaced maps (three parses each), then parses the
entire cohort in one timed loop. The second pass reverses library/API order.
Both passes count equally. Parsing, required text conversion, allocations,
object-count access, result release, loop bookkeeping and normal GC during the
loop are included. Imports, preloading, warmup and process shutdown are not.
Every map's object count must agree across all batches, not just the total.

This prevents a preceding file or traversal call in the same process from
warming a resident-input call. It measures amortized warm batch cost, not cold
startup or isolated-request latency. File APIs still open/read/close the file;
preloading ensures warm OS page-cache contents. No concurrent benchmark workers
run during these batches. Two passes are not a confidence interval.

### Supplement: interleaved full-corpus sweeps

Each decoder warms on 64 evenly spaced maps before two complete passes through
the corpus. We take one timed sample per map per pass, rotate worker/workload
order per map, and reverse it in pass two. **Means retain every timed sample**,
not per-map minima or only fast passes. Per-pass means in the evidence report
show variation; two passes do not establish a confidence interval.
Processes stay alive, but interleaving these runtimes does **not** guarantee hot
CPU caches. This is not an isolated, tight-loop batch benchmark.

Timers include parsing, required text conversion, allocation, reading the
hitobject count, and immediate result cleanup. Imports, JSON IPC and input
pre-reading are excluded. Managed runtimes use normal GC, not a forced
collection per map; any deferred collection outside the timer is not charged
to parsing. This measures warm per-call API latency, not process startup,
end-to-end batch throughput or peak memory.

Resident input and warm-file API calls are separate workloads. File contents
are already in the OS page cache; no claim about cold disk I/O is made. FOSU
does not receive pre-parsed input or skip required native input copies.

The original `8a51ae8` native-view Python calls shared a process per library/backend. Their
order materially affects CPU-cache warmth: FOSU AVX2 resident-input means were
84.3 and 40.3 µs in the two passes. We retain those raw measurements but **do not
use their pooled means for the Python headline**. Non-Python workers have one
workload each. The traversal supplement also exposes both pass means rather
than pretending it is an isolated workload.

## Python APIs: warm batch cost

Same **9,758 maps / 392,122,516 bytes / 7,863,673 hitobjects** in every batch.
Microseconds per map, lower is better. Means include both complete passes;
OsuPyParser has no published resident-input API.

FOSU and rosu-pp-py rows use a two-pass measurement on the same host, interpreter
and cohort. Other libraries retain the original measurements. Full Python
value construction and release are included for FOSU;
[batch evidence](../bench/comparison/results/hetzner-2026-09-08.cached-slots-python-batch.json)
records both passes and the matching corpus fingerprint. These FOSU Python
batches use source `2d010a1`; native and traversal rows below retain `d31cddf`.

| Python interface | Resident bytes (µs/map) | Warm file (µs/map) |
|---|---:|---:|
| FOSU Python AVX2 (eager) | 498.3 | 510.7 |
| FOSU Python scalar (eager) | 548.7 | 554.7 |
| rosu-pp-py 4.0.2 | 310.5 | 319.1 |
| pyttanko 2.1.0 | 2,501.5 | 2,501.2 |
| OsuPyParser 1.0.7 | Unsupported (file-only API) | 5,070.8 |
| slider 0.8.4 | 14,091.0 | 14,119.7 |

| Python interface | Resident bytes: pass 1 / 2 | Warm file: pass 1 / 2 |
|---|---:|---:|
| FOSU Python AVX2 (eager) | 496.0 / 500.6 | 507.0 / 514.3 |
| FOSU Python scalar (eager) | 551.1 / 546.3 | 556.7 / 552.8 |
| rosu-pp-py 4.0.2 | 315.7 / 305.2 | 314.9 / 323.3 |
| pyttanko 2.1.0 | 2,510.7 / 2,492.3 | 2,510.2 / 2,492.2 |
| OsuPyParser 1.0.7 | — | 5,080.7 / 5,060.9 |
| slider 0.8.4 | 14,067.5 / 14,114.5 | 14,004.4 / 14,235.0 |

## Other languages: interleaved API latency

Same **9,986 maps / 399,831,198 bytes** for every row. Resident input;
microseconds per map, lower is better. These are supplemental interleaved
per-call measurements, **not directly comparable to the Python batch table**.
Do not interpret the C++/Python difference as binding overhead.

FOSU rows are a separate **FOSU-only** refresh with the same timer boundary,
cohort and two-pass mean; third-party rows retain the original multi-runtime
sweep. A smaller worker mix can change CPU-cache interference. These are not
a simultaneous rerun of all libraries or a controlled cross-revision speedup.
The [refresh evidence](../bench/comparison/results/hetzner-2026-09-08.fosu-refresh.json)
retains every pass and the original cohort-report hash.

| Library / interface | Mean µs/map | Pass 1 / pass 2 |
|---|---:|---:|
| FOSU C++ AVX2 | 32.5 | 32.7 / 32.2 |
| FOSU C++ scalar | 77.8 | 78.0 / 77.6 |
| rosu-pp (Rust) | 332.9 | 331.9 / 334.0 |
| Coosu (C#) | 619.6 | 621.3 / 617.9 |
| rosu-map (Rust) | 680.0 | 683.6 / 676.4 |
| OsuParsers (C#) | 851.0 | 841.1 / 861.0 |
| Official osu!lazer decoder (C#) | 2,839.7 | 2,665.5 / 3,013.9 |
| osu-parsers (TypeScript/JS) | 2,842.5 | 3,042.4 / 2,642.5 |
| osu-parser (JavaScript) | 54,387.8 | 54,456.2 / 54,319.4 |

## Python object traversal

FOSU rows use the eager API in the same FOSU-only refresh. Third-party rows
retain the original sweep; the worker-mix limitation above also applies here.

Parse resident bytes, then sum every hitobject's start time through the public
Python API. Same **9,948 maps**; counts and summed times agree. These figures
include both parsing and traversal, not traversal alone. They share the
interleaved sweep's cache-order limitation described above.

| Library | Mean µs/map | Pass 1 / pass 2 |
|---|---:|---:|
| FOSU Python AVX2 (eager) | 588.2 | 577.8 / 598.6 |
| FOSU Python scalar (eager) | 630.2 | 620.7 / 639.6 |
| pyttanko | 2,175.7 | 2,119.3 / 2,232.2 |
| slider (`stacking=False`) | 14,252.8 | 13,793.1 / 14,712.6 |

## Full-corpus coverage

All **10,000 maps** were attempted in the interleaved sweep. A failed file
means an exception or timeout in either pass; count differences are not counted
again as failures. Matching FOSU's count is not an independent correctness verdict.
Byte/file results agree for libraries with both APIs; FOSU scalar/AVX2 agree.

| Library | Failed files | Decoded with different object count | Matching files |
|---|---:|---:|---:|
| FOSU, rosu-map, rosu-pp, rosu-pp-py, osu!lazer, osu-parsers (each) | 0 | 0 | 10,000 |
| OsuParsers | 0 | 1 | 9,999 |
| Coosu | 6 | 1 | 9,993 |
| osu-parser | 7 | 1 | 9,992 |
| slider | 24 | 1 | 9,975 |
| pyttanko | 48 | 1 | 9,951 |
| OsuPyParser | 191 | 1 | 9,808 |

`pyttanko` rejects 48 non-standard-mode maps. `osu-parser` has five timed-out
files (10 seconds per request) and two `TypeError` files. OsuPyParser reports
90 `IndexError` and 101 `ValueError` files; slider reports 24 `ValueError`
files and Coosu six `InvalidOperationException` files. Exact file IDs and
failure types remain in the evidence. We do not silently repair inputs for
one parser or remove slow successful samples. The C# OsuParsers adapter uses
its public line-based API to avoid the stream overload's Linux CRLF bug.

## Reading the results

Every row in a table uses the **same common subset** of the 10,000 attempted
files: all included APIs must succeed in both passes and return matching
hitobject counts. Python's resident/file columns share a cohort; the
cross-language and traversal tables use their own intersections. Failed maps
do not contribute artificially short timings. The evidence report preserves
full-corpus coverage and excluded file IDs rather than silently dropping them.

Count agreement is a sanity check, **not full semantic parity**. Libraries
differ in metadata coverage, precision, malformed-record handling, slider
representations, legacy corrections and derived data. FOSU retains raw hit
samples and slider edge fields and skips storyboard command bodies. Richer
libraries can do significantly more work; PP libraries can retain less.

Every current FOSU Python row measures complete, detached Python results.
The original `8a51ae8` native-view measurements remain only in the historical
evidence artifacts; they do not describe the current API's result construction.
The traversal workload additionally sums every hitobject start time through
public Python objects, making that access cost visible. `rosu-pp-py` does not
expose an equivalent iterable hitobject API, so it is not included in traversal.

For the legacy JavaScript `osu-parser`, slider endpoint calculation is part of
its public parse call. Its large costs on some maps are not evidence that
JavaScript delimiter parsing alone is that slow. Similarly, the official osu!
decoder is intended to prepare a much richer game representation.

Choose a parser for its output and compatibility contract first. These results
support FOSU for fast structural parsing, not replacing another library's PP,
gameplay or geometry functionality without implementing those missing pieces.

## Reproduction and evidence

Published evidence:

- [Current FOSU and rosu-pp-py batches](../bench/comparison/results/hetzner-2026-09-08.cached-slots-python-batch.json)
- [Python construction experiments and full-corpus comparisons](../bench/results/2026-09-08-python-conversion.json)
- [Current FOSU-only refresh](../bench/comparison/results/hetzner-2026-09-08.fosu-refresh.json)
- [All 160,000 refresh records](../bench/comparison/results/hetzner-2026-09-08.fosu-refresh.samples.csv.gz)
- [Refresh corpus manifest](../bench/comparison/results/hetzner-2026-09-08.fosu-refresh.corpus.csv.gz)

Original multi-library measurements (including the obsolete FOSU native-view API):

- [Python batch timings](../bench/comparison/results/hetzner-2026-09-08.python-batch.json)
- [Full interleaved summary and coverage](../bench/comparison/results/hetzner-2026-09-08.json)
- [All 480,000 interleaved records (CSV.gz)](../bench/comparison/results/hetzner-2026-09-08.samples.csv.gz)
- [10,000-file corpus manifest (CSV.gz)](../bench/comparison/results/hetzner-2026-09-08.corpus.csv.gz)

See [the harness](../bench/comparison/README.md) for pinned dependencies, exact
public calls, build commands, timing boundaries and report inclusion rules.
The checked-in summary records the corpus fingerprint, source/package versions,
all-sample and per-pass means, full-corpus failures, count/checksum mismatches,
and the raw-results SHA-256. A compressed CSV preserves all measured timings,
counts and failure types, including excluded maps, without publishing local
paths or exception messages. The corpus itself is not bundled.
A compressed corpus manifest lists the public beatmap file IDs, sizes and
SHA-256 hashes; it contains neither beatmap contents nor private source keys.

The [FOSU-only performance measurements](performance.md) use means of
per-map minima from immediately repeated hot parses. They answer a different
question and must not be divided into these competitors' all-sample means to
claim a speedup.
