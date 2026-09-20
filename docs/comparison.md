# Public parser comparison

All rows were measured on 2026-09-20 from FOSU 0.5.0 (`798b810`), using
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

`Exact` means the invocation requests the stated FOSU contract. `Superset` means
the parser unavoidably performs additional work. `Different` covers richer models
whose fields and processing do not form a strict subset or superset.

## Capability and execution model

| Library | Normal timed result | Slider/gameplay work | Relation to structural decode |
|---|---|---|---|
| FOSU | Full supported document; detached objects in Python and parser-owned records in C++ | End times, paths, events, stacking and mods are explicit parse options | Exact baseline |
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

Each value is the median of six steady-state passes from three independent
two-pass runs after one discarded warm-up batch. Pass detail is the complete
minimum–maximum range.

| Python interface | Contract | Resident bytes | Warm file | Pass detail: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2 | Exact | 165.1 | 174.7 | 161.2–169.8 / 172.2–176.6 |
| FOSU scalar | Exact | 224.4 | 231.9 | 223.3–228.1 / 229.9–247.5 |
| OsuPyParser | Different | Unsupported | 4,418.2 | — / 4,402.8–4,423.7 |

OsuPyParser has no published resident-input API.

## Python structural decode: standard

The 250-entry common standard cohort allows standard-only and standard-focused
libraries to participate. slider's normal parse already performs the geometry
work described in the next section.

| Python interface | Contract | Resident bytes | Warm file |
|---|---|---:|---:|
| FOSU AVX2 | Exact | 174.4 | 186.1 |
| FOSU scalar | Exact | 219.7 | 228.6 |
| OsuPyParser | Different | Unsupported | 4,072.7 |
| slider | Superset | 15,864.6 | 16,096.7 |

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
| FOSU AVX2 | Explicit geometry options | 483.5 | 490.8 | 480.5–494.8 / 489.5–524.0 |
| FOSU scalar | Explicit geometry options | 530.7 | 545.2 | 523.0–537.6 / 534.0–564.3 |
| slider | Geometry built during parse | 17,289.6 | 17,410.9 | 17,143.7–17,423.6 / 17,241.6–17,757.7 |

Eager Python construction and geometry work reduce the relative backend difference
in this scenario; native feature costs are reported separately in
[the FOSU performance matrix](performance.md).

## Native and cross-language structural decode

Resident-input API latency over the 1,023-entry common all-mode cohort. Every
runtime is a persistent worker, but each timed call creates a fresh parser/result.
Jobs rotate per map and reverse in pass two. This is an interleaved public-API
comparison and is not directly comparable to the isolated Python batches.

| Library / interface | Contract | Mean | Pass 1 / pass 2 |
|---|---|---:|---:|
| FOSU C++ AVX2 | Exact | 50.2 | 50.7 / 49.7 |
| FOSU C++ scalar | Exact | 109.0 | 109.8 / 108.2 |
| rosu-map (Rust) | Closest structural scope | 648.0 | 645.5 / 650.6 |
| Coosu (C#) | Different | 790.2 | 926.5 / 654.0 |
| OsuParsers (C#) | Superset | 1,038.3 | 1,107.7 / 968.9 |
| osu-parsers (TypeScript) | Different | 3,357.9 | 3,468.4 / 3,247.3 |
| Official osu!lazer decoder (C#) | Superset | 4,010.8 | 4,294.2 / 3,727.4 |
| osu-parser (JavaScript) | Superset | 15,836.4 | 16,256.7 / 15,416.2 |

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

Python headlines use the median of three independent two-pass runs after one
discarded warm-up batch. Each job receives a fresh process, preloads the common
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

Builds finished before timing, workloads ran serially, and host load was recorded.
WSL reported no swapping or CPU steal during the run. Python batch medians retain
all six measured passes; their full ranges are shown above, including slower
passes. The largest FOSU batch range was 7.7% of the fastest pass. Managed-runtime
variation in the interleaved table is substantially larger (for example, Coosu's
926.5 versus 654.0 µs/map); these are observed pass means, not confidence bounds
or evidence of an otherwise identical workload.

## Reproduction

See [the comparison harness](../bench/comparison/README.md) for pinned versions,
commands, inclusion rules and timing boundaries. Generated evidence belongs in
ignored `build/` directories; the corpus itself is not bundled.
