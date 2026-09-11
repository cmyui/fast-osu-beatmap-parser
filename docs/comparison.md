# Public parser comparison

Measured on 2026-09-11. This comparison asks how long documented public APIs
take to produce useful beatmap results. It does not pretend that every parser
returns the same model.

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

`Exact` means the invocation requests the stated FOSU contract. `Superset` means
the parser unavoidably performs additional work. `Reduced` means important parts
of the structural result are absent, so its timing must not be read as a faster
implementation of the same output. `Different` covers richer models whose fields
and processing do not form a strict subset or superset.

## Capability and execution model

| Library | Normal timed result | Slider/gameplay work | Relation to structural decode |
|---|---|---|---|
| FOSU | Full supported document; detached objects in Python and parser-owned records in C++ | End times, paths, events, stacking and mods are explicit parse options | Exact baseline |
| slider 0.8.4 | Eager Python beatmap objects; reliable on the measured standard, taiko and catch maps | Slider end times and curve objects are eager; stacking and mods are follow-up calls | Superset for structural decode; comparable geometry outcome |
| rosu-pp-py 4.0.2 / rosu-pp 4.0.1 | Compact PP-oriented native model | No difficulty or PP calculation requested | Reduced |
| pyttanko 2.1.0 | Historical standard-only PP-oriented Python model | No difficulty or PP calculation requested | Reduced |
| OsuPyParser 1.0.7 | Eager Python document, file hash and derived statistics | File-only public API | Different, with additional eager work |
| rosu-map 0.2.1 | General-purpose legacy document | Ordinary decode only | Closest structural scope; representation differs |
| osu!lazer 2026.730.0 | Official rich ruleset model | Defaults, samples, control points and stable object sorting are eager | Superset work |
| OsuParsers 1.7.2 | Rich C# document | Storyboard decoding is eager | Superset work |
| Coosu 2.5.1 | Typed C# document | Normal post-deserialization processing is eager | Different, with additional work |
| osu-parsers 4.1.7 | Rich TypeScript document | Optional storyboard decoder disabled | Different |
| osu-parser 0.3.3 | Legacy JavaScript document | Slider endpoints, duration and maximum combo are eager | Superset work |

Replay-only libraries are not beatmap decoders. `oppai-ng` is omitted because its
documented convenience API also calculates PP; reaching into internal parsing
would not be a public-API comparison.

## Python structural decode: all modes

Two isolated complete-process batch passes on the 1,004-entry common cohort:
250 standard, 248 taiko, 254 catch and 252 mania entries; 44,321,288 bytes.
Microseconds per map; lower is better.

| Python interface | Contract | Resident bytes | Warm file | Passes: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2 | Exact | 443.6 | 444.1 | 450.9 / 436.3; 439.6 / 448.5 |
| FOSU scalar | Exact | 514.0 | 499.1 | 551.2 / 476.8; 506.2 / 492.1 |
| rosu-pp-py | Reduced | 309.4 | 333.5 | 318.3 / 300.6; 354.1 / 312.9 |
| OsuPyParser | Different | Unsupported | 4,885.0 | —; 5,014.4 / 4,755.6 |

The reduced rosu-pp-py result does not contain FOSU's complete detached Python
graph. OsuPyParser has no published resident-input API.

## Python structural decode: standard

The 250-entry common standard cohort allows standard-only and standard-focused
libraries to participate. slider's normal parse already performs the geometry
work described in the next section.

| Python interface | Contract | Resident bytes | Warm file |
|---|---|---:|---:|
| FOSU AVX2 | Exact | 445.9 | 428.3 |
| FOSU scalar | Exact | 432.9 | 447.5 |
| rosu-pp-py | Reduced | 272.4 | 295.7 |
| pyttanko | Reduced | 1,996.3 | 2,014.9 |
| OsuPyParser | Different | Unsupported | 4,405.4 |
| slider | Superset | 12,349.7 | 12,467.7 |

Python result construction dominates these measurements. The small AVX2/scalar
inversion in this two-pass standard subset is benchmark noise, not evidence that
the scalar native parser is faster.

## Python geometry-ready: standard

FOSU enables `calculate_slider_end_times` and `calculate_slider_paths`. slider's
ordinary parse eagerly constructs its end times and curve objects. Both accept
all 256 entries in this table (10,347,075 bytes). The outcome is comparable—a
caller can inspect end times and query the slider path—but the representations
are not identical.

| Python interface | Execution model | Resident bytes | Warm file | Passes: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2 | Explicit geometry options | 1,120.7 | 1,060.1 | 1,145.3 / 1,096.2; 1,041.8 / 1,078.3 |
| FOSU scalar | Explicit geometry options | 1,107.0 | 1,096.1 | 1,052.2 / 1,161.9; 1,082.9 / 1,109.3 |
| slider | Geometry built during parse | 13,338.3 | 13,387.6 | 13,465.4 / 13,211.2; 13,434.3 / 13,340.8 |

The same Python-dominated noise explains the near-equal FOSU backend timings in
this scenario; native feature costs are reported separately in
[the FOSU performance matrix](performance.md).

## Native and cross-language structural decode

Resident-input API latency over the 1,023-entry common all-mode cohort. Every
runtime is a persistent worker, but each timed call creates a fresh parser/result.
Jobs rotate per map and reverse in pass two. This is an interleaved public-API
comparison and is not directly comparable to the isolated Python batches.

| Library / interface | Contract | Mean | Pass 1 / pass 2 |
|---|---|---:|---:|
| FOSU C++ AVX2 | Exact | 46.1 | 48.0 / 44.1 |
| FOSU C++ scalar | Exact | 97.8 | 99.1 / 96.6 |
| rosu-pp (Rust) | Reduced | 328.3 | 326.0 / 330.7 |
| rosu-map (Rust) | Closest structural scope | 556.4 | 558.1 / 554.6 |
| Coosu (C#) | Different | 712.1 | 836.0 / 588.3 |
| OsuParsers (C#) | Superset | 916.1 | 1,041.3 / 790.8 |
| Official osu!lazer decoder (C#) | Superset | 2,873.4 | 3,092.6 / 2,654.1 |
| osu-parsers (TypeScript) | Different | 2,991.3 | 3,130.6 / 2,852.0 |
| osu-parser (JavaScript) | Superset | 13,339.3 | 14,027.6 / 12,651.0 |

## Coverage

All 1,024 entries were attempted. Matching object counts are a sanity check, not
semantic proof. A failed or mismatching entry never contributes a fast timing.

| Library | Decoded / attempted | Matching FOSU object count | Notes |
|---|---:|---:|---|
| FOSU, rosu-map, rosu-pp, rosu-pp-py, osu!lazer, OsuParsers, Coosu, osu-parsers | 1,024 / 1,024 | 1,024 | All four modes |
| osu-parser | 1,023 / 1,024 | 1,023 | One standard `TypeError` |
| OsuPyParser | 1,004 / 1,024 | 1,004 | 20 parse failures across the modes |
| slider | 799 / 1,024 | 799 | All standard/taiko/catch entries; 225 mania failures |
| pyttanko | 256 / 1,024 | 256 | Standard only by design |

## Measurement contract

The corpus profile contains 1,024 entries (986 unique beatmaps), 256 from each
mode and 46,029,610 bytes. It is stratified by ordinary map size, slider/hold
share and timing-row count from popular ranked/approved Akatsuki maps. The corpus
SHA-256 is `1f7e90f4ac0222f0a2b0890e6f07c70807e9cc5d5e6ac2b392b859b2fa042895`.

The host is an eight-vCPU shared-tenancy AMD EPYC Genoa VM running Ubuntu 24.04.
Runs are pinned to logical CPU 3. C++ uses GCC 13.3 and `-O3`; AVX2 uses
`-march=x86-64-v3 -mtune=znver4`, while scalar uses `-march=x86-64` and
`FOSU_DISABLE_SIMD`. Python uses CPython 3.12.3.

Python headline jobs each receive a fresh process, preload the common cohort,
warm 64 evenly spaced entries three times, then time one complete pass. Pass two
reverses job order. Parsing, required conversion, allocations, result inspection,
normal GC and release are timed; imports, preload, warmup and shutdown are not.
Warm-file calls open, read and close files already in the OS page cache.

The cross-language sweep keeps independent workers alive, warms each worker,
rotates job order per entry and reverses it in pass two. Input preparation and
JSON IPC are outside the timer. Managed runtimes keep normal GC behavior. A
10-second request budget records a timeout as failure and restarts the worker.

## Reproduction

See [the comparison harness](../bench/comparison/README.md) for pinned versions,
commands, inclusion rules and timing boundaries. Generated evidence belongs in
ignored `build/` directories; the corpus itself is not bundled.
