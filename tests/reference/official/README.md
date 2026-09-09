# Official reference audits

`build.sh` builds the pinned, unmodified osu! legacy decoder. `AuditDecoder`
observes its accepted lines and exceptions; it rethrows decoder exceptions so
the official outer decoder still decides whether to continue.

`tests/test_official.py` compares whole-map completion, line rejection counts,
hitobject counts, and parsed bookmarks. Its synthetic enum cases explicitly
assert the documented differences between osu! and FOSU's typed domains.

`tests/test_official_values.py` additionally requests `RawFields`. It audits
the represented object, slider, timing-point, break, colour, and explicitly
provided header fields. Metadata settings, object order, positions, endpoints,
and combo values come from the finished official map. This is not an assertion
that FOSU builds every part of osu!'s gameplay model.

```sh
sh tests/reference/official/build.sh
python tests/test_official_values.py --corpus /path/to/corpus/files \
  --report /private/official-values.json
FOSU_BACKEND=scalar python tests/test_official_values.py --corpus /path/to/corpus/files \
  --report /private/official-values-scalar.json
```

## Raw fields versus gameplay values

The official decoder does not retain every original field. The test projection
uses its decoded objects/properties where representations agree, and its public
`Parsing` helpers on **officially accepted lines** for raw values it discards.
It does not call FOSU or port FOSU's fast paths. Projection errors are reported
separately and never treated as official line rejections.

| Boundary | Comparison treatment |
|---|---|
| Object order and old-format offsets | Compare the final stable timestamp order with offsets enabled. Original type bits remain associated with the corresponding official object. |
| Object positions and timestamps | Compare the final decoded position and start time, including centred spinners and pre-v5 offsets. |
| Object end times | Compare the official final end time, including slider geometry/timing, duration clamping and the legacy hold offset ordering. |
| Slider repeats and length | Compare spans clamped to at least 1 and nonnegative declared length. Geometry-dependent repeat corrections remain excluded from these raw fields. |
| Slider path | Compare raw curve choice and control points with official coordinate conversion, before duplicate-point handling, relative coordinates, or perfect-curve normalization. Python's initial object-position point is included. |
| Samples and flags | Compare original hitsound/type bits and sample/edge strings. Effective new-combo and colour-skip values come from the finished official object. Resolved samples and generated node samples remain excluded. |
| Timing points | Compare every accepted source row with its legacy timestamp offset, including zero/default selectors. Do not compare deduplicated control points, resolved sample banks, clamped slider/scroll velocity, or tick generation. |
| Difficulty and editor fields | Compare official final precision, clamps, and defaults for difficulty, stack leniency, distance spacing, beat divisor, grid size, timeline zoom and preview time. Float32 properties are widened to double before JSON serialization to preserve their exact value. |
| Filenames and events | Compare explicit source filenames and normalized break endpoints, not standardized paths, video-as-background repairs or storyboard background fallback. Video filenames are retained even though the official beatmap decoder discards them. |
| Metadata whitespace | Compare text exactly after the official whitespace trimming. There is no whitespace exception in the audit. |
| Python conveniences | Compare `-1` identifiers/preview times as `None` and `bookmark_list` with the official integer-list parsing. Text fields use the C names `tags` and `bookmarks`. Native/Python regression checks cover other derived conveniences. |
| Enums | Explicitly record FOSU's stricter sample-set/curve-type rejection policy rather than pretending official acceptance agrees. |

`UseSkinSprites`, `OverlayPosition`, and `SkinPreference` are retained legacy
fields that this pinned official decoder ignores; those checks validate raw
retention, not an official decoded property. Storyboard command bodies are not
parsed by FOSU, and its fast/slow-path counters have no official counterpart.
Absent defaults outside the explicitly finalized fields above are covered by
ordinary unit tests, not inferred by this corpus audit. The synthetic files in
`tests/fixtures/official` exercise finalization and run through this same audit
in CI, in addition to the native unit tests.

Reports contain local paths and input-derived values; keep them private. A
successful audit is bounded evidence about these files and these explicit
representations, not a proof of complete gameplay compatibility.
