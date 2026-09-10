# Parsing contract and compatibility

fosu decodes legacy `.osu` files and applies the official decoder's metadata
precision/clamps, legacy clock offsets, stable hitobject ordering and combo rules.
Optional calculations provide slider paths, end times, path events and unmodded
osu!standard stacking. They do not resolve samples, apply mods or convert rulesets.
Retained paths use decoder geometry, not ruleset-specific Catmull rendering optimisations.
Sample strings and encoded type bits are
retained separately from effective combo flags.

## Numeric and malformed-input behavior

- Object start times, spinner/hold end times and break endpoints are double
  milliseconds; fractional values are preserved. Circle `end_time` equals its
  start. Slider `end_time` is zero by default in both interfaces and must not be
  used unless end-time calculation is requested, directly or through slider events
  or standard stacking. Calculation includes repeats and degenerate-path handling.
  Spinner, hold and break endpoints follow the
  official clamps; spinners are centred at (256, 192). Pre-v5 timestamps use
  the official +24 ms adjustment, including its distinct hold-end ordering.
- Coordinate acceptance follows the official decoder's float32 conversion and
  ±131,072 bound; accepted coordinates truncate toward zero. Timestamps,
  timing-point beat lengths and double metadata use its ±2,147,483,647 bound.
  Slider lengths use ±131,072. Out-of-range fields are rejected, not saturated.
- Integer fields use the official symmetric ±2,147,483,647 range, even when
  fosu stores them in a wider integer. Boolean fields parse a complete integer
  and compare it with 1. Mode must name a legacy ruleset (0–3). Countdown
  accepts official names and underlying int32 values.
- Sample sets are enums with values `None` (0), `Normal` (1), `Soft` (2), and
  `Drum` (3). General metadata accepts those names or their integer spellings;
  timing points accept 0–3. Zero is the legacy default selector, not an error:
  timing points use the beatmap default, and a General `None` denotes normal.
  Unknown values and comma-separated sample-set combinations are malformed.
  Slider curve types must be `B` (Bezier), `C` (Catmull), `L` (linear), or `P`
  (perfect curve). Unknown curve types reject the hitobject, not the whole map.
- Difficulty values and stack leniency decode directly to float32, then widen
  to double storage. Difficulty and editor settings use the official clamps;
  mania circle size is a key count bounded to 1–18. Omitted editor distance
  spacing is 1 and grid size is 0. Metadata keys and text values trim .NET
  whitespace, including its Unicode whitespace characters.
- Decimal and exponent conversion is bounded by the field's logical end and
  independent of the process locale. Large significands use a correctly rounded
  fallback rather than rounding an intermediate integer. Overflow is rejected;
  underflow rounded to signed zero is accepted. Numeric fields allow surrounding
  ASCII whitespace.
- Slider span counts above 9,000 are rejected. Counts below 1 become 1, and
  negative lengths become zero. Geometry-dependent repeat corrections remain
  outside this parser.
  An omitted slider length is represented as zero. Missing hold end times use
  the start time. Hit-sample and edge-bank fields remain raw strings, with the
  numeric portions the official decoder reads checked before retaining an object.
  Every raw field ends at the next comma, matching the official decoder's field
  split: a slider sample such as `0:0:0:0:a,b` is retained as `0:0:0:0:a`
  regardless of line length.
- NaN is retained **only for inherited timing-point beat lengths**. It must not
  be treated as an ordinary slider-velocity number. Other NaN/infinity values
  are rejected. Duration uses velocity 1 for inherited NaN; consumers generating
  ticks must also respect its tick-suppression meaning.
- Invalid hitobjects and timing points are skipped and counted in
  `stats.malformed_lines`. A rejected slider can retain unreferenced points in
  the native pool; use each slider's explicit point range. Python exposes only
  the points of accepted sliders. Invalid known numeric or enum
  metadata retains its previous/default value and increments the same counter.
- Combo colours accept indices 1–8 and three integer RGB components in 0–255.
  Like the official legacy decoder, a fourth component is accepted but ignored.
  Invalid colours increment `stats.malformed_lines`; valid colours with other
  keys or combo indices are ignored.
- Section and metadata names must match completely. Unknown fields/sections
  are ignored. An empty input produces an empty/default result; successful
  parsing is not proof of a valid or playable beatmap. Comments and blank lines
  are ignored. Storyboard bodies are counted rather than interpreted.

Hitobjects are stably sorted by timestamp only when input order decreases.
Equal-time objects keep their source order, and slider indices still identify
their original pool entries. Sorting uses temporary arena memory. Effective
`new_combo` and `combo_skip` follow the first-object, post-spinner and post-break
rules while `type` retains the source bits. Section-selective parsing applies
rules using only the selected data; omitted events cannot contribute breaks,
and omitted General metadata leaves the mode at its default.

Inputs are bounded by the process address space rather than an arbitrary format
limit. Sizes that cannot fit together with the parser's readable padding return
`ErrorCode::InputTooLarge`; `make_padded` returns an empty `FileBuffer`, and
`read_into` returns failure with `errno=EFBIG`.
Output arrays and temporary allocations can exceed the source size. This is
not a strict memory or CPU quota, particularly for consumer geometry code.

The C++ parser copies pointer inputs into its working arena and appends the 128
readable zero bytes required by its fast paths. Callers therefore need only
provide the exact logical byte range. Parser allocation failures become
`ErrorCode::AllocationFailure` or Python `MemoryError`
at the respective API boundary.

The test suite checks malformed bytes with ASan, UBSan and differential fuzzing.
These are evidence about tested behavior, not a sandbox or a claim that all
possible native-code vulnerabilities have been excluded. Applications should
reject incomplete results when their use requires every record, and apply their
own gameplay and resource constraints before expanding slider curves/repeats.

## Sources of truth

A previous fosu release is a regression baseline, not the definition of osu!
correctness. The official osu! decoder is the reference for legacy syntax and
numeric behavior. FOSU deliberately rejects unknown enum values rather than
exposing undefined choices through its typed APIs, even where the official
decoder accepts them. These domain checks are not a ranking validator.
Third-party parsers are not the authority for those decisions. Duration resolves
timing and curve distance; resolved samples, path-position queries and ruleset
processing remain outside decoding.

The reference is the unmodified open-source legacy decoder from osu! at
[`48c4800e3ae4ee752452cdff83bd3787ccf3105f`](https://github.com/ppy/osu/tree/48c4800e3ae4ee752452cdff83bd3787ccf3105f).
This is lazer's implementation of legacy `.osu` decoding, not an execution of
the closed-source stable client. Its
[numeric helpers](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Beatmaps/Formats/Parsing.cs),
[legacy decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Beatmaps/Formats/LegacyBeatmapDecoder.cs)
and [object decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs)
define the tested numeric limits and inherited-NaN behavior. Record acceptance
also follows the explicit enum restrictions above.

Rejection is usually **per line**, not per file. For example,
`OverallDifficulty:7` followed by `ApproachRate:1e309` leaves AR at 7 and continues
loading the map. fosu likewise retains the previous/default field and counts
the rejected line. Callers decide whether a partial result is useful.

The executable reference harness records exceptions from the official decoder
and lets its own outer error handler decide whether decoding continues. Run it
with .NET 8 and an interpreter with fosu installed:

```sh
sh tests/reference/official/build.sh
python tests/test_official.py
FOSU_BACKEND=scalar python tests/test_official.py
python tests/test_official.py --corpus /path/to/maps --report /private/report.json
```

The synthetic suite checks field rejection and retained object counts. The
explicit enum-policy cases assert both osu!'s acceptance and FOSU's rejection;
they are not skipped comparisons. The
corpus audit compares whole-map completion, rejection counts and object counts;
it does not prove equality of every gameplay value or identify every rejected
line in fosu. Keep corpus reports private: they contain local paths.

For field-level corpus comparisons, run `tests/test_official_values.py`. Its
[reference projection and normalization inventory](../tests/reference/official/README.md)
describe which values come directly from decoded objects and which raw fields
are recovered from officially accepted lines. The audit also compares retained
paths and event descriptors, and uses the official standard processor for stacking.
Path coordinates allow 0.001-pixel/1e-6-relative tolerance for float arithmetic;
other fields are compared exactly. This is not equality with the complete gameplay
model: sample resolution, mods and ruleset conversion remain outside the API.

This is bounded compatibility evidence, not complete format or stable-client
parity. The raw parser is not a replacement for the game's package loader,
storyboard renderer or ruleset processing. Empty raw buffers remain valid empty
results even though the official file decoder requires a header/content.
Storyboard elements and their source commands are decoded, but FOSU does not
render them or evaluate the command timeline.
Lazer-only curve segments and versions are outside the supported scope.
