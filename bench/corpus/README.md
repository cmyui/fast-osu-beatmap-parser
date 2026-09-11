# Corpora

Use a small representative all-mode sample for routine performance comparisons,
and the full compatibility corpus for broad correctness checks. Raw maps and
generated reports stay outside version control.

The current performance snapshot uses 1,024 entries (986 unique beatmaps), 256
per mode and 46,029,610 bytes. Selection is stratified by file size,
slider/hold share and timing-row count from popular ranked/approved maps. Its
content fingerprint is recorded in [the comparison report](../../docs/comparison.md).

The retained compatibility corpus contains 15,952 ranked/approved maps:
9,952 standard and 2,000 each of taiko, catch and mania. Selection used playcount;
it is not a representative traffic-weighted distribution.

Pass the same directory to each benchmark variant. Compare only runs with the
same files, host, compiler and timing boundary. Supply a corpus manifest to the
competitor harness's `--corpus-manifest` option when available.

See [performance](../../docs/performance.md) for FOSU checks and
[competitor comparisons](../comparison/README.md) for the cross-library harness.
