# Public parser comparison

All rows were measured on 2026-10-06 from FOSU commit `ab0301c` (after 0.8.0), using
the fixed cohorts and timing protocols below. This comparison asks how long documented public APIs take to produce useful beatmap
results. It does not pretend that every parser returns the same model.

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

FOSU rows use both [parser lifetimes](performance.md#parser-lifetime); the
other libraries are measured through their normal per-call APIs.

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

The official osu!lazer completely and rosu-map largely support lazer-specific
v128 beatmap features. FOSU supports the v128 fields represented by its public
model when parsing as lazer, since osu!stable has no v128 rules; other parsers
in this table have more limited or no v128 coverage.

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
| FOSU AVX2, reused parser | Exact | 165.7 | 176.2 | 165.7–171.1 / 176.2–180.5 |
| FOSU scalar, reused parser | Exact | 226.0 | 240.3 | 226.0–235.3 / 240.3–246.0 |
| FOSU AVX2, `fosu.parse` per call | Exact | 274.9 | 280.1 | 274.9–285.4 / 280.1–289.0 |
| FOSU scalar, `fosu.parse` per call | Exact | 332.7 | 354.0 | 332.7–343.3 / 354.0–361.2 |
| OsuPyParser | Different | Unsupported | 4,544.3 | — / 4,544.3–4,618.5 |

OsuPyParser has no published resident-input API.

## Python structural decode: standard

The 250-entry common standard cohort allows standard-only and standard-focused
libraries to participate. slider's normal parse already performs the geometry
work described in the next section.

| Python interface | Contract | Resident bytes | Warm file |
|---|---|---:|---:|
| FOSU AVX2, reused parser | Exact | 174.1 | 182.0 |
| FOSU scalar, reused parser | Exact | 213.4 | 225.0 |
| FOSU AVX2, `fosu.parse` per call | Exact | 270.5 | 282.2 |
| FOSU scalar, `fosu.parse` per call | Exact | 313.8 | 330.3 |
| OsuPyParser | Different | Unsupported | 4,200.5 |
| slider | Superset | 16,214.9 | 16,308.0 |

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
| FOSU AVX2, reused parser | Explicit geometry options | 464.5 | 484.1 | 464.5–481.1 / 484.1–498.9 |
| FOSU scalar, reused parser | Explicit geometry options | 509.6 | 524.1 | 509.6–525.6 / 524.1–538.4 |
| FOSU AVX2, `fosu.parse` per call | Explicit geometry options | 596.3 | 607.6 | 596.3–614.3 / 607.6–631.9 |
| FOSU scalar, `fosu.parse` per call | Explicit geometry options | 637.4 | 661.5 | 637.4–671.5 / 661.5–674.2 |
| slider | Geometry built during parse | 17,805.0 | 17,779.8 | 17,805.0–18,265.3 / 17,779.8–18,129.7 |

Eager Python construction and geometry work reduce the relative backend difference
in this scenario; native feature costs are reported separately in
[the FOSU performance matrix](performance.md).

## Native and cross-language structural decode

Resident-input API latency over the 1,023-entry common all-mode cohort. Every
runtime is a persistent worker, and each timed call creates a new result. FOSU's
reused-parser rows keep one parser per worker; the others create one per call.
Each job parses the whole cohort on its own in every pass, in that pass's
shuffled order, and job order reverses every other pass. Each row is the
fastest of four pass means, with the range of all four. This worker comparison
uses a different cohort and harness from the Python batches; compare rows within
this table.

| Library / interface | Contract | Fastest pass | Pass range |
|---|---|---:|---:|
| FOSU C++ AVX2, reused parser | Exact | 33.5 | 33.5–34.6 |
| FOSU C++ scalar, reused parser | Exact | 97.0 | 97.0–98.8 |
| FOSU C++ AVX2, new parser per call | Exact | 127.2 | 127.2–128.7 |
| FOSU C++ scalar, new parser per call | Exact | 191.1 | 191.1–193.6 |
| rosu-map (Rust) | Closest structural scope | 610.3 | 610.3–616.0 |
| Coosu (C#) | Different | 583.0 | 583.0–2,858.4 |
| OsuParsers (C#) | Superset | 924.4 | 924.4–3,821.1 |
| osu-parsers (TypeScript) | Different | 3,280.8 | 3,280.8–3,389.4 |
| Official osu!lazer decoder (C#) | Superset | 3,626.6 | 3,626.6–10,591.6 |
| osu-parser (JavaScript) | Superset | 15,658.3 | 15,658.3–15,984.3 |

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

The corpus is the 1,024-entry [performance snapshot](../bench/corpus/README.md).

The host is a six-core Intel Core i7-8700 running Ubuntu 22.04 under WSL2 on
Windows 11. Inputs and build products use WSL's ext4 filesystem. Runs are pinned
to logical CPU 8 with the Windows High performance power plan selected. C++ workers
build through the project's CMake Release configuration with GCC 15.2: `-O3`,
the product's hardening flags and jump alignment for Intel's JCC erratum. AVX2
uses `-march=x86-64-v3`, while scalar uses `-march=x86-64` and
`FOSU_DISABLE_SIMD`. Python uses CPython 3.12.14. Timing boundaries follow
the [harness measurement contract](../bench/comparison/README.md#measurement-contract).

## Run stability

Builds finished before timing, workloads ran serially, and the host's
one-minute load average was recorded at each step; it stayed between 0.5 and
2.8.

In the worker table, every row's slowest pass is within 3.3% of its fastest,
except the three C# libraries. Their first pass takes 2.8–4.2 times their
fastest and their second up to 4.9 times
(Coosu: 2,445.1, 2,858.4, 587.7 and 583.0 µs/map), consistent with .NET's
tiered JIT still compiling on the single pinned CPU; the official decoder's
second pass is already within 24%. Each C# library's last two passes agree
within 1%. Ranges are observed pass means, not confidence bounds.

## Reproduction

See [the comparison harness](../bench/comparison/README.md) for pinned versions,
commands, inclusion rules and timing boundaries. Generated evidence belongs in
ignored `build/` directories; the corpus itself is not bundled.
