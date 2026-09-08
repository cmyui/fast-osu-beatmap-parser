# Parsing contract and compatibility

fosu decodes the raw fields of legacy `.osu` files. It does not compute the
playable objects that a ruleset produces from those fields. In particular,
slider duration and geometry, stacking, mods, legacy clock offsets, timing-point
resolution, object sorting, combo processing and ruleset conversion belong to
the consumer. Input order and raw sample strings are retained.

## Numeric and malformed-input behavior

- Object start times, spinner/hold end times and break endpoints are double
  milliseconds; fractional values are preserved. Native circle and slider `end_time`
  remain zero: a slider's end depends on timing points and difficulty settings.
  Python retains this value as `raw_end_time`; its `end_time` is the start time
  for a circle and `None` for a slider.
- Coordinate acceptance follows the official decoder's float32 conversion and
  ±131,072 bound; accepted coordinates truncate toward zero. Timestamps,
  timing-point beat lengths and double metadata use its ±2,147,483,647 bound.
  Slider lengths use ±131,072. Out-of-range fields are rejected, not saturated.
- Integer fields use the official symmetric ±2,147,483,647 range, even when
  fosu stores them in a wider integer. Boolean fields parse a complete integer
  and compare it with 1. Mode must name a legacy ruleset (0–3). Countdown and
  sample-bank enums accept official names and underlying int32 values.
- Difficulty values and stack leniency are stored as raw doubles, but acceptance
  uses the official float32 domain. Float rounding matters at the upper bound;
  these are parsing limits, not the subsequent gameplay difficulty clamps.
- Decimal and exponent conversion is bounded by the field's logical end and
  independent of the process locale. Large significands use a correctly rounded
  fallback rather than rounding an intermediate integer. Overflow is rejected;
  underflow rounded to signed zero is accepted. Numeric fields allow surrounding
  ASCII whitespace.
- Slider repeat counts above 9,000 are rejected. Nonpositive repeat counts and
  negative lengths remain raw values; gameplay preparation must normalize them.
  An omitted slider length is represented as zero. Missing hold end times use
  the start time. Hit-sample and edge-bank fields remain raw strings, with the
  numeric portions the official decoder reads checked before retaining an object.
  Every raw field ends at the next comma, matching the official decoder's field
  split: a slider sample such as `0:0:0:0:a,b` is retained as `0:0:0:0:a`
  regardless of line length.
- NaN is retained **only for inherited timing-point beat lengths**. It must not
  be treated as an ordinary slider-velocity number. Other NaN/infinity values
  are rejected. A consumer implementing slider duration/ticks must handle the
  inherited-NaN case explicitly.
- Invalid hitobjects and timing points are skipped and counted in
  `stats.malformed_lines`. A rejected slider can retain unreferenced points in
  the native pool; use each slider's explicit point range. Python exposes only
  the points of accepted sliders. Invalid known numeric
  metadata retains its previous/default value and increments the same counter.
- Section and metadata names must match completely. Unknown fields/sections
  are ignored. An empty input produces an empty/default result; successful
  parsing is not proof of a valid or playable beatmap. Comments and blank lines
  are ignored. Storyboard bodies are counted rather than interpreted.

All entry points accept at most **64 MiB** of source bytes. Python rejects
larger inputs with `ValueError`; the C ABI returns `FOSU_INVALID_ARGUMENT`.
C++ `Parser::parse` and `Parser::parse_file` return `ErrorCode::InputTooLarge`;
`make_padded` throws `std::length_error`, and `read_into` returns failure with
`errno=EFBIG`. The
standalone executable exits with status 6.
Output arrays and temporary allocations can exceed the source size. This is
not a strict memory or CPU quota, particularly for consumer geometry code.

The C++ parser copies pointer inputs into its working arena and appends the 128
readable zero bytes required by its fast paths. Callers therefore need only
provide the exact logical byte range. Parser allocation failures become
`ErrorCode::AllocationFailure`, `FOSU_OUT_OF_MEMORY`, or Python `MemoryError`
at the respective API boundary.

The test suite checks malformed bytes with ASan, UBSan and differential fuzzing.
These are evidence about tested behavior, not a sandbox or a claim that all
possible native-code vulnerabilities have been excluded. Applications should
reject incomplete results when their use requires every record, and apply their
own gameplay and resource constraints before expanding slider curves/repeats.

## Sources of truth

A previous fosu release is a regression baseline, not the definition of osu!
correctness. **The official osu! decoder determines acceptance and rejection
policy.** Third-party parsers are not the authority for those decisions. Raw
storage is separate from gameplay transformations such as clamping difficulty,
resolving timing points, applying format-version offsets and sorting objects.

The reference is the unmodified open-source legacy decoder from osu! at
[`48c4800e3ae4ee752452cdff83bd3787ccf3105f`](https://github.com/ppy/osu/tree/48c4800e3ae4ee752452cdff83bd3787ccf3105f).
This is lazer's implementation of legacy `.osu` decoding, not an execution of
the closed-source stable client. Its
[numeric helpers](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Beatmaps/Formats/Parsing.cs),
[legacy decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Beatmaps/Formats/LegacyBeatmapDecoder.cs)
and [object decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs)
define the tested numeric limits, inherited-NaN behavior and record acceptance.

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
FOSU_FORCE_SCALAR=1 python tests/test_official.py
python tests/test_official.py --corpus /path/to/maps --report /private/report.json
```

The synthetic suite checks field rejection and retained object counts. The
corpus audit compares whole-map completion, rejection counts and object counts;
it does not prove equality of every gameplay value or identify every rejected
line in fosu. Keep corpus reports private: they contain local paths.

This is bounded compatibility evidence, not complete format or stable-client
parity. The raw parser is not a replacement for the game's package loader,
storyboard interpreter or ruleset processing. Empty raw buffers remain valid
empty results even though the official file decoder requires a header/content.
Lazer-only curve segments and versions are outside the supported scope. The
64 MiB input cap is fosu's resource policy, not an official format restriction.
