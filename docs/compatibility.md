# Parsing contract and compatibility

fosu decodes legacy `.osu` files and applies the official decoder's metadata
precision/clamps, legacy clock offsets, stable hitobject ordering and combo rules.
Optional calculations provide slider paths, end times, path events and unmodded
osu!standard stacking. They do not resolve samples, apply mods or convert rulesets.
Retained paths use decoder geometry, not ruleset-specific Catmull rendering optimisations.
Sample strings and encoded type bits are
retained separately from effective combo flags.

## Target client

`client` (`ParseOptions::client` in C++) chooses whose behaviour fosu follows
where osu!stable and osu!lazer read a map differently: stable, the default, or
lazer. The rest of this document describes what they share. Each client
compiles its own parse, so the choice adds no per-line cost.

- **Unloadable maps.** A map the client would not load fails the parse with
  `ParseErrorCode::Unloadable` and its 1-based line (`fosu.MapLoadError` in
  Python). Both clients refuse a map whose version number does not parse.
  stable also refuses a map with a hit object it cannot read, though it ignores
  one whose type names no kind; fosu counts that as malformed. Lazer skips
  each invalid hit object.
- **v128 maps.** stable has no rules past v14, its latest format, so stable
  mode reads a v128 map as it reads v14: it truncates coordinates, reads
  curve types as below, rejects B-spline degrees and uses the legacy path
  rules. `format_version` still reports 128. Only lazer mode applies the
  v128 rules this document describes.
- **Version line.** stable reads only the first line, if it starts with
  `osu file format`, and takes the integer after its last `v` with the full
  int32 range. Lazer reads the first non-blank line after trimming, if it
  starts with `osu file format v`, and accepts ±2,147,483,647. Otherwise the
  version is 14.
- **Before the first section.** stable treats the first line as its version
  line even when it is a section header, and then ignores that section if it
  is General, Metadata, Difficulty or TimingPoints. It ignores other lines
  before the first header, which lazer reads as `[General]`.
- **Section headers.** stable reads any line starting with `[` as a header,
  trims brackets from both ends and matches the name exactly; a name it does
  not know leaves the current section in effect. It also knows Variables,
  Unknown and All, which fosu ignores. fosu trims no whitespace, as stable's
  object reader does; its header reader also accepts `[Difficulty] `. Lazer
  needs `[name]` after trimming the line's end, starts `[General]` for an
  unknown name and ignores Variables, Fonts, CatchTheBeat and Mania. fosu
  accepts neither client's numeric, comma-list or padded enum spellings.
- **After `[HitObjects]`.** stable's header reader never leaves that section,
  so later General, Metadata, Difficulty and TimingPoints sections are ignored.
  Later Events, Colours and HitObjects sections are read.
- **Indented lines.** stable skips hit-object and colour lines starting with
  a space or `_`, and hit-object lines starting with `[`. Lazer reads
  indented lines and rejects the others.
- **Curve types.** stable reads a one-character token as the curve type of
  the whole slider, the last one winning, and ignores one naming no type; a
  longer token must be a point. Lazer starts a segment at each letter and
  rejects any other one-character token; before
  v128 it gives each segment the legacy curve rules, counting the next
  segment's first point as its end. Lazer would also read a longer token starting with
  a letter as a type; fosu rejects it, so in stable mode the map is unloadable.
- **Coordinates and slider lengths.** Lazer rejects values beyond ±131,072.
  stable has no bound; fosu keeps its control points within int32 and its
  lengths finite.
- **Difficulty and stack leniency.** Before v13, stable reads HP, CS, OD and AR
  as integers from 0 to 255. It clamps circle size as it reads it, to the
  mania key range only for v13+ maps whose `[General]` already set mania, and
  clamps stack leniency to [0, 1]. Lazer clamps circle size with the final
  mode and keeps any stack leniency.
- **Defaults.** An omitted `Countdown` is 1 (normal) in stable and 0 (none) in
  lazer. An omitted `BeatmapID` is 0 in stable and -1 in lazer.

Stable mode does not yet model the rest of stable. Calculated values (slider
end times, paths, events and stacking) follow lazer in both modes. So do header
values, timing points, breaks and colours that stable cannot read, though its
header reader stops at the first such value. Hit-object fields still use
lazer's numeric parsing, where stable truncates timestamps and reads some
fields as integers. Lazer's trailing `//` comments are not stripped.

## Numeric and malformed-input behavior

- Object start times, spinner/hold end times and break endpoints are double
  milliseconds; fractional values are preserved. Circle `end_time` equals its
  start. Slider `end_time` is zero by default in both interfaces and must not be
  used unless end-time calculation is requested, directly or through slider events
  or standard stacking. Calculation includes repeats and degenerate-path handling.
  Spinner, hold and break endpoints follow the
  official clamps; spinners are centred at (256, 192). Pre-v5 timestamps use
  the official +24 ms adjustment, including its distinct hold-end ordering.
- Coordinate acceptance follows the official decoder's float32 conversion and,
  in lazer mode, its ±131,072 bound. Accepted object positions clamp to [0, 512] on both axes,
  as in osu!stable and lazer; except in lazer's v128 rules, positions then
  truncate toward zero.
  Slider control points are not clamped. Timestamps,
  timing-point beat lengths and double metadata use its ±2,147,483,647 bound.
  Slider lengths use the coordinate bound. Out-of-range fields are rejected, not saturated.
- Integer fields use the official symmetric ±2,147,483,647 range, even when
  fosu stores them in a wider integer. Boolean fields parse a complete integer
  and compare it with 1. Mode must name a legacy ruleset (0–3). Countdown
  accepts official names and underlying int32 values.
- Sample sets are enums with values `None` (0), `Normal` (1), `Soft` (2), and
  `Drum` (3). General metadata accepts those names or their integer spellings;
  timing points accept 0–3. Zero is the legacy default selector, not an error:
  timing points use the beatmap default, and a General `None` denotes normal.
  Unknown values and comma-separated sample-set combinations are malformed.
  Slider curve types are `B` (Bezier), `C` (Catmull), `L` (linear) and `P`
  (perfect curve); like both clients, fosu reads any other letter as Catmull.
  B-spline degrees (`B2|…`) are read only under lazer's v128 rules.
  Otherwise they reject the line: stable cannot load such a map, and lazer
  before v128 reads a B-spline.
- Difficulty values and stack leniency decode directly to float32, then widen
  to double storage. Difficulty and editor settings use the official clamps;
  mania circle size is a key count bounded to 1–18. Omitted editor distance
  spacing is 1 and grid size is 0. Metadata keys and text values trim .NET
  whitespace, including its Unicode whitespace characters.
- Omitted fields take osu!'s defaults: HP, CS, OD and AR 5 (a missing AR
  follows OD), slider multiplier 1.4, tick rate 1, stack leniency 0.7, sample
  volume 100, and format version 14 without a version line. Countdown and
  `BeatmapID` defaults depend on the [target client](#target-client). A missing
  set ID or preview time is -1, exposed as `None` in Python, as is lazer's
  missing `BeatmapID`. Editor settings and velocity presets (0.75, 1, 1.5)
  follow lazer: stable keeps editor settings as user preferences and has no
  velocity presets.
- Decimal and exponent conversion is bounded by the field's logical end and
  independent of the process locale. Large significands use a correctly rounded
  fallback rather than rounding an intermediate integer. Overflow is rejected;
  underflow rounded to signed zero is accepted. Numeric fields allow surrounding
  ASCII whitespace. Like both clients' .NET parsers, float and double fields
  accept `,` group separators anywhere in the integer part after its first
  digit: `1,,0` is 10. fosu reads separators only in values of up to 128 bytes.
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
- Invalid timing points, and in lazer mode invalid hitobjects, are skipped and
  counted in `stats.malformed_lines`; in stable mode an invalid hitobject
  usually makes the map unloadable. A rejected slider leaves no points or segments in
  the native pools. Invalid known numeric or enum
  metadata retains its previous/default value and increments the same counter.
- Combo colours accept indices 1–8 and three integer RGB components in 0–255.
  Like the official legacy decoder, a fourth component is accepted but ignored.
  Invalid colours increment `stats.malformed_lines`; valid colours with other
  keys or combo indices are ignored.
- Metadata names must match completely, and unknown fields are ignored; see
  [target client](#target-client) for section names. An empty input produces an empty/default result; successful
  parsing is not proof of a valid or playable beatmap. Whole-line comments and
  blank lines are ignored. As in stable, a trailing `//` stays part of its line;
  lazer strips it outside `[Metadata]`, which fosu does not support yet.
  Storyboard bodies are counted rather than interpreted.

Hitobjects are stably sorted by timestamp only when input order decreases.
Equal-time objects keep their source order, and slider indices still identify
their original pool entries. Sorting uses temporary arena memory. Effective
`new_combo` and `combo_skip` follow the first-object, post-spinner and post-break
rules while `type` retains the source bits. In lazer mode, as in lazer, a circle
or spinner line rejected only for its hit sample still counts as the previous
object for the post-spinner rule. With several kind bits set, an object is a circle, slider,
spinner or hold in that order of precedence, and the C++ `is_circle()`,
`is_slider()`, `is_spinner()` and `is_hold()` helpers follow it.
Section-selective parsing applies rules using only the selected data; omitted events cannot contribute breaks,
and omitted General metadata leaves the mode at its default.

Inputs are bounded by the process address space rather than an arbitrary format
limit. Sizes that cannot fit together with the parser's readable padding fail the
parse; `parse_file` also sets `errno=EFBIG`.
Output arrays and temporary allocations can exceed the source size. This is
not a strict memory or CPU quota, particularly for consumer geometry code.

The C++ parser copies its input into its working arena and appends the 128
readable zero bytes required by its fast paths. Callers therefore need only
provide the exact logical byte range. Parser allocation failures return
`nullptr` in C++ and raise `ValueError` in Python.

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
[`fc790c78c4b393f4a0101b9ad54cb3b4391037cd`](https://github.com/ppy/osu/tree/fc790c78c4b393f4a0101b9ad54cb3b4391037cd).
This is lazer's implementation of legacy `.osu` decoding, not an execution of
the closed-source stable client. Its
[numeric helpers](https://github.com/ppy/osu/blob/fc790c78c4b393f4a0101b9ad54cb3b4391037cd/osu.Game/Beatmaps/Formats/Parsing.cs),
[legacy decoder](https://github.com/ppy/osu/blob/fc790c78c4b393f4a0101b9ad54cb3b4391037cd/osu.Game/Beatmaps/Formats/LegacyBeatmapDecoder.cs)
and [object decoder](https://github.com/ppy/osu/blob/fc790c78c4b393f4a0101b9ad54cb3b4391037cd/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs)
define the tested numeric limits and inherited-NaN behavior. Record acceptance
also follows the explicit enum restrictions above. The reference checks lazer
mode. Stable mode follows the stable client's source, which cannot be run as a
reference.

Rejection of header values is **per line**, not per file, as is rejection of
hitobjects in lazer mode. For example,
`OverallDifficulty:7` followed by `ApproachRate:1e309` leaves AR at 7 and continues
loading the map. fosu likewise retains the previous/default field and counts
the rejected line. Callers decide whether a partial result is useful.

The executable reference harness records exceptions from the official decoder
and lets its own outer error handler decide whether decoding continues. Run it
with .NET 10 and an interpreter with fosu installed:

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
storyboard interpreter or ruleset processing. Empty raw buffers remain valid
empty results even though the official file decoder requires a header/content.
