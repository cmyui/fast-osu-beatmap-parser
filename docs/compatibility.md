# Parsing contract and compatibility

fosu decodes the raw fields of legacy `.osu` files. It does not compute the
playable objects that a ruleset produces from those fields. In particular,
slider duration and geometry, stacking, mods, legacy clock offsets, timing-point
resolution, object sorting, combo processing and ruleset conversion belong to
the consumer. Input order and raw sample strings are retained.

## Numeric and malformed-input behavior

- Object start times, spinner/hold end times and break endpoints are double
  milliseconds; fractional values are preserved. Circle and slider `end_time`
  remain zero: a slider's end depends on timing points and difficulty settings.
- Coordinates truncate toward zero. Coordinates and object/break times retain
  the signed-32-bit saturation range. Timing-point offsets and beat lengths
  are doubles. This range is a storage policy, not osu!'s acceptance criterion.
- Decimal and exponent conversion is bounded by the field's logical end and
  independent of the process locale. Large significands use a correctly rounded
  fallback rather than rounding an intermediate integer. Overflow is rejected for double
  fields; integer overflow saturates without signed-overflow undefined behavior.
- NaN is retained **only for inherited timing-point beat lengths**. It must not
  be treated as an ordinary slider-velocity number. Other NaN/infinity values
  are rejected. A consumer implementing slider duration/ticks must handle the
  inherited-NaN case explicitly.
- Invalid hitobjects and timing points are skipped and counted in
  `stats.malformed_lines`. A rejected slider can retain unreferenced points in
  the pool; use each slider's explicit point range. Invalid known numeric
  metadata retains its previous/default value and increments the same counter.
- Section and metadata names must match completely. Unknown fields/sections
  are ignored. An empty input produces an empty/default result; successful
  parsing is not proof of a valid or playable beatmap. Comments and blank lines
  are ignored. Storyboard bodies are counted rather than interpreted.

All entry points accept at most **64 MiB** of source bytes. Python rejects
larger inputs with `ValueError`; the C ABI returns `FOSU_INVALID_ARGUMENT`.
C++ `parse_into` and `make_padded` throw `std::length_error`; file helpers return
failure with `errno=EFBIG`. The standalone executable exits with status 6.
Output arrays and temporary allocations can exceed the source size. This is
not a strict memory or CPU quota, particularly for consumer geometry code.

The C++ pointer interface requires a valid input allocation with 128 readable
zero bytes after its logical end. File helpers, the C ABI and Python provide
that storage. Passing arbitrary pointers or unpadded memory violates the C++
contract. Hosted allocation failures become `std::bad_alloc`,
`FOSU_OUT_OF_MEMORY`, or Python `MemoryError` as appropriate.

The test suite checks malformed bytes with ASan, UBSan and differential fuzzing.
These are evidence about tested behavior, not a sandbox or a claim that all
possible native-code vulnerabilities have been excluded. Applications should
reject incomplete results when their use requires every record, and apply their
own gameplay and resource constraints before expanding slider curves/repeats.

## Sources of truth

A previous fosu release is a regression baseline, not the definition of osu!
correctness. Corrections are checked against independent implementations, with
raw decoding separated from their subsequent gameplay transformations.

The official osu! legacy decoder at revision
[`48c4800e`](https://github.com/ppy/osu/tree/48c4800e3ae4ee752452cdff83bd3787ccf3105f)
provides two relevant rules:

- Its [timing-point decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Beatmaps/Formats/LegacyBeatmapDecoder.cs#L488)
  accepts inherited NaN beat lengths to disable slider ticks, with velocity 1,
  and rejects NaN in an uninherited timing point.
- Its [object decoder](https://github.com/ppy/osu/blob/48c4800e3ae4ee752452cdff83bd3787ccf3105f/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs#L53)
  reads start/end timestamps as doubles. It also applies coordinate limits,
  legacy-format conversion and gameplay normalization which fosu does not
  reproduce simply by returning raw records.

The synthetic numeric fixtures exercise these decoding rules. They do not
constitute a full differential run of the official game, nor establish parity
for lazer-only formats or the whole stable ruleset.
