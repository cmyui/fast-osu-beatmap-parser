# Public parser comparison

All rows were measured on 2026-10-04 from FOSU 0.6.0 (`cf9350e`), using
the fixed cohorts and timing protocols below. The native comparison uses the
complete interleaved worker schedule. This comparison asks how long documented
public APIs take to produce useful beatmap results. It does not pretend that
every parser returns the same model.

## Result contracts

Performance is grouped by requested outcome. A number is published only when the
parser's public API produces that outcome; unavailable work is `Unsupported`.
Unavoidable additional work stays inside the timer.

- **Structural decode** returns the parser's normal typed beatmap without asking
  for optional geometry, gameplay transforms, difficulty or PP.
- **Geometry-ready** additionally requires slider end times and a public way to
  query slider paths. Representations may still differ.
- **Gameplay-ready** means FOSU slider events and stacking. No measured competitor
  currently exposes a sufficiently equivalent result, so this remains a
  [FOSU feature-cost comparison](performance.md), not a ranking.
- **Mod-adjusted gameplay** is likewise reported only for FOSU until another
  measured public API offers an equivalent bulk result.

FOSU appears with two parser lifetimes. A **reused parser** keeps its memory
between calls. A **new parser per call** also reserves that memory and takes a
page fault on the first write to each page, every call; Python's module-level
`fosu.parse` and `fosu.parse_file` work this way. The other libraries are
measured through their normal per-call APIs.

`Exact` means the invocation requests the stated FOSU contract. `Superset` means
the parser unavoidably performs additional work. `Different` covers richer models
whose fields and processing do not form a strict subset or superset.

## Capability and execution model

| Library | Normal timed result | Slider/gameplay work | Relation to structural decode |
|---|---|---|---|
| FOSU | Full supported document; detached objects in Python and parser-owned records in C++; a parser can be reused across calls | End times, paths, events, stacking and mods are explicit parse options | Exact baseline |
| slider 0.8.4 | Eager Python beatmap objects; reliable on the measured standard, taiko and catch maps | Slider end times and curve objects are eager; stacking and mods are follow-up calls | Superset for structural decode; comparable geometry outcome |
| OsuPyParser 1.0.7 | Eager Python document, file hash and derived statistics | File-only public API | Different, with additional eager work |
| rosu-map 0.2.1 | General-purpose legacy document | Ordinary decode only | Closest structural scope; representation differs |
| osu!lazer 2026.730.0 | Official rich ruleset model | Defaults, samples, control points and stable object sorting are eager | Superset work |
| OsuParsers 1.7.2 | Rich C# document | Storyboard decoding is eager | Superset work |
| Coosu 2.5.1 | Typed C# document | Normal post-deserialization processing is eager | Different, with additional work |
| osu-parsers 4.1.7 | Rich TypeScript document | Optional storyboard decoder disabled | Different |
| osu-parser 0.3.3 | Legacy JavaScript document | Slider endpoints, duration and maximum combo are eager | Superset work |

rosu-pp, its Python bindings and pyttanko are intentionally excluded. Their
significantly reduced PP-oriented models are not general-purpose beatmap
documents, so presenting their construction time in the same ranking would be
misleading. Replay-only libraries are not beatmap decoders. `oppai-ng` is also
omitted because its documented convenience API calculates PP; reaching into
internal parsing would not be a public-API comparison.

## Python structural decode: all modes

Isolated complete-process batch passes on the 1,004-entry common cohort:
250 standard, 248 taiko, 254 catch and 252 mania entries; 44,321,288 bytes.
Microseconds per map; lower is better.

Each API has six passes from three independent two-pass runs after one discarded
warm-up batch. Passes more than 5% above that API's unfiltered median are excluded
as presumed interference. Values are the median and minimum–maximum range of
the remaining five or six passes; see run stability below.

| Python interface | Contract | Resident bytes | Warm file | Pass detail: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2, reused parser | Exact | 164.3 | 174.1 | 161.6–172.0 / 172.9–176.1 |
| FOSU scalar, reused parser | Exact | 223.7 | 233.0 | 221.0–234.6 / 229.7–240.8 |
| FOSU AVX2, `fosu.parse` per call | Exact | 269.5 | 279.9 | 267.4–271.8 / 278.2–286.1 |
| FOSU scalar, `fosu.parse` per call | Exact | 328.3 | 340.0 | 319.7–332.2 / 338.8–352.5 |
| OsuPyParser | Different | Unsupported | 4,477.7 | — / 4,434.3–4,525.3 |

OsuPyParser has no published resident-input API.

## Python structural decode: standard

The 250-entry common standard cohort allows standard-only and standard-focused
libraries to participate. slider's normal parse already performs the geometry
work described in the next section.

| Python interface | Contract | Resident bytes | Warm file |
|---|---|---:|---:|
| FOSU AVX2, reused parser | Exact | 169.9 | 180.0 |
| FOSU scalar, reused parser | Exact | 210.1 | 224.1 |
| FOSU AVX2, `fosu.parse` per call | Exact | 269.1 | 283.8 |
| FOSU scalar, `fosu.parse` per call | Exact | 310.6 | 323.4 |
| OsuPyParser | Different | Unsupported | 4,121.6 |
| slider | Superset | 15,852.1 | 16,106.8 |

Python result construction dominates these measurements, so the backend difference
is smaller here than at the native parsing boundary.

## Python geometry-ready: standard

FOSU enables `calculate_slider_end_times` and `calculate_slider_paths`. slider's
ordinary parse eagerly constructs its end times and curve objects. Both accept
all 256 entries in this table (10,347,075 bytes). The outcome is comparable—a
caller can inspect end times and query the slider path—but the representations
are not identical. Stacking is excluded: FOSU leaves `apply_stacking` disabled,
and slider is accessed with `hit_objects(stacking=False)`.

| Python interface | Execution model | Resident bytes | Warm file | Pass detail: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2, reused parser | Explicit geometry options | 460.7 | 476.9 | 453.6–473.7 / 464.3–481.1 |
| FOSU scalar, reused parser | Explicit geometry options | 510.0 | 518.5 | 503.2–516.4 / 513.3–533.0 |
| FOSU AVX2, `fosu.parse` per call | Explicit geometry options | 594.3 | 613.3 | 591.7–603.9 / 601.7–624.2 |
| FOSU scalar, `fosu.parse` per call | Explicit geometry options | 633.7 | 650.2 | 627.4–648.3 / 639.3–676.5 |
| slider | Geometry built during parse | 17,338.0 | 17,548.9 | 17,252.2–17,445.2 / 17,299.2–18,240.2 |

Eager Python construction and geometry work reduce the relative backend difference
in this scenario; native feature costs are reported separately in
[the FOSU performance matrix](performance.md).

## Native and cross-language structural decode

Resident-input API latency over the 1,023-entry common all-mode cohort. Every
runtime is a persistent worker, and each timed call creates a new result. FOSU's
reused-parser rows keep one parser per worker; the others create one per call.
Jobs rotate per map and reverse in pass two. This is an interleaved public-API
comparison and is not directly comparable to the isolated Python batches.

| Library / interface | Contract | Mean | Pass 1 / pass 2 |
|---|---|---:|---:|
| FOSU C++ AVX2, reused parser | Exact | 44.5 | 40.0 / 49.0 |
| FOSU C++ scalar, reused parser | Exact | 100.3 | 97.0 / 103.7 |
| FOSU C++ AVX2, new parser per call | Exact | 150.4 | 167.1 / 133.8 |
| FOSU C++ scalar, new parser per call | Exact | 197.2 | 201.1 / 193.4 |
| rosu-map (Rust) | Closest structural scope | 646.8 | 646.4 / 647.2 |
| Coosu (C#) | Different | 765.6 | 868.9 / 662.2 |
| OsuParsers (C#) | Superset | 1,039.9 | 1,135.6 / 944.2 |
| osu-parsers (TypeScript) | Different | 3,539.2 | 3,634.4 / 3,444.1 |
| Official osu!lazer decoder (C#) | Superset | 4,132.1 | 4,488.0 / 3,776.2 |
| osu-parser (JavaScript) | Superset | 15,973.3 | 16,321.9 / 15,624.8 |

## Coverage

All 1,024 entries were attempted. Matching object counts are a sanity check, not
semantic proof. A failed or mismatching entry never contributes a fast timing.

| Library | Decoded / attempted | Matching FOSU object count | Notes |
|---|---:|---:|---|
| FOSU, rosu-map, osu!lazer, OsuParsers, Coosu, osu-parsers | 1,024 / 1,024 | 1,024 | All four modes |
| osu-parser | 1,023 / 1,024 | 1,023 | One standard `TypeError` |
| OsuPyParser | 1,004 / 1,024 | 1,004 | 20 parse failures across the modes |
| slider | 799 / 1,024 | 799 | All standard/taiko/catch entries; 225 mania failures |

## Measurement contract

The corpus profile contains 1,024 entries (986 unique beatmaps), 256 from each
mode and 46,029,610 bytes. It is stratified by ordinary map size, slider/hold
share and timing-row count from popular ranked/approved Akatsuki maps. The corpus
SHA-256 is `1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

The host is a six-core Intel Core i7-8700 running Ubuntu 22.04 under WSL2 on
Windows 11. Inputs and build products use WSL's ext4 filesystem. Runs are pinned
to logical CPU 8 with the Windows High performance power plan selected. C++ uses
GCC 15.2 and `-O3`; AVX2 uses `-march=x86-64-v3`, while scalar uses
`-march=x86-64` and `FOSU_DISABLE_SIMD`. Python uses CPython 3.12.14.

Python headlines use three independent two-pass runs after one discarded warm-up
batch, applying the one-sided pass exclusion above before taking the median.
Each job receives a fresh process, preloads the common
cohort, warms 64 evenly spaced entries three times, then times one complete pass.
Pass two reverses job order. Parsing, required conversion, allocations, result
inspection, normal GC and release are timed; imports, preload, warmup and
shutdown are not. Warm-file calls open, read and close files already in the OS
page cache.

The cross-language sweep keeps independent workers alive, warms each worker,
rotates job order per entry and reverses it in pass two. Input preparation and
JSON IPC are outside the timer. Managed runtimes keep normal GC behavior. A
10-second request budget records a timeout as failure and restarts the worker.

## Run stability

Builds finished before timing, workloads ran serially, and the host's
one-minute load average was recorded at each step; it stayed between 0.5 and
3.0. The one-sided Python rule excludes one of 180 complete passes, not
individual slow maps or GC events. Its raw measurements are retained; an
external cause is assumed for reporting, not established by the load logs.
The first geometry phase varied in every library, slider included, so it was
repeated; its results remain with the raw evidence.

Variation between the two interleaved passes is substantial for several rows
(for example, Coosu's 868.9 versus 662.2 µs/map); these are observed pass
means, not confidence bounds or evidence of an otherwise identical workload.
That two-pass comparison has too few repetitions to classify whole-pass
outliers reliably, so both are shown.

## Reproduction

See [the comparison harness](../bench/comparison/README.md) for pinned versions,
commands, inclusion rules and timing boundaries. Generated evidence belongs in
ignored `build/` directories; the corpus itself is not bundled.
