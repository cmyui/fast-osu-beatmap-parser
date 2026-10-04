# Public parser comparison

All rows were measured on 2026-10-04 from FOSU commit `f2c86ce` (after 0.6.1), using
the fixed cohorts and timing protocols below. The native comparison times one
complete corpus pass per worker job. This comparison asks how long documented
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
warm-up batch. Values are the fastest pass and the minimum–maximum range of all
six; see run stability below.

| Python interface | Contract | Resident bytes | Warm file | Pass detail: bytes / file |
|---|---|---:|---:|---|
| FOSU AVX2, reused parser | Exact | 162.8 | 174.5 | 162.8–166.3 / 174.5–178.3 |
| FOSU scalar, reused parser | Exact | 219.0 | 227.0 | 219.0–233.6 / 227.0–306.7 |
| FOSU AVX2, `fosu.parse` per call | Exact | 265.0 | 277.2 | 265.0–274.3 / 277.2–283.5 |
| FOSU scalar, `fosu.parse` per call | Exact | 324.4 | 334.2 | 324.4–333.5 / 334.2–340.1 |
| OsuPyParser | Different | Unsupported | 4,418.9 | — / 4,418.9–4,474.3 |

OsuPyParser has no published resident-input API.

## Python structural decode: standard

The 250-entry common standard cohort allows standard-only and standard-focused
libraries to participate. slider's normal parse already performs the geometry
work described in the next section.

| Python interface | Contract | Resident bytes | Warm file |
|---|---|---:|---:|
| FOSU AVX2, reused parser | Exact | 170.5 | 178.7 |
| FOSU scalar, reused parser | Exact | 210.1 | 219.0 |
| FOSU AVX2, `fosu.parse` per call | Exact | 264.9 | 279.8 |
| FOSU scalar, `fosu.parse` per call | Exact | 307.7 | 320.3 |
| OsuPyParser | Different | Unsupported | 4,095.1 |
| slider | Superset | 15,788.3 | 15,882.0 |

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
| FOSU AVX2, reused parser | Explicit geometry options | 448.3 | 460.2 | 448.3–460.3 / 460.2–499.1 |
| FOSU scalar, reused parser | Explicit geometry options | 496.1 | 509.2 | 496.1–536.0 / 509.2–524.4 |
| FOSU AVX2, `fosu.parse` per call | Explicit geometry options | 574.9 | 583.0 | 574.9–585.0 / 583.0–604.2 |
| FOSU scalar, `fosu.parse` per call | Explicit geometry options | 610.7 | 631.2 | 610.7–649.2 / 631.2–665.6 |
| slider | Geometry built during parse | 17,369.1 | 17,365.6 | 17,369.1–17,458.8 / 17,365.6–17,836.2 |

Eager Python construction and geometry work reduce the relative backend difference
in this scenario; native feature costs are reported separately in
[the FOSU performance matrix](performance.md).

## Native and cross-language structural decode

Resident-input API latency over the 1,023-entry common all-mode cohort. Every
runtime is a persistent worker, and each timed call creates a new result. FOSU's
reused-parser rows keep one parser per worker; the others create one per call.
Each job parses the whole cohort on its own in every pass, and job order
reverses every other pass. Each row is the fastest of four pass means, with
the range of all four. This worker comparison uses a different cohort and
harness from the Python batches; compare rows within this table.

| Library / interface | Contract | Fastest pass | Pass range |
|---|---|---:|---:|
| FOSU C++ AVX2, reused parser | Exact | 34.4 | 34.4–35.6 |
| FOSU C++ scalar, reused parser | Exact | 92.7 | 92.7–93.4 |
| FOSU C++ AVX2, new parser per call | Exact | 125.7 | 125.7–128.2 |
| FOSU C++ scalar, new parser per call | Exact | 185.8 | 185.8–195.4 |
| rosu-map (Rust) | Closest structural scope | 602.1 | 602.1–608.3 |
| Coosu (C#) | Different | 573.0 | 573.0–3,073.5 |
| OsuParsers (C#) | Superset | 877.5 | 877.5–3,737.4 |
| osu-parsers (TypeScript) | Different | 3,337.5 | 3,337.5–3,479.1 |
| Official osu!lazer decoder (C#) | Superset | 3,403.3 | 3,403.3–11,898.4 |
| osu-parser (JavaScript) | Superset | 15,312.9 | 15,312.9–16,014.9 |

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
batch and report the fastest pass.
Each job receives a fresh process, preloads the common
cohort, warms 64 evenly spaced entries three times, then times one complete pass.
Pass two reverses job order. Parsing, required conversion, allocations, result
inspection, normal GC and release are timed; imports, preload, warmup and
shutdown are not. Warm-file calls open, read and close files already in the OS
page cache.

The cross-language sweep keeps independent workers alive, warms each worker,
then times one complete pass per job, reversing job order every other pass.
Input preparation and JSON IPC are outside the timer. Managed runtimes keep
normal GC behavior. A 10-second request budget records a timeout as failure and
restarts the worker.

## Run stability

Builds finished before timing, workloads ran serially, and the host's
one-minute load average was recorded at each step; it stayed between 0.2 and
3.0.

Every table reports the fastest complete run or pass. Interference from the host
only adds time, so the fastest pass is the closest observation of what each
implementation can do. Passes are never split: each keeps the garbage
collection and JIT compilation its library causes, and no individual map or
pause is filtered out. All passes remain in the raw evidence.

In the worker table, every row's slowest pass is within 5.2% of its fastest,
except the three C# libraries: their first two passes are several times slower
(Coosu: 2,438.7, 3,073.5, 576.1 and 573.0 µs/map), consistent with
.NET's tiered JIT still compiling on the single pinned CPU. Their last two passes
agree closely. Ranges are observed pass means, not confidence bounds.

## Reproduction

See [the comparison harness](../bench/comparison/README.md) for pinned versions,
commands, inclusion rules and timing boundaries. Generated evidence belongs in
ignored `build/` directories; the corpus itself is not bundled.
